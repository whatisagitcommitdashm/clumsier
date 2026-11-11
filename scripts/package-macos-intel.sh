#!/bin/sh
# Experimental Intel package. No system installation or network activation.
set -eu
BETA=${CLUMSIER_RELEASE_BETA:-4}
case "$BETA" in ""|*[!0-9]*) echo "Invalid release beta number" >&2; exit 1;; esac
cd "$(dirname "$0")/.."
: "${SIGN_IDENTITY:?Select the signing identity}"
: "${CLUMSIER_QT_ROOT:?Select a Qt 6.8 SDK containing x86_64}"
: "${PYINSTALLER_PYTHON:?Select a Python containing an x86_64 slice and PyInstaller}"
# python.org installations may not have their optional certificate setup run.
# Use macOS's trusted CA bundle, never disable TLS certificate verification.
SSL_CERT_FILE=${SSL_CERT_FILE:-/etc/ssl/cert.pem}
export SSL_CERT_FILE
WORK=build/macos-intel-package
ROOT="$WORK/root"
APP="$ROOT/Applications/Clumsier.app"
QT_ROOT=$(cd "$CLUMSIER_QT_ROOT" && pwd)
PYTHON=$(cd "$(dirname "$PYINSTALLER_PYTHON")" && pwd)/$(basename "$PYINSTALLER_PYTHON")
# Run the interpreter in Intel mode: PyInstaller must not freeze an ARM runtime.
arch -x86_64 "$PYTHON" -c 'import platform; assert platform.machine() == "x86_64"'
cmake -S src/ui/qt -B build/qt-macos-x86_64 -DCMAKE_PREFIX_PATH="$QT_ROOT" \
    -DCMAKE_OSX_SYSROOT="${CLUMSIER_MACOS_SDK:-/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk}" -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    -DCMAKE_BUILD_TYPE=Release -DCLUMSIER_LOCAL_SIGN_IDENTITY=
cmake --build build/qt-macos-x86_64 --parallel "${CLUMSIER_BUILD_JOBS:-4}"
mkdir -p "$WORK"
rm -rf "$ROOT"
mkdir -p "$ROOT/Applications" "$WORK/helper-root/Library/Application Support/Clumsier"
ditto build/qt-macos-x86_64/Clumsier.app "$APP"
"$QT_ROOT/bin/macdeployqt" "$APP" "-qmldir=$PWD/src/ui/qt" -always-overwrite "-codesign=$SIGN_IDENTITY"
# Qt deploys optional database drivers even though Clumsier uses JSON files.
# Remove unused SQL drivers rather than ship their external Homebrew dependencies.
rm -rf "$APP/Contents/PlugIns/sqldrivers"
arch -x86_64 "$PYTHON" -m PyInstaller --noconfirm --clean --onedir --noupx \
    --target-architecture x86_64 --name clumsier-lag-helper \
    --distpath "$WORK/frozen" --workpath "$WORK/pyi" --specpath "$WORK" \
    --paths "$PWD/tools" --codesign-identity "$SIGN_IDENTITY" tools/macos_lag_helper.py
HELPER="$WORK/helper-root/Library/Application Support/Clumsier/Helper"
ditto "$WORK/frozen/clumsier-lag-helper" "$HELPER"
cp packaging/macos/uninstall.sh "$HELPER/uninstall.sh"
cp LICENSE "$APP/Contents/Resources/LICENSE.txt"
cp docs/MACOS-INSTALL.md "$APP/Contents/Resources/START-HERE.txt"
ditto resources/macos "$APP/Contents/Resources/AxeArtwork"
printf '%s\n' 'PF/dummynet Intel testing beta; no Network Extension' > "$APP/Contents/Resources/ClumsierLocalPackage"
# Include the helper during auditing: all privileged runtime dependencies count.
ditto "$WORK/helper-root/Library" "$ROOT/Library"
CLUMSIER_QT_VERSION=6.8.3 CLUMSIER_QT_ROOT="$QT_ROOT" \
    arch -x86_64 "$PYTHON" tools/collect_macos_notices.py "$ROOT"
arch -x86_64 "$PYTHON" tools/audit_macos_package.py "$ROOT" \
    --architecture x86_64 --minimum-macos 12.0 --maximum-macos 12.0
chmod -R go-w "$ROOT" "$WORK/helper-root"
arch -x86_64 "$PYTHON" tools/sign_macos_payload.py "$ROOT" "$SIGN_IDENTITY"
# Package the signed/audited helper, not the pre-signing copy.
rm -rf "$HELPER"
ditto "$ROOT/Library/Application Support/Clumsier/Helper" "$HELPER"
pkgbuild --root "$WORK/helper-root" --identifier org.clumsier.local.networking \
    --version 0.1.0 --scripts packaging/macos/networking-scripts --ownership recommended \
    "$APP/Contents/Resources/Clumsier-Networking.pkg"
codesign --force --sign "$SIGN_IDENTITY" --timestamp=none "$APP"
codesign --verify --deep --strict "$APP"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString 0.1.0" "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $BETA" "$APP/Contents/Info.plist"
codesign --force --sign "$SIGN_IDENTITY" --timestamp=none "$APP"
codesign --verify --deep --strict "$APP"
mkdir -p dist
ditto -c -k --sequesterRsrc --keepParent "$APP" dist/Clumsier-macOS-x86_64-beta.$BETA.zip
shasum -a 256 dist/Clumsier-macOS-x86_64-beta.$BETA.zip > dist/Clumsier-macOS-x86_64-beta.$BETA.zip.sha256
