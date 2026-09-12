import QtQuick
import QtQuick.Controls

ToolTip {
    id: hint
    required property var theme
    delay: 350
    timeout: -1
    padding: 10 * theme.scale
    margins: 12
    width: Math.min(310 * theme.scale, implicitWidth)
    closePolicy: Popup.NoAutoClose
    contentItem: Text {
        text: hint.text; color: hint.theme.text
        font.family: hint.theme.family; font.pixelSize: 12 * hint.theme.scale
        wrapMode: Text.WordWrap
    }
    background: Rectangle { color: hint.theme.surface; border.color: hint.theme.border; radius: 7 }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: hint.theme.reducedMotion ? 0 : 100 }
        NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: hint.theme.reducedMotion ? 0 : 100; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: hint.theme.reducedMotion ? 0 : 70 } }
}
