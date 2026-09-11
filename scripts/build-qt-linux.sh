#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ -z "${CLUMSIER_QT_ROOT:-}" ] && [ -d build/Qt/6.10.3/gcc_64 ]; then
    CLUMSIER_QT_ROOT="$PWD/build/Qt/6.10.3/gcc_64"
fi
cmake -S src/ui/qt -B build/qt-linux -DCMAKE_BUILD_TYPE=Debug \
    ${CLUMSIER_QT_ROOT:+"-DCMAKE_PREFIX_PATH=$CLUMSIER_QT_ROOT"}
cmake --build build/qt-linux --parallel "${CLUMSIER_BUILD_JOBS:-4}"
printf '%s\n' 'Built Qt app: run sh scripts/run-qt-linux.sh'
