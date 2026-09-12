import QtQuick
import QtQuick.Controls

CheckBox {
    id: control
    required property var theme
    property string helpText: ""
    HoverHandler { id: hintHover; blocking: false }
    QuietHint { objectName: "hint-" + control.objectName; theme: control.theme; text: control.helpText; visible: text !== "" && hintHover.hovered && control.visible && !control.down && !!control.Window.window && control.Window.window.active }
    spacing: 8 * theme.scale
    padding: 4 * theme.scale
    implicitHeight: 28 * theme.scale
    font.family: theme.family
    font.pixelSize: 12 * theme.scale
    indicator: Rectangle {
        implicitWidth: 16 * control.theme.scale
        implicitHeight: implicitWidth
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: 3
        color: control.theme.surface
        border.color: control.visualFocus ? control.theme.accent : control.theme.border
        Text {
            anchors.centerIn: parent
            text: control.checked ? "✓" : ""
            font.pixelSize: 13 * control.theme.scale
            color: control.theme.accent
        }
    }
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.theme.text
        leftPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
    }
}
