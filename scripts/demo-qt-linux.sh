#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
exec sh scripts/test-linux-network.sh --qt-demo
