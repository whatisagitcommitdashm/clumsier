"""Package audit tests do not install, authorize or execute privileged code."""
from pathlib import Path
import plistlib
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from audit_macos_package import audit

class Tests(unittest.TestCase):
    def fixture(self, root):
        app = Path(root) / 'Applications/Clumsier.app/Contents'
        (app / 'Resources').mkdir(parents=True)
        (app / 'Info.plist').write_bytes(plistlib.dumps({}))
        binary = app / 'Clumsier'
        binary.write_bytes(b'\xcf\xfa\xed\xfeexample')
        return app, binary

    def test_actual_floor_and_system_dependencies(self):
        with tempfile.TemporaryDirectory() as root:
            app, binary = self.fixture(root)
            def output(args, **kwargs):
                if args[1] == '-L':
                    return f'{binary}:\n\t/System/Library/Frameworks/AppKit.framework/AppKit (compatibility version 1.0)\n\t@rpath/Python (compatibility version 1.0)\n'
                return 'cmd LC_BUILD_VERSION\n minos 15.0\n'
            with patch('audit_macos_package.subprocess.check_output', side_effect=output):
                report = audit(root)
            self.assertEqual(report['minimum_macos'], '15.0.0')
            self.assertFalse(report['clean_machine_tested'])
            self.assertEqual(plistlib.loads((app / 'Info.plist').read_bytes())['LSMinimumSystemVersion'], '15.0.0')

    def test_external_homebrew_dependency_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            self.fixture(root)
            with patch('audit_macos_package.subprocess.check_output', return_value='binary:\n\t/opt/homebrew/lib/example.dylib (compatibility version 1)\n'):
                with self.assertRaisesRegex(RuntimeError, 'External dependency'):
                    audit(root)

    def test_external_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            app, _ = self.fixture(root)
            (app / 'escape').symlink_to('/usr/bin/python3')
            with patch('audit_macos_package.subprocess.check_output', return_value='binary:\n'):
                with self.assertRaisesRegex(RuntimeError, 'symlink'):
                    audit(root)

    def test_intel_audits_only_requested_slice_and_sets_monterey_floor(self):
        with tempfile.TemporaryDirectory() as root:
            app, binary = self.fixture(root)
            def output(args, **kwargs):
                if args[0].endswith('lipo'):
                    return 'x86_64 arm64'
                self.assertEqual(args[1:3], ['-arch', 'x86_64'])
                return 'binary:\n' if '-L' in args else 'cmd LC_BUILD_VERSION\n minos 11.0\n'
            with patch('audit_macos_package.subprocess.check_output', side_effect=output):
                report = audit(root, 'x86_64', '12.0', '12.0')
            self.assertEqual(report['minimum_macos'], '12.0.0')
            self.assertEqual(report['architecture'], 'x86_64')

    def test_intel_rejects_arm_only_payload(self):
        with tempfile.TemporaryDirectory() as root:
            self.fixture(root)
            with patch('audit_macos_package.subprocess.check_output', return_value='arm64'):
                with self.assertRaisesRegex(RuntimeError, 'Missing x86_64'):
                    audit(root, 'x86_64', '12.0', '12.0')

    def test_intel_rejects_newer_macos_dependency(self):
        with tempfile.TemporaryDirectory() as root:
            self.fixture(root)
            def output(args, **kwargs):
                if args[0].endswith('lipo'): return 'x86_64'
                return 'binary:\n' if '-L' in args else 'cmd LC_BUILD_VERSION\n minos 13.0\n'
            with patch('audit_macos_package.subprocess.check_output', side_effect=output):
                with self.assertRaisesRegex(RuntimeError, 'above requested'):
                    audit(root, 'x86_64', '12.0', '12.0')
