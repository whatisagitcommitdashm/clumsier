#!/bin/sh
# Wrap the audited staged beta as a self-contained .app ZIP. No installation.
set -eu
cd "$(dirname "$0")/.."
: "${SIGN_IDENTITY:?Set SIGN_IDENTITY}"
WORK=build/macos-app-download
ROOT="$WORK/root"
APP="$ROOT/Applications/Clumsier.app"
STAGED=build/macos-local-package/root
rm -rf "$WORK"
mkdir -p "$ROOT/Applications" "$WORK/helper-root/Library/Application Support/Clumsier"
ditto "$STAGED/Applications/Clumsier.app" "$APP"
ditto "$STAGED/Library/Application Support/Clumsier/Helper" "$WORK/helper-root/Library/Application Support/Clumsier/Helper"
pkgbuild --root "$WORK/helper-root" --identifier org.clumsier.local.networking --version 0.1.0 --scripts packaging/macos/networking-scripts --ownership recommended "$APP/Contents/Resources/Clumsier-Networking.pkg"
cp resources/macos/Clumsier.icns "$APP/Contents/Resources/Clumsier.icns"
ditto resources/macos "$APP/Contents/Resources/AxeArtwork"
chmod u+w "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleIconFile Clumsier.icns' "$APP/Contents/Info.plist" || /usr/libexec/PlistBuddy -c 'Add :CFBundleIconFile string Clumsier.icns' "$APP/Contents/Info.plist"
python3 tools/sign_macos_payload.py "$ROOT" "$SIGN_IDENTITY"
mkdir -p dist
ditto -c -k --sequesterRsrc --keepParent "$APP" dist/Clumsier-macOS-arm64-beta.2.zip
shasum -a 256 dist/Clumsier-macOS-arm64-beta.2.zip > dist/Clumsier-macOS-arm64-beta.2.zip.sha256
