import QtQuick

QuietButton {
    id: recorder
    required property var bridge
    property string shortcut: ""
    property bool recording: false
    signal shortcutChosen(string shortcut)
    text: recording ? "Press shortcut…" : shortcut
    selected: recording
    onClicked: { recording = true; forceActiveFocus(); }
    onActiveFocusChanged: if (!activeFocus) recording = false
    Keys.onPressed: function(event) {
        if (!recording) { event.accepted = false; return; }
        event.accepted = true;
        if (event.isAutoRepeat) return;
        if (event.key === Qt.Key_Escape) { recording = false; return; }
        const chosen = bridge.shortcutForKey(event.key, event.modifiers);
        if (chosen === "") return; // Wait for the key after Ctrl/Alt/Shift.
        recording = false;
        shortcutChosen(chosen);
    }
}
