"""Collect reproducible license notices for a local beta; no release approval implied."""
from pathlib import Path
import importlib.metadata
import shutil
import sys
import os
import platform
import urllib.request

root = Path(sys.argv[1])
destination = root / 'Applications/Clumsier.app/Contents/Resources/ThirdParty'
destination.mkdir(parents=True, exist_ok=True)
qt_version = os.environ.get('CLUMSIER_QT_VERSION', '6.11.2')
python_version = platform.python_version()
# Match the selected runtime and Qt SDK, not the build host's Homebrew version.
sources = {
    'Qt-LGPL-3.0.txt': f'https://raw.githubusercontent.com/qt/qtbase/v{qt_version}/LICENSES/LGPL-3.0-only.txt',
    'Qt-GPL-3.0.txt': f'https://raw.githubusercontent.com/qt/qtbase/v{qt_version}/LICENSES/GPL-3.0-only.txt',
    'Qt-GPL-2.0.txt': f'https://raw.githubusercontent.com/qt/qtbase/v{qt_version}/LICENSES/GPL-2.0-only.txt',
    'Python-LICENSE.txt': f'https://raw.githubusercontent.com/python/cpython/v{python_version}/LICENSE',
}
for name, url in sources.items():
    with urllib.request.urlopen(url, timeout=60) as response:
        (destination / name).write_bytes(response.read())
for package in ('pyinstaller', 'altgraph', 'macholib', 'packaging', 'setuptools', 'pyinstaller-hooks-contrib'):
    distribution = importlib.metadata.distribution(package)
    for path in distribution.files or []:
        if 'license' in str(path).lower() or path.name == 'COPYING.txt':
            original = Path(distribution.locate_file(path))
            if original.is_file():
                shutil.copyfile(original, destination / (package + '-' + path.name))
qt_root = os.environ.get('CLUMSIER_QT_ROOT')
if qt_root and (Path(qt_root) / 'LICENSES').is_dir():
    shutil.copytree(Path(qt_root) / 'LICENSES', destination / 'Qt-LICENSES', dirs_exist_ok=True)
shutil.copyfile('external/cjson/LICENSE', destination / 'cJSON-LICENSE.txt')
# SPDX inventories contain exact versions/licenses/source locations for bundled
# Homebrew Qt and its transitive libraries. Copy installed SBOMs, not assumptions.
for cellar in ([] if os.environ.get('CLUMSIER_QT_ROOT') else Path('/opt/homebrew/Cellar').glob('*/*/sbom.spdx.json')):
    shutil.copyfile(cellar, destination / (cellar.parent.parent.name + '-' + cellar.parent.name + '-sbom.spdx.json'))
(destination / 'README.txt').write_text(f'''Clumsier beta third-party components
Qt is dynamically linked. Source: https://code.qt.io/qt/ ; build/replacement
instructions: https://doc.qt.io/qt-6/macos-deployment.html . No prohibition on
reverse engineering for LGPL library debugging is imposed by this package.
Qt {qt_version} sources: https://download.qt.io/archive/qt/{".".join(qt_version.split(".")[:2])}/{qt_version}/single/
Python {python_version}: https://www.python.org/downloads/release/python-{"".join(python_version.split("."))}/
PyInstaller 6.22.3: https://github.com/pyinstaller/pyinstaller/tree/v6.22.3
PyInstaller's bootloader distribution exception is included in its COPYING.
Additional dependency inventories are provided as SPDX files. Public release
requires a final artifact-specific notices/source-compliance review; this local
package is not approval to publish an incomplete set of license notices.
''')
print('Collected third-party notices; public-release compliance review remains required.')
