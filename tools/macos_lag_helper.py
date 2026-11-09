#!/usr/bin/env python3
"""Root-only local broker. Launch explicitly in Terminal; never run Qt as root."""
import argparse
import contextlib
import errno
import ipaddress
import json
import os
import signal
import socket
import stat
import sys
from macos_lag import Session, integer


class Broker:
    def __init__(self, address="", port=0):
        self.address, self.port = address, port
        self.session = None
        self.values = (0, 0)

    def stop(self):
        if self.session:
            errors = self.session.close()
            if errors:
                raise RuntimeError('; '.join(errors))
            self.session = None

    def command(self, request):
        command = request.get('command')
        if command == 'status':
            return
        if command == 'stop':
            self.stop()
            return
        if command not in ('start', 'apply'):
            raise ValueError('Unknown command')
        values = request.get('delay')
        if not isinstance(values, list) or len(values) != 2 or any(type(v) is not int or not 0 <= v <= 1000 for v in values):
            raise ValueError('Delay must contain two integers in 0..1000 ms')
        if command == 'start':
            if self.session:
                raise ValueError('Stop before starting another session')
            address = request.get('address', '')
            if not isinstance(address, str):
                raise ValueError('Remote address must be a numeric IP or empty')
            address = str(ipaddress.ip_address(address)) if address else ''
            port = request.get('port', 0)
            if type(port) is not int or not 0 <= port <= 65535:
                raise ValueError('Remote port must be an integer in 0..65535')
            # Explicit CLI restrictions still cap what this session may select.
            if self.address and address and address != self.address:
                raise ValueError('Selected IP differs from the authorized helper target')
            if self.port and port and port != self.port:
                raise ValueError('Selected port differs from the authorized helper target')
            protocol = request.get('protocol')
            if type(protocol) is not int or protocol not in (0, 1) or request.get('native', False):
                raise ValueError('This helper supports structured TCP selection only')
            self.session = Session(address or self.address, port or self.port, 'both')
            try:
                self.session.start()
            except Exception:
                self.stop()
                raise
            self.values = (0, 0)
        if not self.session:
            raise ValueError('Start before applying delay')
        old = self.values
        try:
            for pipe, value in zip(self.session.pipes, values):
                self.session.runner('/usr/sbin/dnctl', 'pipe', str(pipe), 'config', 'delay', f'{value}ms')
        except RuntimeError:
            try:
                for pipe, value in zip(self.session.pipes, old):
                    self.session.runner('/usr/sbin/dnctl', 'pipe', str(pipe), 'config', 'delay', f'{value}ms')
            except RuntimeError:
                self.stop()
            if command == 'start':
                self.stop()
            raise
        self.values = tuple(values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', type=ipaddress.ip_address, help='Optional restriction to one numeric remote IP')
    parser.add_argument('--port', type=int, default=0, help='Optional remote port restriction; 0 means any')
    parser.add_argument('--background', action='store_true', help='Detach after socket is ready; logs go to /var/run')
    args = parser.parse_args()
    integer(args.port, 0, 65535)
    address = str(args.server) if args.server else ""
    uid = int(os.environ.get('SUDO_UID', '0'))
    if sys.platform != 'darwin' or os.geteuid() != 0 or uid <= 0:
        parser.error('Launch with sudo from your normal user account')
    if args.background:
        from macos_lag_daemon import background
        background(f'/var/run/clumsier-lag-{uid}.log',
                   lambda ready: serve(address, args.port, uid, ready=ready, reconnect=True))
    else:
        serve(address, args.port, uid)


def serve(address, port, uid, ready=lambda: None, socket_path=None, accept_timeout=60, idle_timeout=10, reconnect=False):
    path = socket_path or f'/var/run/clumsier-lag-{uid}.sock'
    broker = Broker(address, port)
    server = socket.socket(socket.AF_UNIX)
    owned = False
    os.umask(0o077)
    def interrupted(*_):
        raise KeyboardInterrupt
    for sig in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, interrupted)
    try:
        # /var/run is root-owned: no unlinking unknown or stale endpoints.
        try:
            server.bind(path)
        except OSError as error:
            if error.errno == errno.EADDRINUSE:
                raise RuntimeError('A helper socket already exists. Open Clumsier directly if the helper is running; otherwise inspect the recovery log before removing a stale socket') from error
            raise
        owned = True
        server.listen(1)
        os.chmod(path, stat.S_IRUSR | stat.S_IWUSR)
        os.chown(path, uid, -1)
        print(f'Clumsier helper ready: TCP {address or "any IP"}:{port or "any port"}. Open the Qt app within {accept_timeout} seconds.', flush=True)
        print('GUI disconnect/10s silence clears capture; background mode allows reopening for 60 seconds.', flush=True)
        server.settimeout(accept_timeout)
        ready()
        while True:
            try:
                connection, _ = server.accept()
            except TimeoutError:
                break
            try:
                with connection, contextlib.redirect_stdout(sys.stderr):
                    connection.settimeout(idle_timeout)
                    pending = b''
                    while True:
                        block = connection.recv(4096)
                        if not block:
                            break
                        pending += block
                        if len(pending) > 4096:
                            raise ValueError('Oversized request')
                        while b'\n' in pending:
                            line, pending = pending.split(b'\n', 1)
                            try:
                                request = json.loads(line)
                                if not isinstance(request, dict):
                                    raise ValueError('Expected command object')
                                broker.command(request)
                                reply = dict(ok=True, running=broker.session is not None, error='')
                            except (ValueError, RuntimeError) as error:
                                reply = dict(ok=False, running=broker.session is not None, error=str(error))
                            connection.sendall(json.dumps(reply).encode() + b'\n')
            except (TimeoutError, ConnectionError):
                # Lost/idle client: remove all conditioning before accepting any
                # new controller. A cleanup error escapes and fails closed.
                pass
            with contextlib.redirect_stdout(sys.stderr):
                broker.stop()
            if not reconnect:
                break
            print('Capture cleared; waiting for GUI reconnection.', flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        server.close()
        try:
            with contextlib.redirect_stdout(sys.stderr):
                broker.stop()
        finally:
            if owned:
                os.unlink(path)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError) as error:
        print(f'Clumsier: {error}', file=sys.stderr)
        sys.exit(1)
