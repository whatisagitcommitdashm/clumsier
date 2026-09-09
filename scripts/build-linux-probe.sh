#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ "$(uname -s)" != Linux ]; then
    echo "The NFQUEUE probe requires a native Linux compiler and headers." >&2
    exit 1
fi
pkg-config --exists libnetfilter_queue
mkdir -p build/linux
# pkg-config emits separate compiler/linker arguments, so splitting is intended.
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic \
    $(pkg-config --cflags libnetfilter_queue) \
    src/backends/linux/nfqueue_probe.c src/backends/linux/lag_queue.c src/core/network.c \
    $(pkg-config --libs libnetfilter_queue) -o build/linux/nfqueue-probe
