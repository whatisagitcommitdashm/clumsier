#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
# Avoid loading desktop-specific style plugins built against a different Qt SDK.
unset QT_PLUGIN_PATH QML2_IMPORT_PATH QML_IMPORT_PATH QT_QPA_PLATFORMTHEME QT_STYLE_OVERRIDE
exec ./build/qt-linux/clumsier-ui-preview "$@"
