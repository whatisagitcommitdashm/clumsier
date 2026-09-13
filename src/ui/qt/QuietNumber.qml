import QtQuick
import QtQuick.Controls

QuietField {
    id: field
    property int value: 0
    property int maximum: 15000
    signal valueCommitted(int number)
    signal invalidEntry()
    function restore() { text = String(value); }
    function finish() {
        const candidate = text.trim() === "" ? "0" : text;
        if (!/^[0-9]+$/.test(candidate) || Number(candidate) > maximum) {
            restore(); invalidEntry(); return;
        }
        valueCommitted(Number(candidate));
        // The owner updates value only after accepting the change.
        restore();
    }
    Component.onCompleted: restore()
    onValueChanged: if (!activeFocus) restore()
    onActiveFocusChanged: {
        if (activeFocus) {
            // The click positions the caret after focus is delivered.
            Qt.callLater(function() { if (field.activeFocus && field.text === "0") field.selectAll(); });
        } else finish();
    }
}
