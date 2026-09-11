#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ ! -x build/linux/clumsier ]; then
    echo 'Run sh scripts/build-linux.sh first.' >&2
    exit 1
fi
# Fresh user/network namespaces: no root password, host routes or firewall edits.
CLUMSIER_PARENT_NETNS=$(readlink /proc/self/ns/net)
export CLUMSIER_PARENT_NETNS
exec unshare --user --map-root-user --net python3 tests/linux/network.py "$@"
