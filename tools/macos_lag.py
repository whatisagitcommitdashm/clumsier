#!/usr/bin/env python3
"""Experimental, session-scoped TCP delay using macOS PF/dummynet."""
import argparse
import ipaddress
import os
import re
import select
import secrets
import signal
import subprocess
import sys
import time

PF = '/sbin/pfctl'
DN = '/usr/sbin/dnctl'


def run(*args, text=None):
    result = subprocess.run(args, input=text, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f'{args[0]} failed: {result.stderr.strip() or result.stdout.strip()}')
    return result.stdout + result.stderr


def rules(address, port, direction, pipes):
    address = str(ipaddress.ip_address(address)) if address else ''
    if type(port) is not int or not 0 <= port <= 65535:
        raise ValueError('Invalid remote port')
    if direction not in ('in', 'out', 'both'):
        raise ValueError('Invalid direction')
    families = [('inet6' if ipaddress.ip_address(address).version == 6 else 'inet')] if address else ['inet', 'inet6']
    remote = address or 'any'
    if port:
        remote += f' port {port}'
    lines = []
    for family in families:
        if direction in ('in', 'both'):
            lines.append(f'dummynet in quick {family} proto tcp from {remote} to any pipe {pipes[0]}')
        if direction in ('out', 'both'):
            lines.append(f'dummynet out quick {family} proto tcp from any to {remote} pipe {pipes[1]}')
    return '\n'.join(lines) + '\n'


class Session:
    def __init__(self, address, port, direction, runner=run):
        self.runner = runner
        self.address, self.port, self.direction = address, port, direction
        self.anchor = 'com.apple/clumsier-local-' + secrets.token_hex(8)
        self.pipes = []
        self.token = None
        self.loaded = False

    def start(self):
        # Do not reload /etc/pf.conf or add a new top-level hook implicitly.
        hooks = self.runner(PF, '-s', 'dummynet')
        if not re.search(r'dummynet-anchor\s+"com\.apple/\*"', hooks):
            raise RuntimeError('Active PF rules lack dummynet-anchor "com.apple/*". Refusing to replace global rules.')
        existing = self.runner(DN, 'list')
        occupied = {int(n) for n in re.findall(r'^\s*(\d+):', existing, re.M)}
        # Random, checked IDs; no other tool should allocate these during startup.
        candidates = [n for n in range(20000, 60000) if n not in occupied]
        if len(candidates) < 2:
            raise RuntimeError('No unused dummynet pipe IDs available.')
        for _ in range(2):
            pipe = secrets.choice(candidates)
            candidates.remove(pipe)
            self.pipes.append(pipe)
            self.runner(DN, 'pipe', str(pipe), 'config', 'delay', '0ms')
        configuration = rules(self.address, self.port, self.direction, self.pipes)
        self.runner(PF, '-n', '-a', self.anchor, '-f', '-', text=configuration)
        # Mark before loading so partial failure also attempts scoped cleanup.
        print(f'Owned anchor: {self.anchor}; pipes: {self.pipes}', flush=True)
        print(f'Emergency detach: sudo {PF} -a {self.anchor} -f /dev/null', flush=True)
        self.loaded = True
        self.runner(PF, '-a', self.anchor, '-f', '-', text=configuration)
        output = self.runner(PF, '-E')
        match = re.search(r'Token\s*:\s*(\d+)', output)
        if not match:
            raise RuntimeError('PF enabled but its reference token was not returned; inspect pfctl -s References.')
        self.token = match[1]
        print('Recovery commands (only if this process cannot clean up):', flush=True)
        print(f"  sudo {PF} -a {self.anchor} -f /dev/null", flush=True)
        for pipe in self.pipes:
            print(f'  sudo {DN} pipe {pipe} delete', flush=True)
        print(f'  sudo {PF} -X {self.token}', flush=True)

    def delay(self, milliseconds):
        # Per-direction delay, not a target ping or a total RTT.
        for pipe in self.pipes:
            self.runner(DN, 'pipe', str(pipe), 'config', 'delay', f'{milliseconds}ms')
        print(f'Added delay: {milliseconds} ms per selected direction', flush=True)

    def close(self):
        errors = []
        if self.loaded:
            try:
                self.runner(PF, '-a', self.anchor, '-f', '-', text='')
                self.loaded = False
            except RuntimeError as error:
                # Leave pipes intact if detaching failed; report recovery clearly.
                return [str(error), 'Anchor detachment failed; run the printed recovery commands.']
        for pipe in self.pipes[:]:
            try:
                self.runner(DN, 'pipe', str(pipe), 'config', 'delay', '0ms')
                # Best effort drain before deleting; kernel behavior remains experimental.
                time.sleep(0.02)
                self.runner(DN, 'pipe', str(pipe), 'delete')
                self.pipes.remove(pipe)
            except RuntimeError as error:
                errors.append(str(error))
        if self.token:
            try:
                self.runner(PF, '-X', self.token)
                self.token = None
            except RuntimeError as error:
                errors.append(str(error))
        return errors


def integer(value, low, high):
    number = int(value)
    if not low <= number <= high:
        raise ValueError(f'Expected {low}..{high}')
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', required=True, help='Numeric server IP (not hostname)')
    parser.add_argument('--port', type=int, default=25565)
    parser.add_argument('--direction', choices=('in', 'out', 'both'), default='out')
    parser.add_argument('--dry-run', action='store_true', help='Print example rules without privileges or changes')
    args = parser.parse_args()
    address = str(ipaddress.ip_address(args.server))
    integer(args.port, 1, 65535)
    if args.dry_run:
        print(rules(address, args.port, args.direction, [20001, 20002]), end='')
        return 0
    if sys.platform != 'darwin' or os.geteuid() != 0:
        parser.error('Run on macOS with sudo; --dry-run needs no privileges.')
    if not sys.stdin.isatty():
        parser.error('An interactive terminal is required (EOF always cleans up).')
    session = Session(address, args.port, args.direction)
    def interrupted(_signum, _frame):
        raise KeyboardInterrupt
    for name in ('SIGTERM', 'SIGHUP'):
        signal.signal(getattr(signal, name), interrupted)
    status = 0
    try:
        session.start()
        print(f'TCP {address}:{args.port}, {args.direction}; starts at zero delay.')
        print('Commands: delay MS | off | burst MS SECONDS [COUNTDOWN] | status | quit')
        print('Burst returns to zero. Countdown defaults to 5 seconds to refocus Minecraft.')
        print('Not integrated with Qt. No global hotkeys yet. Connect/reconnect after startup.')
        deadline = None
        while True:
            if deadline is not None and time.monotonic() >= deadline:
                session.delay(0)
                deadline = None
            readable, _, _ = select.select([sys.stdin], [], [], 0.1)
            if not readable:
                continue
            line = sys.stdin.readline()
            if not line:
                break
            words = line.split()
            if not words:
                continue
            try:
                if words == ['quit']:
                    break
                if words == ['off']:
                    session.delay(0)
                    deadline = None
                elif len(words) == 2 and words[0] == 'delay':
                    session.delay(integer(words[1], 0, 1000))
                    deadline = None
                elif len(words) in (3, 4) and words[0] == 'burst':
                    ms = integer(words[1], 0, 1000)
                    duration = integer(words[2], 1, 60)
                    countdown = integer(words[3], 0, 30) if len(words) == 4 else 5
                    # Never leave an earlier burst active during the countdown.
                    session.delay(0)
                    for remaining in range(countdown, 0, -1):
                        print(f'Burst in {remaining}...', flush=True)
                        time.sleep(1)
                    session.delay(ms)
                    deadline = time.monotonic() + duration
                elif words == ['status']:
                    print(session.runner(DN, 'list'))
                else:
                    print('Use: delay MS | off | burst MS SECONDS [COUNTDOWN] | status | quit')
            except ValueError as error:
                print(f'Invalid command: {error}', file=sys.stderr)
    except KeyboardInterrupt:
        print('\nStopping...')
    except RuntimeError as error:
        print(error, file=sys.stderr)
        status = 1
    finally:
        errors = session.close()
        for error in errors:
            print(f'CLEANUP FAILED: {error}', file=sys.stderr)
        if errors:
            status = 1
    return status


if __name__ == '__main__':
    sys.exit(main())
