#!/bin/sh
# Self-contained PF/dummynet beta; never installs/authorizes/activates anything.
set -eu
cd "$(dirname "$0")/.."
: "${SIGN_IDENTITY:?Set SIGN_IDENTITY to your certificate fingerprint or name}"
: "${PYINSTALLER_PYTHON:?Set PYINSTALLER_PYTHON to a Python with PyInstaller 6.22.3 installed}"
QT_ROOT=${CLUMSIER_QT_ROOT:-/opt/homebrew}
DEPLOYQT="$QT_ROOT/bin/macdeployqt"
test -x "$DEPLOYQT"
VERSION=$(cat VERSION | tr -d '\r\n')
WORK=build/macos-local-package
ROOT="$WORK/root"
APP="$ROOT/Applications/Clumsier.app"
HELPER="$ROOT/Library/Application Support/Clumsier/Helper"
# Separate build tree: never replace/sign the player's running development app.
cmake -S src/ui/qt -B "$WORK/qt" -DCMAKE_PREFIX_PATH="$QT_ROOT" \
    -DCMAKE_BUILD_TYPE=Release -DCLUMSIER_LOCAL_SIGN_IDENTITY= -DBUILD_TESTING=OFF
cmake --build "$WORK/qt" --parallel "${CLUMSIER_BUILD_JOBS:-4}"
rm -rf "$ROOT"
mkdir -p "$ROOT/Applications" "$(dirname "$HELPER")"
ditto "$WORK/qt/Clumsier.app" "$APP"
"$DEPLOYQT" "$APP" "-libpath=$QT_ROOT/opt/qtsvg/lib" "-qmldir=$PWD/src/ui/qt" -always-overwrite "-codesign=$SIGN_IDENTITY"
# onedir avoids extracting privileged Python code into a writable temp folder.
"$PYINSTALLER_PYTHON" -m PyInstaller --noconfirm --clean --onedir --noupx \
    --name clumsier-lag-helper --distpath "$WORK/frozen" --workpath "$WORK/pyi" \
    --specpath "$WORK" --paths "$PWD/tools" --codesign-identity "$SIGN_IDENTITY" \
    tools/macos_lag_helper.py
ditto "$WORK/frozen/clumsier-lag-helper" "$HELPER"
cp packaging/macos/uninstall.sh "$HELPER/uninstall.sh"
cp LICENSE "$APP/Contents/Resources/LICENSE.txt"
cp docs/MACOS-INSTALL.md "$APP/Contents/Resources/START-HERE.txt"
printf '%s\n' 'PF/dummynet local beta; no Network Extension' > "$APP/Contents/Resources/ClumsierLocalPackage"
osacompile -o "$ROOT/Applications/Uninstall Clumsier.app" packaging/macos/Uninstall-Clumsier.applescript
# Metadata must describe the maximum actual shipped binary floor. Packaging
# records the maximum OS floor of every shipped binary.
chmod u+w "$APP/Contents/Info.plist"
"$PYINSTALLER_PYTHON" tools/collect_macos_notices.py "$ROOT"
"$PYINSTALLER_PYTHON" tools/audit_macos_package.py "$ROOT"
chmod -R go-w "$ROOT"
"$PYINSTALLER_PYTHON" tools/sign_macos_payload.py "$ROOT" "$SIGN_IDENTITY"
# Disable Installer relocation to an unrelated build-tree copy of Clumsier.
pkgbuild --analyze --root "$ROOT" "$WORK/components.plist"
"$PYINSTALLER_PYTHON" - "$WORK/components.plist" <<'PY'
import plistlib, sys
p = sys.argv[1]
with open(p, 'rb') as f: items = plistlib.load(f)
for item in items:
    item['BundleIsRelocatable'] = False
    item['BundleHasStrictIdentifier'] = True
    item['BundleOverwriteAction'] = 'upgrade'
with open(p, 'wb') as f: plistlib.dump(items, f)
PY
mkdir -p dist
OUTPUT="dist/Clumsier-$VERSION-macos-$(uname -m)-beta.pkg"
pkgbuild --root "$ROOT" --component-plist "$WORK/components.plist" \
    --identifier org.clumsier.local.beta --version "${VERSION%%-*}" \
    --scripts packaging/macos/scripts --ownership recommended "$OUTPUT"
shasum -a 256 "$OUTPUT" > "$OUTPUT.sha256"
echo "Built $OUTPUT. NOT installed, NOT notarized. See docs/MACOS-INSTALL.md."
