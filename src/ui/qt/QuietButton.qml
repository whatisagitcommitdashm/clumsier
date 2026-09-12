import QtQuick
import QtQuick.Controls

Button {
    id: control
    required property var theme
    property bool subtle: false
    property bool transparentIdle: false
    property bool selected: false
    property bool accentText: false
    property bool alignLeft: false
    property string helpText: ""
    QuietHint { objectName: "hint-" + control.objectName; theme: control.theme; text: control.helpText; visible: text !== "" && control.pointerInside && !control.down && !!control.Window.window && control.Window.window.active }
    hoverEnabled: false
    // Track the pointer separately from the button's press/focus state. Rows
    // should stop looking hovered when the pointer leaves, even after a click.
    readonly property bool pointerInside: pointerHover.hovered && enabled && visible
    HoverHandler { id: pointerHover; blocking: false }
    implicitHeight: 38 * theme.scale
    implicitWidth: Math.max(38 * theme.scale, contentItem.implicitWidth + 28 * theme.scale)
    padding: 10 * theme.scale
    font.family: theme.family
    font.pixelSize: 14 * theme.scale
    Accessible.name: text
    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? control.theme.muted : control.accentText || control.selected
               ? control.theme.accent : control.theme.text
        opacity: control.enabled ? 1 : 0.5
        horizontalAlignment: control.alignLeft ? Text.AlignLeft : Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 5
        color: control.down || control.selected ? control.theme.selected
               : control.pointerInside ? control.theme.hover
               : control.subtle ? (control.transparentIdle ? "transparent" : control.theme.background) : control.theme.surface
        border.width: control.visualFocus || !control.subtle ? 1 : 0
        border.color: control.visualFocus ? control.theme.accent : control.theme.border
    }
}
