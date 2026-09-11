"""Protocol and pipe-loss regression checks, run inside the network lab."""
import json
from pathlib import Path
import select
import subprocess as sp
import time

ROOT = Path(__file__).resolve().parents[2]

def check(value, message):
    if not value:
        raise AssertionError(message)


def main():
    helper = sp.Popen([str(ROOT / 'build/qt-linux/clumsier-linux-helper')],
                      stdin=sp.PIPE, stdout=sp.PIPE, stderr=sp.PIPE, text=True)
    ping = None
    try:
        def request(command):
            helper.stdin.write(json.dumps(command) + '\n')
            helper.stdin.flush()
            check(select.select([helper.stdout], [], [], 12)[0], 'Helper reply timed out')
            return json.loads(helper.stdout.readline())
        preset = (ROOT / 'examples/four-leaps.json').read_text()
        # Zero-delay metadata preset for capture selection; lag is independent.
        p = json.loads(preset)
        p['steps'] = [{'name': 'lowest', 'note': '', 'type': 'lowest'}]
        start = request({'command': 'start', 'preset': json.dumps(p), 'lag': [True, True, True, 5000, 0]})
        check(start['ok'] and start['running'], start)
        for lag in ([True, True, True, -1, 0], [True, True, True, 0.5, 0], [1, True, True, 0, 0]):
            reply = request({'command': 'apply', 'lag': lag})
            check(not reply['ok'] and reply['running'], 'Invalid protocol request changed capture')
        ping = sp.Popen(['ping', '-U', '-n', '-c', '1', '-W', '7', '192.0.2.2'], stdout=sp.PIPE, stderr=sp.STDOUT)
        time.sleep(0.3)
        check(ping.poll() is None, 'Invalid apply did not preserve five-second delay')
        helper.stdin.close()
        helper.wait(timeout=12)
        ping.communicate(timeout=2)
        check(helper.returncode == 0 and ping.returncode == 0, 'Pipe EOF did not release held traffic')
        print('PASS helper rejects malformed settings and releases held traffic on GUI pipe loss', flush=True)
    finally:
        if helper.poll() is None:
            helper.terminate()
            helper.wait(timeout=12)
        if ping is not None and ping.poll() is None:
            ping.terminate()
            ping.wait(timeout=5)

if __name__ == '__main__':
    main()
