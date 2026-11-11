#!/bin/sh
# Repackage the released app ZIP, preserving its signatures and exact contents.
# Creates a disk image only: never installs or executes the app/helper.
set -eu
cd "$(dirname "$0")/.."
ZIP=${1:-dist/Clumsier-macOS-arm64-beta.2.zip}
OUTPUT=${2:-dist/Clumsier-macOS-arm64-beta.2.dmg}
DMG_ARCH=${CLUMSIER_DMG_ARCH:-arm64}
DMG_MINIMUM=${CLUMSIER_DMG_MINIMUM:-15.7}
DMG_BETA=${CLUMSIER_DMG_BETA:-2}
case "$DMG_ARCH" in arm64) CHIP='Apple Silicon';; x86_64) CHIP='Intel';; *) echo 'Unsupported architecture' >&2; exit 1;; esac
test -f "$ZIP"
command -v hdiutil >/dev/null
WORK=$(mktemp -d "${TMPDIR:-/tmp}/clumsier-dmg.XXXXXX")
MOUNT=""
cleanup() {
    if [ -n "$MOUNT" ]; then hdiutil detach "$MOUNT" >/dev/null || true; fi
    rm -rf "$WORK"
}
trap cleanup EXIT HUP INT TERM
mkdir "$WORK/payload"
ditto -x -k "$ZIP" "$WORK/payload"
test -d "$WORK/payload/Clumsier.app"
codesign --verify --deep --strict "$WORK/payload/Clumsier.app"
ln -s /Applications "$WORK/payload/Applications"
cat > "$WORK/payload/Install Clumsier.txt" <<GUIDE
Clumsier for Mac — beta $DMG_BETA
$CHIP · macOS $DMG_MINIMUM or later

1. Quit any old Clumsier and wait 70 seconds for helper cleanup.
2. Drag Clumsier.app onto Applications. Open it from Applications.
3. If macOS blocks it, click Done. In System Settings > Privacy & Security,
   find Clumsier and choose Open Anyway only if you trust this download.
4. In Clumsier, choose Install networking components. Finish Installer,
   then return and choose Authorize networking. The installer may also need
   its own Open Anyway approval. Authorization alone does not enable delay.
5. Hotkeys are optional. Grant Input Monitoring, reopen if asked, and test
   recording a keyboard key before playing.

Clumsier can delay all TCP traffic, not just Minecraft. UDP is unaffected.
No notarization: this disk image does not remove Apple's approval checks.
Do not disable Gatekeeper or SIP. Stop if macOS reports detected malware.

Setup guide: https://clumsier.vercel.app
Source and release notes: https://github.com/Maximooch/clumsier
GUIDE
mkdir -p "$(dirname "$OUTPUT")" "$WORK/payload/.background"
mv "$WORK/payload/Install Clumsier.txt" "$WORK/payload/.background/Install Clumsier.txt"
cp packaging/macos/dmg/background.png "$WORK/payload/.background/background.png"
# Author Finder layout on a temporary writable image; never touch an existing
# mounted Clumsier image or install the app. Finder may ask for automation access.
VOLUME=${CLUMSIER_DMG_VOLUME:-Install Clumsier}
if [ -e "/Volumes/$VOLUME" ]; then
    echo "Eject the existing $VOLUME disk image before rebuilding." >&2
    exit 1
fi
hdiutil create -volname "$VOLUME" -srcfolder "$WORK/payload" -format UDRW "$WORK/layout.dmg"
hdiutil attach -nobrowse -readwrite -plist "$WORK/layout.dmg" > "$WORK/mount.plist"
MOUNT=$(python3 - "$WORK/mount.plist" <<'PYMOUNT'
import plistlib, sys
with open(sys.argv[1], 'rb') as f: data = plistlib.load(f)
print(next(x['mount-point'] for x in data['system-entities'] if 'mount-point' in x))
PYMOUNT
)
osascript packaging/macos/dmg/layout.applescript "$VOLUME"
# Allow Finder to finish persisting the window metadata before detach.
sync
hdiutil detach "$MOUNT"
MOUNT=""
hdiutil convert "$WORK/layout.dmg" -format UDZO -ov -o "$OUTPUT"
# Final artifact stays read-only and preserves the signed app.

hdiutil verify "$OUTPUT"
shasum -a 256 "$OUTPUT" > "$OUTPUT.sha256"
echo "Created $OUTPUT (not notarized)."
