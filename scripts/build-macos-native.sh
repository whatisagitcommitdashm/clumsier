#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test "$(uname -s)" = Darwin || { echo 'Native compilation requires macOS and Xcode command line tools.' >&2; exit 1; }
mkdir -p build/macos/objects
for source in src/core/network.c src/core/preset.c src/core/preset_json.c external/cjson/cJSON.c src/backends/macos/held_packets.c src/backends/macos/packet_match.c; do
  name=$(basename "$source" .c)
  xcrun --sdk macosx clang -std=c11 -Wall -Wextra -Werror -mmacosx-version-min=13.0 -DCJSON_NESTING_LIMIT=32 -Isrc -Iexternal/cjson -c "$source" -o "build/macos/objects/$name.o"
done
xcrun --sdk macosx clang -fobjc-arc -fblocks -Wall -Wextra -Werror -mmacosx-version-min=13.0 -Isrc \
  src/platform/macos/host.m build/macos/objects/*.o -framework AppKit -framework NetworkExtension -framework SystemExtensions -o build/macos/Clumsier-host
xcrun --sdk macosx clang -fobjc-arc -fblocks -Wall -Wextra -Werror -mmacosx-version-min=13.0 -Isrc \
  src/platform/macos/provider.m build/macos/objects/*.o -framework Foundation -framework NetworkExtension -framework Network -o build/macos/Clumsier-provider
echo 'Native executables compiled and linked; no bundle, signature, installation or activation performed.'
