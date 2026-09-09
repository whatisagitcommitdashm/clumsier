#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/macos
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -Isrc \
  src/core/network.c src/backends/macos/held_packets.c tests/macos/held_packets.c \
  -o build/macos/held-packets
build/macos/held-packets
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -Isrc \
  src/core/network.c src/backends/macos/packet_match.c tests/macos/packet_match.c \
  -o build/macos/packet-match
build/macos/packet-match
