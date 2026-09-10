import QtQuick
import QtQuick.Controls

Dialog {
    id: dialog
    required property var theme
    // The Basic style's opaque rectangular header covered the rounded panel.
    // Keep its title, but let one background paint all four corners.
    header: Label {
        text: dialog.title
        visible: text !== ""
        padding: 14 * dialog.theme.scale
        color: dialog.theme.text
        font.family: dialog.theme.family
        font.bold: true
        wrapMode: Text.WordWrap
    }
    background: Rectangle {
        color: dialog.theme.surface
        radius: 8
        border.color: dialog.theme.border
    }
}
