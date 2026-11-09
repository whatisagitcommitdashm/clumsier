#!/bin/sh
# Sign a local Qt development bundle; does not install or reset permissions.
set -eu
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo 'Usage: sh scripts/sign-macos-local.sh SIGNING_IDENTITY [APP_PATH]' >&2
    exit 1
fi
IDENTITY=$1
APP=${2:-build/qt-macos/Clumsier.app}
[ "$IDENTITY" != '-' ] || { echo 'Use a certificate identity, not ad-hoc signing.' >&2; exit 1; }
test -d "$APP/Contents" || { echo "App bundle not found: $APP" >&2; exit 1; }
APP=$(cd "$APP" && pwd -P)
# Do not replace signatures beneath an executing process.
if ps -axo command= | grep -F -x "$APP/Contents/MacOS/Clumsier" >/dev/null; then
    echo 'Quit Clumsier before signing it, then rerun this command.' >&2
    exit 1
fi
/usr/bin/codesign --force --sign "$IDENTITY" --timestamp=none "$APP"
/usr/bin/codesign --verify --strict --verbose=2 "$APP"
/usr/bin/codesign --display --requirements - "$APP"
echo 'Local app signed. Approve this app in Input Monitoring once, then retain this signing identity for subsequent builds.'
echo 'This does not sign or authorize a Network Extension, notarize the app, or change privacy settings.'
