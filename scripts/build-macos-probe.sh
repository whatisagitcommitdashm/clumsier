#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ "$(uname -s)" != Darwin ]; then
  echo 'The SDK probe requires macOS and Xcode command line tools.' >&2
  exit 1
fi
mkdir -p build/macos
xcrun --sdk macosx clang -fobjc-arc -fmodules -Wall -Wextra -Werror \
  -mmacosx-version-min=10.15 -c tests/macos/sdk_probe.m -o build/macos/sdk-probe.o
echo 'Apple API probe compiled. No extension was installed or activated.'
