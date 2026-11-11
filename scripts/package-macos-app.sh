#!/bin/sh
# Wrap the audited staged beta as a self-contained .app ZIP. No installation.
set -eu
BETA=${CLUMSIER_RELEASE_BETA:-4}
case "$BETA" in ""|*[!0-9]*) echo "Invalid release beta number" >&2; exit 1;; esac
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
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString 0.1.0" "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $BETA" "$APP/Contents/Info.plist"
codesign --force --sign "$SIGN_IDENTITY" --timestamp=none "$APP"
codesign --verify --deep --strict "$APP"
mkdir -p dist
ditto -c -k --sequesterRsrc --keepParent "$APP" dist/Clumsier-macOS-arm64-beta.$BETA.zip
shasum -a 256 dist/Clumsier-macOS-arm64-beta.$BETA.zip > dist/Clumsier-macOS-arm64-beta.$BETA.zip.sha256
