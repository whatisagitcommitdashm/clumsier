"""Static guard checks complement (not replace) privileged install acceptance."""
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[2]

class Tests(unittest.TestCase):
    def test_installer_refuses_live_or_unsafe_install(self):
        script = (ROOT / 'packaging/macos/scripts/preinstall').read_text()
        for guard in ('clumsier-lag-helper', 'clumsier-lag-*.sock', 'root-owned', 'symlink'):
            self.assertIn(guard, script)
        self.assertNotIn('\nkill ', script)

    def test_authorization_uses_fixed_root_path_and_clean_environment(self):
        source = (ROOT / 'src/platform/macos/helper_setup.cpp').read_text()
        self.assertIn('/usr/bin/env -i PATH=/usr/bin:/bin:/usr/sbin:/sbin HOME=/var/root', source)
        self.assertIn('--uid %1 --background', source)
        self.assertIn('with administrator privileges', source)
        self.assertIn('info.st_uid == 0', source)
        self.assertIn('S_IWGRP | S_IWOTH', source)
        self.assertIn('QFileInfo(parent).isSymLink()', source)

    def test_uninstall_never_globally_flushes(self):
        script = (ROOT / 'packaging/macos/uninstall.sh').read_text()
        for command in ('pfctl -F', 'pfctl -d', 'dnctl flush', 'killall', 'pkill'):
            self.assertNotIn(command, script)
        self.assertIn('org.clumsier.preview', script)
        self.assertIn('clumsier-lag-*.sock', script)

    def test_non_relocatable_root_owned_payload(self):
        script = (ROOT / 'scripts/package-macos-local.sh').read_text()
        self.assertIn("item['BundleIsRelocatable'] = False", script)
        self.assertIn('--ownership recommended', script)
        self.assertIn('--onedir', script)
        self.assertNotIn('--onefile', script)

    def test_app_download_installs_runtime_without_running_writable_code(self):
        source = (ROOT / 'src/platform/macos/helper_setup.cpp').read_text()
        self.assertIn('QDesktopServices::openUrl(QUrl::fromLocalFile(path))', source)
        self.assertIn('Resources/Clumsier-Networking.pkg', source)
        script = (ROOT / 'scripts/package-macos-app.sh').read_text()
        self.assertIn('packaging/macos/networking-scripts', script)
        self.assertIn('tools/sign_macos_payload.py', script)
        helper_guard = (ROOT / 'packaging/macos/networking-scripts/preinstall').read_text()
        self.assertNotIn('/Clumsier$|', helper_guard)
        self.assertIn('clumsier-lag-*.sock', helper_guard)
        self.assertIn('root-owned', helper_guard)
