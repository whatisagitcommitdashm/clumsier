#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/linux
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic \
    tests/linux/lag_queue.c src/backends/linux/lag_queue.c src/core/network.c \
    -o build/linux/test-lag-queue
build/linux/test-lag-queue
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic \
    tests/linux/rules_session.c src/backends/linux/rules.c src/backends/linux/session.c src/core/network.c \
    -o build/linux/test-rules-session
build/linux/test-rules-session
