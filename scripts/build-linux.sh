#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ "$(uname -s)" != Linux ]; then
    echo "Build this prototype on Linux with Linux headers and libraries." >&2
    exit 1
fi
pkg-config --exists libnetfilter_queue
mkdir -p build/linux
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -pthread \
    -DCJSON_NESTING_LIMIT=32 -Isrc $(pkg-config --cflags libnetfilter_queue) \
    src/platform/linux/main.c src/prototype/console.c \
    src/core/actions.c src/core/controller.c src/core/network.c src/core/preset.c src/core/preset_json.c \
    src/backends/linux/backend.c src/backends/linux/nft.c src/backends/linux/rules.c \
    src/backends/linux/session.c src/backends/linux/lag_queue.c external/cjson/cJSON.c \
    $(pkg-config --libs libnetfilter_queue) -o build/linux/clumsier
cp LICENSE build/linux/LICENSE.txt
cp external/cjson/LICENSE build/linux/cJSON-LICENSE.txt
echo "Built build/linux/clumsier. Start is explicit; see docs/LINUX.md before the first network test."
