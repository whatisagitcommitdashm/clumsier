import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ComboBox {
    id: control
    required property var theme
    TextMetrics { id: selectedMetrics; text: control.displayText; font: control.font }
    implicitWidth: Math.ceil(selectedMetrics.advanceWidth) + 48 * theme.scale
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.preferredWidth: implicitWidth
    Layout.maximumWidth: implicitWidth
    implicitHeight: 38 * theme.scale
    padding: 0 // The text item supplies padding; counting both would clip the selected label.
    leftPadding: 0
    rightPadding: 0
    font.family: theme.family
    font.pixelSize: 14 * theme.scale
    contentItem: Text {
        text: control.displayText; color: control.theme.text; font: control.font
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        leftPadding: 12 * control.theme.scale; rightPadding: 30 * control.theme.scale
    }
    indicator: Text {
        text: "⌄"; color: control.theme.muted
        anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
    }
    background: Rectangle {
        color: control.theme.surface; radius: 5
        border.color: control.visualFocus ? control.theme.accent : control.theme.border
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        required property var modelData
        width: control.width
        implicitHeight: 38 * control.theme.scale
        text: control.textRole ? modelData[control.textRole] : modelData
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: option.text; color: control.theme.text; font: control.font
            verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        }
        background: Rectangle {
            color: option.highlighted ? control.theme.selected : control.theme.surface
            radius: 4
        }
    }
    popup: Popup {
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: control.theme.reducedMotion ? 0 : 90; easing.type: Easing.OutCubic } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: control.theme.reducedMotion ? 0 : 70 } }
        y: control.height + 4
        width: control.width
        padding: 4
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 300 * control.theme.scale)
        background: Rectangle { color: control.theme.surface; radius: 6; border.color: control.theme.border }
        contentItem: ListView {
            clip: true; implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollBar.vertical: ScrollBar {
                contentItem: Rectangle { implicitWidth: 4; radius: 2; color: control.theme.muted; opacity: 0.6 }
            }
        }
    }
}
