#!/usr/bin/env python3
"""Root-only local broker. Launch explicitly in Terminal; never run Qt as root."""
import argparse
import contextlib
import ipaddress
import json
import os
import signal
import socket
import stat
import sys
from macos_lag import Session, integer


class Broker:
    def __init__(self, address, port):
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
            if address and str(ipaddress.ip_address(address)) != self.address:
                raise ValueError('Selected IP differs from the authorized helper target')
            if request.get('port', 0) not in (0, self.port):
                raise ValueError('Selected port differs from the authorized helper target')
            if request.get('protocol') not in (0, 1) or request.get('native', False):
                raise ValueError('This helper supports structured TCP selection only')
            self.session = Session(self.address, self.port, 'both')
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
    parser.add_argument('--server', required=True, type=ipaddress.ip_address)
    parser.add_argument('--port', type=int, default=25565)
    args = parser.parse_args()
    integer(args.port, 1, 65535)
    uid = int(os.environ.get('SUDO_UID', '0'))
    if sys.platform != 'darwin' or os.geteuid() != 0 or uid <= 0:
        parser.error('Launch with sudo from your normal user account')
    path = f'/var/run/clumsier-lag-{uid}.sock'
    broker = Broker(str(args.server), args.port)
    server = socket.socket(socket.AF_UNIX)
    owned = False
    os.umask(0o077)
    def interrupted(*_):
        raise KeyboardInterrupt
    for sig in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, interrupted)
    try:
        # /var/run is root-owned: no unlinking unknown or stale endpoints.
        server.bind(path)
        owned = True
        server.listen(1)
        os.chmod(path, stat.S_IRUSR | stat.S_IWUSR)
        os.chown(path, uid, -1)
        print(f'Clumsier helper ready: TCP {args.server}:{args.port}. Open the Qt app now.', flush=True)
        print('One GUI connection per launch. Ctrl-C stops; GUI disconnect/10s silence triggers cleanup.', flush=True)
        connection, _ = server.accept()
        with connection, contextlib.redirect_stdout(sys.stderr):
            connection.settimeout(10)
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
    main()
