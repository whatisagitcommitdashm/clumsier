#!/bin/sh
# Explicit administrator operation. Never flush PF or delete other tools' pipes.
set -eu
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH
test "$(id -u)" = 0 || { echo 'Administrator authorization is required.' >&2; exit 1; }
if ps -axo comm= | grep -E '(/Clumsier$|/clumsier-lag-helper$|/macos_lag_helper.py$)' >/dev/null; then
    echo 'Quit Clumsier and wait 70 seconds for helper cleanup, then retry uninstall.' >&2
    exit 1
fi
# A socket/log may represent unresolved cleanup; preserve recovery evidence.
for socket in /var/run/clumsier-lag-*.sock; do
    if [ -e "$socket" ] || [ -L "$socket" ]; then
        echo "Unresolved helper endpoint: $socket. Inspect the corresponding recovery log before uninstalling." >&2
        exit 1
    fi
done
for directory in '/Library/Application Support' '/Library/Application Support/Clumsier' '/Library/Application Support/Clumsier/Helper'; do
    if [ -e "$directory" ] || [ -L "$directory" ]; then
        [ ! -L "$directory" ] && [ -d "$directory" ] || { echo 'Unsafe helper path.' >&2; exit 1; }
        test "$(stat -f %u "$directory")" = 0 || { echo 'Helper must be root-owned.' >&2; exit 1; }
        mode=$(stat -f %Lp "$directory")
        test "$((0$mode & 0022))" = 0 || { echo 'Helper path is writable.' >&2; exit 1; }
    fi
done
APP=/Applications/Clumsier.app
if [ -e "$APP" ]; then
    ID=$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$APP/Contents/Info.plist")
    test "$ID" = org.clumsier.preview || { echo 'Refusing to remove an unrelated app.' >&2; exit 1; }
    rm -rf -- "$APP"
fi
rm -rf -- '/Library/Application Support/Clumsier/Helper'
/usr/sbin/pkgutil --forget org.clumsier.local.beta >/dev/null 2>&1 || true
echo 'Clumsier removed. User presets, privacy consent and root-only recovery logs were retained. No network rules were flushed.'
