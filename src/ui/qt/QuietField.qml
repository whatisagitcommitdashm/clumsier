import QtQuick
import QtQuick.Controls

TextField {
    id: field
    required property var theme
    implicitHeight: 42 * theme.scale
    leftPadding: 12 * theme.scale
    rightPadding: 12 * theme.scale
    font.family: theme.family
    font.pixelSize: 15 * theme.scale
    color: theme.text
    placeholderTextColor: theme.muted
    selectionColor: theme.accent
    selectedTextColor: theme.surface
    selectByMouse: true
    Keys.onEscapePressed: function(event) {
        // Let an enclosing popup handle Escape before moving focus out of it.
        event.accepted = false;
        Qt.callLater(function() { if (field.activeFocus) { field.focus = false; field.Window.window.contentItem.forceActiveFocus(); } });
    }
    onAccepted: { field.focus = false; field.Window.window.contentItem.forceActiveFocus(); }
    background: Rectangle {
        radius: 5
        color: field.theme.surface
        border.color: field.activeFocus ? field.theme.accent : field.theme.border
        Behavior on border.color { ColorAnimation { duration: field.theme.motion } }
    }
}
