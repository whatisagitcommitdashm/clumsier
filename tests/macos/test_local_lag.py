"""Unprivileged lifecycle tests; these do not establish kernel packet behavior."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('local_lag', Path(__file__).resolve().parents[2] / 'tools/macos_lag.py')
lag = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lag)


class Fake:
    def __init__(self):
        self.calls = []
        self.fail_load = False
        self.fail_detach = False

    def __call__(self, *args, text=None):
        self.calls.append((args, text))
        if args == (lag.PF, '-s', 'dummynet'):
            return 'dummynet-anchor "com.apple/*" all\n'
        if args == (lag.DN, 'list'):
            return '20001: existing pipe\n'
        if args == (lag.PF, '-E'):
            return 'pf enabled\nToken : 12345\n'
        if '-f' in args and '-n' not in args:
            if text and self.fail_load:
                raise RuntimeError('load failed')
            if text == '' and self.fail_detach:
                raise RuntimeError('detach failed')
        return ''


class Tests(unittest.TestCase):
    def test_precise_rules(self):
        text = lag.rules('15.204.237.8', 25565, 'both', [1, 2])
        self.assertIn('in quick inet proto tcp from 15.204.237.8 port 25565 to any pipe 1', text)
        self.assertIn('out quick inet proto tcp from any to 15.204.237.8 port 25565 pipe 2', text)
        self.assertEqual(len(text.splitlines()), 2)
        self.assertIn('inet6', lag.rules('::1', 12345, 'out', [1, 2]))
        with self.assertRaises(ValueError):
            lag.rules('evil; flush', 1, 'out', [1, 2])

    def test_any_tcp_rules_cover_both_families(self):
        text = lag.rules('', 0, 'both', [1, 2])
        self.assertEqual(len(text.splitlines()), 4)
        for family in ('inet', 'inet6'):
            self.assertIn(f'in quick {family} proto tcp from any to any pipe 1', text)
            self.assertIn(f'out quick {family} proto tcp from any to any pipe 2', text)
        self.assertNotIn('port ', text)
        text = lag.rules('', 25565, 'out', [1, 2])
        self.assertEqual(len(text.splitlines()), 2)
        self.assertIn('to any port 25565 pipe 2', text)
        self.assertNotIn('dummynet in', text)
        for invalid in (-1, 65536, True, '22; flush'):
            with self.assertRaises(ValueError):
                lag.rules('', invalid, 'out', [1, 2])

    @patch.object(lag.time, 'sleep')
    def test_scoped_lifecycle(self, _sleep):
        fake = Fake()
        session = lag.Session('15.204.237.8', 25565, 'out', fake)
        session.start()
        self.assertNotIn(20001, session.pipes)
        session.delay(120)
        self.assertEqual(session.close(), [])
        self.assertIsNone(session.token)
        self.assertEqual(session.pipes, [])
        for args, text in fake.calls:
            self.assertNotIn('-F', args)
            self.assertNotIn('-d', args)
            if '-f' in args:
                self.assertIn(session.anchor, args)
        self.assertEqual(session.close(), [])

    @patch.object(lag.time, 'sleep')
    def test_partial_start_cleans_resources(self, _sleep):
        fake = Fake()
        fake.fail_load = True
        session = lag.Session('15.204.237.8', 25565, 'out', fake)
        with self.assertRaises(RuntimeError):
            session.start()
        self.assertEqual(session.close(), [])
        self.assertEqual(session.pipes, [])
        self.assertFalse(session.loaded)

    @patch.object(lag.time, 'sleep')
    def test_detach_failure_does_not_delete_referenced_pipes(self, _sleep):
        fake = Fake()
        session = lag.Session('15.204.237.8', 25565, 'out', fake)
        session.start()
        fake.fail_detach = True
        self.assertTrue(session.close())
        self.assertEqual(len(session.pipes), 2)
        self.assertEqual(session.token, '12345')
        self.assertFalse(any('delete' in args for args, _ in fake.calls))

    def run_main(self, startup_error=None, cleanup_errors=None):
        with patch.object(lag.sys, 'argv', ['macos_lag.py', '--server', '127.0.0.1']), \
             patch.object(lag.sys, 'platform', 'darwin'), \
             patch.object(lag.os, 'geteuid', return_value=0), \
             patch.object(lag.sys.stdin, 'isatty', return_value=True), \
             patch.object(lag.signal, 'signal'), \
             patch.object(lag, 'Session') as session, \
             patch.object(lag.select, 'select', side_effect=KeyboardInterrupt):
            session.return_value.start.side_effect = startup_error
            session.return_value.close.return_value = cleanup_errors or []
            result = lag.main()
            session.return_value.close.assert_called_once()
            return result

    def test_exit_status_after_cleanup(self):
        self.assertEqual(self.run_main(), 0)
        self.assertEqual(self.run_main(cleanup_errors=['cleanup failed']), 1)
        self.assertEqual(self.run_main(startup_error=RuntimeError('startup failed')), 1)

    def test_cleanup_does_not_suppress_unexpected_exception(self):
        with self.assertRaisesRegex(ValueError, 'unexpected'):
            self.run_main(startup_error=ValueError('unexpected'), cleanup_errors=['cleanup failed'])

    def test_missing_hook_refuses_global_edits(self):
        calls = []
        def runner(*args, **kwargs):
            calls.append(args)
            return ''
        session = lag.Session('15.204.237.8', 25565, 'out', runner)
        with self.assertRaises(RuntimeError):
            session.start()
        self.assertEqual(calls, [(lag.PF, '-s', 'dummynet')])


if __name__ == '__main__':
    unittest.main()
