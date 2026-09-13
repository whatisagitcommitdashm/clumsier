"""Real NFQUEUE acceptance checks; launched only through test-linux-network.sh."""
import argparse
import json
import os
from pathlib import Path
import re
import select
import signal
import socket
import statistics
import subprocess as sp
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / 'build/linux/clumsier'
ENV = dict(os.environ, LC_ALL='C')
PINGS = []


def run(*args):
    return sp.check_output(args, text=True, stderr=sp.STDOUT, env=ENV, timeout=10)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


class App:
    def __init__(self, *args):
        self.p = sp.Popen([str(BINARY), *map(str, args)], stdin=sp.PIPE,
                          stdout=sp.PIPE, stderr=sp.STDOUT, env=ENV)
        self.pending = b''
        try:
            self.prompt()
        except BaseException:
            self.p.terminate()
            self.p.wait(timeout=12)
            raise

    def prompt(self):
        deadline = time.monotonic() + 12
        while b'> ' not in self.pending:
            remaining = deadline - time.monotonic()
            check(remaining > 0, f'Console timeout: {self.pending!r}')
            check(select.select([self.p.stdout], [], [], max(0, remaining))[0],
                  f'Console timeout: {self.pending!r}')
            data = os.read(self.p.stdout.fileno(), 65536)
            check(data, f'Console exited: {self.pending.decode()}')
            self.pending += data
        output, self.pending = self.pending.split(b'> ', 1)
        return output.decode()

    def command(self, text, expect=None):
        self.p.stdin.write((text + '\n').encode())
        self.p.stdin.flush()
        output = self.prompt()
        if expect:
            check(expect in output, f'{text}: expected {expect!r}, got {output}')
        return output

    def close(self, how='quit'):
        if self.p.poll() is None:
            if how == 'quit':
                self.p.stdin.write(b'quit\n')
                self.p.stdin.flush()
            elif how == 'eof':
                self.p.stdin.close()
            else:
                self.p.send_signal(how)
            self.p.wait(timeout=12)
        output = self.p.stdout.read().decode()
        check(self.p.returncode == (1 if how in (signal.SIGINT, signal.SIGTERM) else 0),
              f'Exit {self.p.returncode}: {output}')


def ping_process(address='192.0.2.2', count=3):
    # -U measures delivery to userspace, including time held after the kernel
    # receive timestamp. Default ping can hide inbound NFQUEUE delay entirely.
    process = sp.Popen(['ping', '-U', '-n', '-c', str(count), '-i', '0.1', '-W', '7', address],
                       stdout=sp.PIPE, stderr=sp.STDOUT, text=True, env=ENV)
    PINGS.append(process)
    return process


def ping_result(p):
    output = p.communicate(timeout=10)[0]
    check(p.returncode == 0, output)
    totals = re.search(r'(\d+) packets transmitted, (\d+) received', output)
    check(totals and totals[1] == totals[2], f'Packet loss: {output}')
    values = [float(x) for x in re.findall(r'time[=<]([0-9.]+) ms', output)]
    check(values, output)
    return statistics.median(values)


def measure(label, expected, address='192.0.2.2'):
    value = ping_result(ping_process(address))
    if not max(0, expected - 35) <= value <= expected + 150:
        print(run('nft', 'list', 'ruleset'), flush=True)
    check(max(0, expected - 35) <= value <= expected + 150,
          f'{label}: expected ~{expected} ms, measured {value} ms')
    print(f'PASS {label}: {value:.2f} ms (expected ~{expected} ms)', flush=True)
    return value


def tables():
    return run('nft', 'list', 'tables')


def echo_rtt(protocol, port, address):
    family = socket.AF_INET6 if ':' in address else socket.AF_INET
    kind = socket.SOCK_DGRAM if protocol == 'udp' else socket.SOCK_STREAM
    with socket.socket(family, kind) as s:
        s.settimeout(4)
        s.connect((address, port))
        start = time.monotonic()
        s.sendall(b'clumsier')
        check(s.recv(100) == b'clumsier', 'Echo payload changed')
        return (time.monotonic() - start) * 1000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--demo', action='store_true', help='Show sequence RTTs and exit')
    parser.add_argument('--qt', action='store_true', help='Test real Qt controls and helper in the isolated lab')
    parser.add_argument('--qt-demo', action='store_true', help='Open the GUI against isolated traffic; close it to clean up')
    options = parser.parse_args()
    # Reject direct execution on a host, including as root.
    check(os.environ.get('CLUMSIER_PARENT_NETNS') and
          os.readlink('/proc/self/ns/net') != os.environ['CLUMSIER_PARENT_NETNS'],
          'Use scripts/test-linux-network.sh; refusing to change the host network')
    check(not run('ip', '-o', 'link', 'show').strip().splitlines()[1:],
          'Expected a fresh network namespace containing only loopback')
    apps = []
    server = None
    echo = None
    with tempfile.TemporaryDirectory(prefix='clumsier-network-') as temp:
        try:
            # The child retains a second network namespace until cleanup.
            server = sp.Popen(['unshare', '--net', 'sleep', '300'])
            deadline = time.monotonic() + 5
            while os.readlink(f'/proc/{server.pid}/ns/net') == os.readlink('/proc/self/ns/net'):
                check(time.monotonic() < deadline, 'Server namespace startup timeout')
                time.sleep(0.01)
            peer = ['nsenter', '-t', str(server.pid), '-n']
            run('ip', 'link', 'add', 'c0', 'type', 'veth', 'peer', 'name', 's0', 'netns', str(server.pid))
            run('ip', 'addr', 'add', '192.0.2.1/24', 'dev', 'c0')
            for address in ('192.0.2.2/24', '192.0.2.3/24', '2001:db8:1::2/64'):
                run(*peer, 'ip', 'addr', 'add', address, 'dev', 's0')
            run('ip', 'addr', 'add', '2001:db8:1::1/64', 'dev', 'c0')
            for prefix, interface in (([], 'lo'), ([], 'c0'), (peer, 'lo'), (peer, 's0')):
                run(*prefix, 'ip', 'link', 'set', interface, 'up')
            time.sleep(2)  # IPv6 duplicate-address detection.
            measure('baseline', 0)
            check(not tables(), 'Lab should start without firewall tables')
            # Keep an unrelated table to verify ownership-preserving cleanup.
            run('nft', 'add', 'table', 'inet', 'test_sentinel')
            original = tables()
            if options.qt_demo:
                gui_env = dict(ENV, LC_ALL='C.UTF-8', XDG_DATA_HOME=temp + '/data', XDG_CONFIG_HOME=temp + '/config')
                for key in ('QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH', 'QT_QPA_PLATFORMTHEME', 'QT_STYLE_OVERRIDE'):
                    gui_env.pop(key, None)
                print('Qt lab: set a delay in Quick controls and press Start. Watch the RTT below.\n'
                      'Stop restores baseline. Close the GUI to remove the lab. Settings here are temporary.', flush=True)
                monitor = sp.Popen(['ping', '-U', '-n', '-i', '1', '192.0.2.2'], env=ENV)
                PINGS.append(monitor)
                sp.run([str(ROOT / 'build/qt-linux/clumsier-beta'), '--direct-helper'], env=gui_env, check=True)
                check(tables() == original, 'Closing the Qt demo left private tables')
                return
            if options.qt:
                qt_env = dict(ENV, CLUMSIER_QT_NETWORK_LAB='1', QT_QPA_PLATFORM='offscreen',
                              QT_QUICK_BACKEND='software', LC_ALL='C.UTF-8')
                for key in ('QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH', 'QT_QPA_PLATFORMTHEME', 'QT_STYLE_OVERRIDE'):
                    qt_env.pop(key, None)
                sp.run([str(ROOT / 'build/qt-linux/qt-network-test')], env=qt_env, check=True, timeout=60)
                sp.run([sys.executable, str(ROOT / 'tests/linux/qt_helper.py')], env=qt_env, check=True, timeout=30)
                check(tables() == original, 'Qt integration left private tables')
                print('PASS Qt integration cleanup preserves unrelated tables', flush=True)
                return

            def app(*args):
                result = App(*args)
                apps.append(result)
                return result

            a = app('--preset', ROOT / 'examples/four-leaps.json', '--baseline', '50')
            check(tables() == original, 'Loading unexpectedly installed rules')
            a.command('start', 'Running')
            for i, delay in enumerate((150, 0, 100, 50)):
                if i:
                    a.command('next', 'Running')
                measure(f'Four leaps step {i + 1}', delay)
                if options.demo:
                    time.sleep(1)
            a.command('previous', 'Step 3/4')
            a.command('reset', 'Step 1/4')
            a.command('stop', 'Stopped')
            measure('after Stop', 0)
            check(tables() == original, 'Stop left private tables')
            if options.demo:
                a.close()
                print('Demo complete: real packets crossed an isolated virtual cable.', flush=True)
                return
            for _ in range(20):
                a.command('start', 'Running')
                a.command('start', 'Running')
                a.command('stop', 'Stopped')
                a.command('stop', 'Stopped')
                check(tables() == original, 'Repeated lifecycle leaked a table')
            print('PASS 20 repeated Start/Stop cycles and unrelated table preservation', flush=True)
            a.command('start', 'Running')
            for action in ('delay 0 0', 'stop'):
                a.command('delay 5000 0', 'Running')
                p = ping_process(count=1)
                time.sleep(0.35)
                check(p.poll() is None, 'Packet was not held')
                start = time.monotonic()
                a.command(action)
                value = ping_result(p)
                check(time.monotonic() - start < 1, 'Held packet was not released promptly')
                print(f'PASS held-packet release via {action}: RTT {value:.1f} ms', flush=True)
            a.command('start', 'Running')
            a.command('delay 1200 0')
            p = ping_process(count=1)
            time.sleep(0.5)
            check(p.poll() is None, 'Packet was not held before increase')
            a.command('delay 2200 0')
            value = ping_result(p)
            check(2100 <= value <= 2450, f'Live increase changed original deadline: {value}')
            print(f'PASS live increase preserves receipt time: {value:.1f} ms', flush=True)
            a.command('delay 100 0')
            a.command('delay -1 0', 'Use: delay')
            measure('invalid edit preserves delay', 100)
            a.close()
            check(tables() == original, 'Quit left tables')

            # Separate inbound/outbound selection plus IPv4/IPv6 address exclusion.
            template = json.loads((ROOT / 'examples/four-leaps.json').read_text())
            for direction in ('inbound', 'outbound', 'both'):
                for address in ('192.0.2.2', '2001:db8:1::2'):
                    template['traffic'].update(direction=direction, remote_address=address)
                    template['policy'] = 'outbound' if direction == 'outbound' else 'inbound'
                    path = Path(temp) / 'selection.json'
                    path.write_text(json.dumps(template))
                    b = app('--preset', path, '--baseline', '50')
                    b.command('delay ' + {'inbound': '100 0', 'outbound': '0 150', 'both': '100 150'}[direction])
                    b.command('start', 'Running')
                    measure(f'{direction} {address}', {'inbound': 100, 'outbound': 150, 'both': 250}[direction], address)
                    measure('unselected address', 0, '192.0.2.3')
                    b.close()
                    check(tables() == original, 'Selection test leaked table')
            echo = sp.Popen([*peer, sys.executable, str(ROOT / 'tests/linux/echo.py')],
                            stdout=sp.PIPE, stderr=sp.STDOUT)
            check(select.select([echo.stdout], [], [], 5)[0] and
                  echo.stdout.readline() == b'ready\n', 'Echo server failed to start')
            template['policy'] = 'inbound'
            for address in ('192.0.2.2', '2001:db8:1::2'):
                for protocol in ('tcp', 'udp'):
                    template['traffic'].update(direction='inbound', remote_address=address,
                                               protocol=protocol, remote_port=19001)
                    path = Path(temp) / 'port.json'
                    path.write_text(json.dumps(template))
                    b = app('--preset', path, '--baseline', '50')
                    b.command('delay 100 0')
                    b.command('start', 'Running')
                    for actual_protocol, port, expected in (
                            (protocol, 19001, 100), (protocol, 19002, 0),
                            ('tcp' if protocol == 'udp' else 'udp', 19001, 0)):
                        value = echo_rtt(actual_protocol, port, address)
                        check(max(0, expected - 30) <= value <= expected + 100,
                              f'{address} {protocol} filter: {actual_protocol}:{port} RTT {value}')
                    print(f'PASS {address} {protocol} remote port: selected echo ~100 ms; other port/protocol unaffected', flush=True)
                    b.close()
                    check(tables() == original, 'Port test leaked tables')
            for how in ('eof', signal.SIGINT, signal.SIGTERM):
                b = app()
                b.command('start', 'Running')
                b.command('delay 5000 0')
                p = ping_process(count=1)
                time.sleep(0.3)
                b.close(how)
                check(ping_result(p) < 1300, 'Exit did not release held packet')
                check(tables() == original, f'{how} left tables')
            print('PASS EOF, SIGINT and SIGTERM release traffic and remove rules', flush=True)
            b = app()
            b.command('start', 'Running')
            b.command('delay 5000 0')
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as flood:
                flood.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
                flood.settimeout(0.2)
                flood.connect(('192.0.2.2', 19001))
                for i in range(1400):
                    flood.send(str(i).encode())
                    time.sleep(0.001)
                received = set()
                while True:
                    try:
                        received.add(flood.recv(100))
                    except socket.timeout:
                        break
                check(received, 'Queue overload did not pass any traffic before five-second delay')
                early = len(received)
                b.command('delay 0 0', 'Running')
                while True:
                    try:
                        received.add(flood.recv(100))
                    except socket.timeout:
                        break
                check(len(received) > early, 'Held traffic did not drain after overload')
                print(f'PASS bounded overload: {early} early echoes, {len(received)}/1400 total; loss is permitted under overload', flush=True)
            measure('after overload recovery', 0)
            b.close()
            check(tables() == original, 'Overload cleanup leaked tables')
            b, c = app(), app()
            b.command('start', 'Running')
            c.command('start', 'Running')
            check(tables().count('clumsier_') == 2, 'Concurrent sessions did not create separate tables')
            b.close()
            check(tables().count('clumsier_') == 1, 'Stopping one session affected the other')
            c.command('delay 100 0')
            measure('surviving concurrent session', 100)
            c.close()
            check(tables() == original, 'Concurrent sessions leaked tables')
            denied = sp.run(['setpriv', '--bounding-set=-net_admin', str(BINARY)],
                            input='start\nquit\n', text=True, stdout=sp.PIPE,
                            stderr=sp.STDOUT, timeout=12, env=ENV)
            check(denied.returncode != 0 and 'Could not open NFQUEUE' in denied.stdout,
                  f'Missing capability was not reported: {denied.stdout}')
            check(tables() == original, 'Unprivileged Start left tables')
            print('PASS missing CAP_NET_ADMIN fails without installing rules', flush=True)
            template['traffic'].update(protocol='any', remote_port=0, remote_address='not-an-ip')
            path = Path(temp) / 'invalid.json'
            path.write_text(json.dumps(template))
            b = app('--preset', path, '--baseline', '50')
            b.command('start', 'numeric IPv4 or IPv6')
            b.command('status', 'Stopped')
            check(tables() == original, 'Invalid target installed rules')
            b.close()
            template['traffic'].update(direction='both', remote_address='', native_backend='windivert', native_filter='true')
            path.write_text(json.dumps(template))
            b = app('--preset', path, '--baseline', '50')
            b.command('start', 'does not support native filter')
            b.command('status', 'Stopped')
            check(tables() == original, 'Unsupported native filter installed rules')
            b.close()
            print('PASS invalid addresses and Windows native filters are rejected', flush=True)
            b = app()
            b.command('start', 'Running')
            b.command('delay 5000 0')
            p = ping_process(count=1)
            time.sleep(0.3)
            stale = set(re.findall(r'table inet (clumsier_[a-f0-9]+)', tables()))
            b.p.kill()
            b.p.wait(timeout=5)
            p.communicate(timeout=10)  # Held packet loss is allowed on SIGKILL.
            measure('new traffic after SIGKILL (bypass)', 0)
            check(len(stale) == 1, 'Expected exactly one owned crash table')
            for name in stale:
                run('nft', 'delete', 'table', 'inet', name)
            check(tables() == original, 'Crash cleanup affected other tables')
            print('PASS crash bypass and exact-table cleanup', flush=True)
            print('PASS Linux real-packet acceptance checks', flush=True)
        finally:
            for p in PINGS:
                if p.poll() is None:
                    p.terminate()
                    p.wait(timeout=5)
            for a in apps:
                if a.p.poll() is None:
                    a.p.terminate()
                    try:
                        a.p.wait(timeout=12)
                    except sp.TimeoutExpired:
                        a.p.kill()
                        a.p.wait()
            if echo is not None:
                echo.terminate()
                echo.wait(timeout=5)
            if server is not None:
                server.terminate()
                server.wait(timeout=5)
            # These unnamed namespaces vanish when their last process exits.


if __name__ == '__main__':
    main()
