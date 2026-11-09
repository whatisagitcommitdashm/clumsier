#!/bin/sh
# Explicit development launcher; never runs the GUI as root.
set -eu
cd "$(dirname "$0")/.."
test "$(uname -s)" = Darwin || { echo 'macOS required.' >&2; exit 1; }
if [ "$(id -u)" = 0 ]; then echo 'Run as your normal user, not with sudo.' >&2; exit 1; fi
if [ "$#" -gt 2 ]; then
    echo 'Usage: sh scripts/start-macos-local.sh [NUMERIC_SERVER_IP [PORT]]' >&2
    exit 1
fi
APP="$PWD/build/qt-macos/Clumsier.app"
test -x "$APP/Contents/MacOS/Clumsier" || { echo 'Build the Qt app first.' >&2; exit 1; }
SOCKET="/var/run/clumsier-lag-$(id -u).sock"
if [ -e "$SOCKET" ] || [ -L "$SOCKET" ]; then
    echo 'An existing helper endpoint was found; reopening Clumsier without starting another helper.'
    echo 'The existing helper keeps its ORIGINAL authorized scope, not the arguments of this launch.'
    echo 'If Start fails, the helper may have expired; run this launcher again after cleanup.'
    echo "Recovery log: /var/run/clumsier-lag-$(id -u).log"
    open "$APP"
    exit 0
fi
if [ "$#" -eq 0 ]; then
    echo 'Scope: all TCP traffic (IPv4/IPv6), not only Minecraft. Stop restores normal traffic.'
    set -- --background
else
    set -- --server "$1" --port "${2:-25565}" --background
fi
PYTHON=$(command -v python3)
# -I excludes PYTHONPATH, the current directory and user site packages. The
# trusted development checkout is explicitly added by the authorized bootstrap.
sudo "$PYTHON" -I -B -c 'import runpy,sys; p=sys.argv.pop(1); sys.path.insert(0,p); runpy.run_path(p+"/macos_lag_helper.py",run_name="__main__")' "$PWD/tools" \
    "$@"
open "$APP"
echo 'Clumsier opened. Terminal can now close. Quit Clumsier to clear lag; helper exits after 60 seconds without reconnection.'
