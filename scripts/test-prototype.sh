#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/prototype
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -DCJSON_NESTING_LIMIT=32 -Isrc \
  tests/prototype_console.c src/prototype/console.c src/core/controller.c \
  src/core/network.c src/core/preset.c src/core/preset_json.c external/cjson/cJSON.c \
  -o build/prototype/console-tests
build/prototype/console-tests
