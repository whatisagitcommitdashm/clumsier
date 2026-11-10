#!/bin/sh
# Repackage the released app ZIP, preserving its signatures and exact contents.
# Creates a disk image only: never installs or executes the app/helper.
set -eu
cd "$(dirname "$0")/.."
ZIP=${1:-dist/Clumsier-macOS-arm64-beta.2.zip}
OUTPUT=${2:-dist/Clumsier-macOS-arm64-beta.2.dmg}
test -f "$ZIP"
command -v hdiutil >/dev/null
WORK=$(mktemp -d "${TMPDIR:-/tmp}/clumsier-dmg.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
mkdir "$WORK/payload"
ditto -x -k "$ZIP" "$WORK/payload"
test -d "$WORK/payload/Clumsier.app"
codesign --verify --deep --strict "$WORK/payload/Clumsier.app"
ln -s /Applications "$WORK/payload/Applications"
cat > "$WORK/payload/Install Clumsier.txt" <<'GUIDE'
Clumsier for Mac — beta 2
Apple Silicon · macOS 15.7 or later

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
mkdir -p "$(dirname "$OUTPUT")"
hdiutil create -volname 'Clumsier' -srcfolder "$WORK/payload" -format UDZO -ov "$OUTPUT"
hdiutil verify "$OUTPUT"
shasum -a 256 "$OUTPUT" > "$OUTPUT.sha256"
echo "Created $OUTPUT (not notarized)."
