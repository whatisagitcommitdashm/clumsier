import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ThemeCatalog.js" as Catalog

Popup {
    id: picker
    objectName: "themePicker"
    required property var theme
    property string committedId: "lavender"
    property string previewId: ""
    property var favoriteIds: []
    property var results: []
    property int highlighted: -1
    property string pendingHoverId: ""
    signal chosen(string themeId)
    signal favoriteToggled(string themeId)
    modal: true
    dim: false
    focus: true
    padding: 16
    width: Math.min(480 * theme.scale, parent.width - 32)
    height: Math.min(540 * theme.scale, parent.height - 48)
    x: (parent.width - width) / 2
    y: Math.min(90, (parent.height - height) / 2)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: picker.theme.surface; radius: 10; border.color: picker.theme.border }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: picker.theme.reducedMotion ? 0 : 180; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: picker.theme.reducedMotion ? 0 : 120 } }

    function cancelHover() {
        hoverPreview.stop();
        pendingHoverId = "";
    }
    function hover(index) {
        if (!opened || list.moving || index < 0 || index >= results.length) return;
        highlighted = index;
        const id = results[index].id;
        // Moving within a row shouldn't restart its half-second dwell time.
        if (pendingHoverId === id) return;
        cancelHover();
        if (previewId === id) return;
        pendingHoverId = id;
        hoverPreview.start();
    }
    Timer {
        id: hoverPreview
        interval: 500
        onTriggered: {
            if (picker.opened && picker.pendingHoverId !== "")
                picker.previewId = picker.pendingHoverId;
            picker.pendingHoverId = "";
        }
    }

    function filter() {
        cancelHover();
        const query = search.text.trim().toLowerCase();
        results = Catalog.themes.filter(function(item) { return item.name.toLowerCase().includes(query); })
            .slice().sort(function(a, b) {
                const rank = Number(favoriteIds.includes(b.id)) - Number(favoriteIds.includes(a.id));
                return rank || a.name.localeCompare(b.name);
            });
        highlighted = results.length ? 0 : -1;
    }
    function highlight(index) {
        cancelHover();
        if (index < 0 || index >= results.length) return;
        highlighted = index;
        previewId = results[index].id;
        list.positionViewAtIndex(index, ListView.Contain);
    }
    function moveSelection(direction) {
        if (!results.length) return;
        highlight((highlighted + direction + results.length) % results.length);
    }
    function applySelection() {
        if (highlighted < 0 || highlighted >= results.length) return;
        cancelHover();
        previewId = results[highlighted].id;
        chosen(results[highlighted].id);
        close();
    }
    onOpened: {
        search.text = "";
        filter();
        const saved = results.findIndex(function(item) { return item.id === committedId; });
        highlighted = saved;
        previewId = committedId;
        if (saved >= 0) list.positionViewAtIndex(saved, ListView.Contain);
        search.forceActiveFocus();
    }
    // Preview never writes preferences and no timer can revive it after cancel.
    onAboutToHide: { cancelHover(); previewId = "" }
    contentItem: ColumnLayout {
        spacing: 12
        QuietField {
            id: search
            objectName: "themeSearch"
            theme: picker.theme
            Layout.fillWidth: true
            placeholderText: "Theme…"
            Accessible.name: "Search themes"
            onTextEdited: { picker.filter(); picker.highlight(picker.highlighted) }
            Keys.onDownPressed: picker.moveSelection(1)
            Keys.onUpPressed: picker.moveSelection(-1)
            Keys.onReturnPressed: picker.applySelection()
            Keys.onEnterPressed: picker.applySelection()
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: picker.results
            boundsBehavior: Flickable.StopAtBounds
            onMovementStarted: picker.cancelHover()
            ScrollBar.vertical: ScrollBar { }
            delegate: Item {
                id: row
                objectName: "theme-row-" + modelData.id
                required property var modelData
                required property int index
                width: list.width
                height: 44 * picker.theme.scale
                Rectangle { anchors.fill: parent; radius: 4; color: picker.highlighted === row.index ? picker.theme.selected : "transparent" }
                MouseArea {
                    anchors.fill: parent
                    onClicked: { picker.highlight(row.index); picker.applySelection() }
                }
                HoverHandler {
                    // Observe the whole row, including its favorite button.
                    onPointChanged: if (hovered) picker.hover(row.index)
                    onHoveredChanged: if (!hovered && picker.pendingHoverId === row.modelData.id) picker.cancelHover()
                }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 10
                    Label { text: picker.committedId === row.modelData.id ? "✓" : " "; color: picker.theme.accent; font.family: picker.theme.family }
                    Label { text: row.modelData.name; color: picker.theme.text; font.family: picker.theme.family; font.pixelSize: 14 * picker.theme.scale; Layout.fillWidth: true; elide: Text.ElideRight }
                    QuietButton {
                        objectName: "favorite-" + row.modelData.id
                        theme: picker.theme; subtle: true; transparentIdle: true
                        implicitWidth: 30; implicitHeight: 30
                        text: picker.favoriteIds.includes(row.modelData.id) ? "★" : "☆"
                        Accessible.name: "Favorite " + row.modelData.name
                        onClicked: picker.favoriteToggled(row.modelData.id)
                        Keys.onReturnPressed: picker.applySelection()
                        Keys.onDownPressed: picker.moveSelection(1)
                        Keys.onUpPressed: picker.moveSelection(-1)
                    }
                    Rectangle {
                        implicitWidth: 68 * picker.theme.scale
                        implicitHeight: 26 * picker.theme.scale
                        radius: height / 2
                        color: row.modelData.surface
                        Row {
                            anchors.centerIn: parent
                            spacing: 6 * picker.theme.scale
                            Repeater {
                                model: [row.modelData.background, row.modelData.accent, row.modelData.text]
                                Rectangle { required property string modelData; width: 12 * picker.theme.scale; height: width; radius: width / 2; color: modelData }
                            }
                        }
                    }
                }
            }
        }
        Label { visible: picker.results.length === 0; text: "No matching themes"; color: picker.theme.muted; font.family: picker.theme.family; Layout.alignment: Qt.AlignHCenter }
        Rectangle { Layout.fillWidth: true; height: 1; color: picker.theme.border }
        Label { text: "↑ ↓ preview    Enter apply    Esc cancel"; color: picker.theme.muted; font.family: picker.theme.family; font.pixelSize: 12 * picker.theme.scale; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    }
}
