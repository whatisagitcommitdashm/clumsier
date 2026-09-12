import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: hud
    objectName: "clumsierHud"
    required property var bridge
    required property var paletteTheme
    property real scaleFactor: 1
    property real backgroundOpacity: 0.85
    property bool showControls: true
    property bool controlsEnabled: true
    property bool shuttingDown: false
    readonly property var state: bridge ? bridge.state : ({})
    signal hideRequested()
    // No transient parent: minimizing the editor must not minimize the HUD.
    // The tool window stays above ordinary apps without taking keyboard focus.
    transientParent: null
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    title: "Clumsier HUD"
    color: "transparent"
    width: 360 * hudTheme.scale
    height: content.implicitHeight + 28 * hudTheme.scale
    x: 40; y: 100
    onClosing: function(event) {
        // Closing just the HUD means hide it; application exit must be allowed
        // to close this window rather than vetoing Qt's quit request.
        if (!shuttingDown) { event.accepted = false; hideRequested(); }
    }
    Theme { id: hudTheme; activeId: hud.paletteTheme.activeId; scale: Math.max(0.75, Math.min(1.5, hud.scaleFactor)); reducedMotion: hud.paletteTheme.reducedMotion }
    Rectangle {
        anchors.fill: parent; radius: 12 * hudTheme.scale
        color: Qt.rgba(hudTheme.background.r, hudTheme.background.g, hudTheme.background.b, hud.backgroundOpacity)
        border.color: hudTheme.border
    }
    QuietButton {
        objectName: "hudToggle"; parent: hud.state.sequence ? sequenceControls : delayRow
        theme: hudTheme; text: hud.state.running ? "Stop" : "Start"; focusPolicy: Qt.NoFocus
        visible: hud.showControls
        enabled: hud.controlsEnabled && hud.bridge && (hud.state.running || hud.state.canStart)
        onClicked: hud.bridge.execute(2)
    }
    ColumnLayout {
        id: content
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: 14 * hudTheme.scale
        spacing: 8 * hudTheme.scale
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: hud.state.running ? "● Running" : "● Stopped"
                color: hud.state.running ? hudTheme.good : hudTheme.muted
                font.family: hudTheme.family; font.pixelSize: 12 * hudTheme.scale
                Layout.fillWidth: true
                MouseArea { anchors.fill: parent; cursorShape: Qt.SizeAllCursor; onPressed: hud.startSystemMove() }
            }
            QuietButton { theme: hudTheme; text: "×"; Accessible.name: "Hide HUD"; subtle: true; transparentIdle: true; implicitWidth: 26 * hudTheme.scale; implicitHeight: 24 * hudTheme.scale; padding: 0; focusPolicy: Qt.NoFocus; onClicked: hud.hideRequested() }
        }
        Label {
            objectName: "hudSequenceName"
            text: hud.state.sequence ? hud.state.sequenceName : hud.state.activity === "sequences" ? "No sequence ready" : "Delay"
            Layout.fillWidth: true; elide: Text.ElideRight
            font.family: hudTheme.family; font.pixelSize: 20 * hudTheme.scale; color: hudTheme.text
        }
        Label {
            objectName: "hudStepName"; visible: !!hud.state.sequence
            text: (hud.state.stepName || "") + "  ·  " + ((hud.state.step || 0) + 1) + "/" + (hud.state.stepCount || 0)
            Layout.fillWidth: true; elide: Text.ElideRight
            font.family: hudTheme.family; font.pixelSize: 14 * hudTheme.scale; color: hudTheme.accent
        }
        RowLayout {
            id: delayRow; Layout.fillWidth: true; spacing: 8 * hudTheme.scale
        Label {
            objectName: "hudDelay"
            text: hud.state.targetPing ? "~" + hud.state.expected + " ms"
                : (hud.state.inbound === hud.state.outbound ? hud.state.inbound + " ms each way"
                    : (hud.state.inbound || 0) + " ms in · " + (hud.state.outbound || 0) + " ms out")
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            font.family: hudTheme.family; font.pixelSize: (hud.state.sequence ? 23 : 18) * hudTheme.scale; color: hudTheme.text
        }
        }
        Label {
            visible: !!hud.state.sequence
            text: hud.state.targetPing ? (hud.state.running ? "Estimated ping" : "Estimated ping when started") : (hud.state.running ? "Added delay" : "Added delay when started")
            font.family: hudTheme.family; font.pixelSize: 11 * hudTheme.scale; color: hudTheme.muted
        }
        RowLayout {
            id: sequenceControls
            visible: hud.showControls && !!hud.state.sequence; enabled: hud.controlsEnabled
            Layout.fillWidth: true; spacing: 0
            QuietButton { theme: hudTheme; text: "‹"; implicitWidth: 30 * hudTheme.scale; padding: 4; Accessible.name: "Previous step"; subtle: true; transparentIdle: true; focusPolicy: Qt.NoFocus; visible: !!hud.state.sequence; enabled: hud.bridge && !hud.bridge.dirty; onClicked: hud.bridge.execute(4) }
            QuietButton { theme: hudTheme; text: "Reset"; implicitWidth: 62 * hudTheme.scale; padding: 4; subtle: true; transparentIdle: true; focusPolicy: Qt.NoFocus; visible: !!hud.state.sequence; enabled: hud.bridge && !hud.bridge.dirty; onClicked: hud.bridge.execute(5) }
            QuietButton { objectName: "hudNext"; theme: hudTheme; text: "›"; implicitWidth: 30 * hudTheme.scale; padding: 4; Accessible.name: "Next step"; subtle: true; transparentIdle: true; focusPolicy: Qt.NoFocus; visible: !!hud.state.sequence; enabled: hud.bridge && !hud.bridge.dirty; onClicked: hud.bridge.execute(3) }
            Item { Layout.fillWidth: true }

        }
    }
}
