#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
unset QT_PLUGIN_PATH QML2_IMPORT_PATH QML_IMPORT_PATH QT_QPA_PLATFORMTHEME QT_STYLE_OVERRIDE
QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=software QT_QUICK_BACKEND=software \
    ctest --test-dir build/qt-linux --output-on-failure
