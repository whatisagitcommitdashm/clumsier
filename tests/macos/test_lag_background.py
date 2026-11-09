"""Unprivileged subprocess tests. Never run pfctl/dnctl or change system files."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from macos_lag_daemon import background, open_log

TOOLS = str(Path(__file__).resolve().parents[2] / 'tools')


class Tests(unittest.TestCase):
    def test_existing_socket_is_preserved(self):
        from macos_lag_helper import serve
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / 'socket')
            with socket.socket(socket.AF_UNIX) as listener:
                listener.bind(path)
                listener.listen(1)
                with patch('macos_lag_helper.signal.signal'):
                    with self.assertRaisesRegex(RuntimeError, 'socket already exists'):
                        serve('127.0.0.1', 25565, os.getuid(), socket_path=path)
                self.assertTrue(Path(path).exists())
                # The rejected launch must leave the original listener usable.
                with socket.socket(socket.AF_UNIX) as client:
                    client.connect(path)
                    connection, _ = listener.accept()
                    connection.close()

    def test_log_rejects_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / 'target'
            target.write_text('untouched')
            link = Path(directory) / 'log'
            link.symlink_to(target)
            with self.assertRaises(OSError):
                open_log(str(link))
            self.assertEqual(target.read_text(), 'untouched')

    def test_log_rejects_world_readable(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'log'
            path.write_text('')
            path.chmod(0o644)
            with self.assertRaises(RuntimeError):
                open_log(str(path))

    def test_child_failure_is_not_successful_startup(self):
        with tempfile.TemporaryDirectory() as directory:
            # Use a disposable log without requiring root; no daemon is left.
            path = str(Path(directory) / 'log')
            with patch('macos_lag_daemon.open_log', side_effect=lambda _: os.open(path, os.O_WRONLY | os.O_CREAT, 0o600)):
                def fail(_):
                    raise RuntimeError('startup failed')
                with self.assertRaisesRegex(RuntimeError, 'Helper startup failed: startup failed'):
                    background(path, fail)
            self.assertIn('startup failed', Path(path).read_text())

    def run_server(self, mode):
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / 'socket')
            cleaned = str(Path(directory) / 'cleaned')
            script = '''
import sys
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, sys.argv[1])
import macos_lag_helper as h
class FakeBroker:
    session = None
    def __init__(self, *args): pass
    def command(self, request): pass
    def stop(self): Path(sys.argv[3]).write_text('cleaned')
h.Broker = FakeBroker
with patch.object(h.os, 'chown'):
    try:
        h.serve('127.0.0.1', 25565, 1000, socket_path=sys.argv[2], accept_timeout=0.3, idle_timeout=0.1, reconnect=sys.argv[4] == 'reconnect')
    except TimeoutError:
        pass
'''
            process = subprocess.Popen([sys.executable, '-B', '-c', script, TOOLS, path, cleaned, mode], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                self.assertIn('helper ready', process.stdout.readline())
                self.assertIn('silence clears capture', process.stdout.readline())
                if mode != 'no-client':
                    with socket.socket(socket.AF_UNIX) as client:
                        client.settimeout(2)
                        client.connect(path)
                        if mode == 'idle':
                            import time
                            time.sleep(0.25)
                            self.assertEqual(client.recv(4096), b'')
                        elif mode == 'malformed':
                            client.sendall(b'[]\n')
                            response = json.loads(client.recv(4096))
                            self.assertFalse(response['ok'])
                        else:
                            client.sendall(b'{"command":"status"}\n')
                            response = json.loads(client.recv(4096))
                            self.assertTrue(response['ok'])
                if mode == 'reconnect':
                    # Readiness for another client follows cleanup, never precedes it.
                    self.assertIn('Capture cleared', process.stdout.readline())
                    self.assertTrue(Path(cleaned).exists())
                    with socket.socket(socket.AF_UNIX) as client:
                        client.settimeout(2)
                        client.connect(path)
                        client.sendall(b'{"command":"status"}\n')
                        response = json.loads(client.recv(4096))
                        self.assertTrue(response['ok'])
                        self.assertFalse(response['running'])
                stdout, stderr = process.communicate(timeout=3)
                self.assertEqual(process.returncode, 0, stderr)
                self.assertTrue(Path(cleaned).exists())
                self.assertFalse(Path(path).exists())
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()

    def test_reconnect_only_after_capture_cleanup(self):
        self.run_server('reconnect')

    def test_eof_cleans_and_removes_socket(self):
        self.run_server('eof')

    def test_no_client_timeout_cleans_and_removes_socket(self):
        self.run_server('no-client')

    def test_idle_client_cleans_and_removes_socket(self):
        self.run_server('idle')

    def test_bad_request_is_rejected_then_eof_cleans(self):
        self.run_server('malformed')


if __name__ == '__main__':
    unittest.main()
