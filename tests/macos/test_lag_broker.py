import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch, MagicMock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from macos_lag_helper import Broker


class Tests(unittest.TestCase):
    def setUp(self):
        self.patch = patch('macos_lag_helper.Session')
        self.factory = self.patch.start()
        self.addCleanup(self.patch.stop)
        self.session = self.factory.return_value
        self.session.pipes = [20100, 20101]
        self.session.close.return_value = []
        self.broker = Broker('15.204.237.8', 25565)

    def start(self, **changes):
        request = dict(command='start', address='', port=0, protocol=0, native=False, delay=[100, 150])
        request.update(changes)
        self.broker.command(request)

    def test_start_apply_stop(self):
        self.start()
        self.assertEqual(self.broker.values, (100, 150))
        self.broker.command(dict(command='apply', delay=[0, 300]))
        self.assertEqual(self.broker.values, (0, 300))
        self.broker.command(dict(command='stop'))
        self.assertIsNone(self.broker.session)
        self.session.close.assert_called_once()

    def test_reject_before_changing_network(self):
        for change in (dict(address='127.0.0.1'), dict(port=22), dict(protocol=2),
                       dict(native=True), dict(delay=[True, 10]), dict(delay=[1, 1001])):
            with self.assertRaises(ValueError):
                self.start(**change)
        self.factory.assert_not_called()

    def test_unrestricted_default_and_advanced_filters(self):
        self.broker = Broker()
        for changes, expected in (({}, ('', 0)),
                ({'address': '::1'}, ('::1', 0)),
                ({'port': 25565}, ('', 25565)),
                ({'address': '15.204.237.8', 'port': 25565}, ('15.204.237.8', 25565))):
            self.start(**changes)
            self.factory.assert_called_with(*expected, 'both')
            self.broker.stop()

    def test_unrestricted_request_validation_precedes_session(self):
        self.broker = Broker()
        for changes in ({'address': 'evil; flush'}, {'address': None},
                {'port': True}, {'port': '22'}, {'port': -1}, {'port': 65536},
                {'protocol': True}, {'protocol': 2}, {'native': True}):
            with self.assertRaises(ValueError):
                self.start(**changes)
        self.factory.assert_not_called()

    def test_partial_apply_rolls_back(self):
        self.start()
        self.session.runner.side_effect = [None, RuntimeError('failed'), None, None]
        with self.assertRaises(RuntimeError):
            self.broker.command(dict(command='apply', delay=[200, 300]))
        self.assertEqual(self.broker.values, (100, 150))
        self.assertIsNotNone(self.broker.session)

    def test_failed_rollback_stops(self):
        self.start()
        self.session.runner.side_effect = RuntimeError('failed')
        with self.assertRaises(RuntimeError):
            self.broker.command(dict(command='apply', delay=[200, 300]))
        self.assertIsNone(self.broker.session)

    def test_cleanup_failure_remains_visible(self):
        self.start()
        self.session.close.return_value = ['detach failed']
        with self.assertRaisesRegex(RuntimeError, 'detach failed'):
            self.broker.stop()
        self.assertIsNotNone(self.broker.session)


if __name__ == '__main__':
    unittest.main()
