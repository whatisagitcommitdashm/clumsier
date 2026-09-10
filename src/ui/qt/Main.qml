import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import "ThemeCatalog.js" as Catalog

ApplicationWindow {
    id: window
    objectName: "previewWindow"
    width: 1180
    height: 780
    minimumWidth: 760
    minimumHeight: 560
    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint
    title: "Clumsier — UI prototype"
    color: appTheme.background
    font.family: appTheme.family
    font.pixelSize: 14 * appTheme.scale
    palette.window: appTheme.background
    palette.base: appTheme.surface
    palette.text: appTheme.text
    palette.windowText: appTheme.text
    palette.buttonText: appTheme.text
    palette.highlight: appTheme.accent

    Theme {
        id: appTheme
        scale: preferences.uiScale
        reducedMotion: preferences.reducedMotion
        activeId: themePicker.visible && themePicker.previewId !== "" ? themePicker.previewId : Catalog.find(preferences.themeId).id
    }
    readonly property string savedTheme: Catalog.find(preferences.themeId).id
    readonly property string activeTheme: appTheme.activeId
    readonly property bool themePickerOpen: themePicker.visible
    ThemePicker {
        id: themePicker
        parent: Overlay.overlay
        theme: appTheme
        committedId: window.savedTheme
        favoriteIds: Catalog.favoritesFromJson(preferences.favoriteThemeIds)
        onChosen: function(themeId) {
            preferences.themeId = themeId;
            // Property writes are batched by Settings. Store explicit choices now.
            preferences.setValue("themeId", themeId);
            preferences.sync();
        }
        onFavoriteToggled: function(themeId) {
            let favorites = Catalog.favoritesFromJson(preferences.favoriteThemeIds);
            if (favorites.includes(themeId)) favorites = favorites.filter(function(id) { return id !== themeId; });
            else favorites.push(themeId);
            preferences.favoriteThemeIds = JSON.stringify(favorites);
            preferences.setValue("favoriteThemeIds", preferences.favoriteThemeIds);
            preferences.sync();
        }
        onClosed: themeButton.forceActiveFocus(Qt.PopupFocusReason)
    }
    // These settings belong to the preview, never the real application's profiles.
    Settings {
        id: preferences
        location: window.settingsLocation
        category: "shell"
        property bool compactSidebar: false
        property bool showHints: true
        property bool reducedMotion: false
        property real uiScale: 1
        property string themeId: "lavender"
        property string favoriteThemeIds: "[]"
    }
    property url settingsLocation: ""
    readonly property real interfaceScale: preferences.uiScale
    property string page: "sequences"
    onActiveChanged: if (!active) closeSidebar()
    // Mouse peeks ignore incidental focus from clicks; keyboard opens stay until dismissed.
    property string sidebarReveal: "closed"
    readonly property bool sidebarRevealed: sidebarReveal !== "closed"
    readonly property bool sidebarOpen: !preferences.compactSidebar || sidebarRevealed
    readonly property real sidebarWidth: 210 * appTheme.scale
    property int selectedStep: 0
    property int activeStep: 0
    property bool running: false
    property bool wrapSteps: true
    property string sequenceName: "Utopia"
    property int baselineMs: 40
    property string notice: ""
    property int previewPing: 200
    onSelectedStepChanged: editorFade.restart()
    onPageChanged: {
        scroll.contentItem.contentY = 0;
        pageFade.restart();
    }
    function closeSidebar() {
        hideTimer.stop();
        sidebarReveal = "closed";
    }
    signal maximizeRequested()
    function toggleMaximized() { maximizeRequested() }
    function setLowest(lowest) {
        // Commit the mode as one choice. A lowest step has no numeric target.
        targetField.focus = false;
        steps.setProperty(selectedStep, "target", 0);
        steps.setProperty(selectedStep, "lowest", lowest);
    }

    ListModel {
        id: steps
        ListElement { name: "First leap"; target: 200; lowest: false }
        ListElement { name: "Recovery"; target: 0; lowest: true }
        ListElement { name: "Third leap"; target: 150; lowest: false }
        ListElement { name: "Final leap"; target: 100; lowest: false }
    }
    function inform(message) { notice = message; noticeTimer.restart() }
    function toggleRunning() {
        running = !running;
        if (running) { activeStep = selectedStep; previewPing = expected(activeStep) }
    }
    function expected(index) {
        let step = steps.get(index);
        return step.lowest ? baselineMs : Math.max(baselineMs, step.target);
    }
    function advance() {
        activeStep = activeStep + 1 < steps.count ? activeStep + 1 : wrapSteps ? 0 : activeStep;
        previewPing = expected(activeStep);
    }
    function selectSequence(name) {
        sequenceName = name;
        selectedStep = 0;
        if (preferences.compactSidebar) closeSidebar();
        inform("Sample sequence selected. No files are changed.");
    }
    Timer { id: noticeTimer; interval: 4500; onTriggered: notice = "" }
    Shortcut { enabled: !themePicker.visible; sequence: "F7"; autoRepeat: false; onActivated: toggleRunning() }
    Shortcut { enabled: !themePicker.visible; sequence: "F8"; autoRepeat: false; onActivated: advance() }
    Shortcut {
        enabled: !themePicker.visible
        sequence: "Ctrl+B"
        onActivated: {
            if (!preferences.compactSidebar) {
                preferences.compactSidebar = true;
                closeSidebar();
            } else if (sidebarRevealed) closeSidebar();
            else {
                sidebarReveal = "keyboard";
                sidebar.forceActiveFocus(Qt.ShortcutFocusReason);
            }
        }
    }
    Shortcut { enabled: !themePicker.visible; sequence: "Escape"; onActivated: closeSidebar() }

    // Let the OS perform moving/resizing, including screen-edge snapping.
    MouseArea {
        anchors.top: parent.top; width: parent.width; height: header.height
        onPressed: window.startSystemMove()
        onDoubleClicked: window.toggleMaximized()
    }

    component Caption: Label {
        color: appTheme.muted
        font.family: appTheme.family
        font.pixelSize: 13 * appTheme.scale
        wrapMode: Text.WordWrap
    }
    component Heading: Label {
        color: appTheme.text
        font.family: appTheme.family
        font.pixelSize: 28 * appTheme.scale
        elide: Text.ElideRight
    }
    component Rule: Rectangle { color: appTheme.border; implicitHeight: 1; Layout.fillWidth: true }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // The header is also the title bar; controls receive clicks before the drag area.
        GridLayout {
            id: header
            readonly property bool stacked: window.width < 1100 * appTheme.scale
            columns: stacked ? 2 : 3
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 20
            Layout.preferredHeight: (stacked ? 112 : 72) * appTheme.scale
            columnSpacing: 22
            rowSpacing: 0
            Label { text: "clumsier"; color: appTheme.text; font.pixelSize: 25 * appTheme.scale; Layout.row: 0; Layout.column: 0 }
            RowLayout {
                Layout.fillWidth: true
                Layout.row: header.stacked ? 1 : 0
                Layout.column: header.stacked ? 0 : 1
                Layout.columnSpan: header.stacked ? 2 : 1
                spacing: 8
                Repeater {
                model: ["quick controls", "sequences", "hotkeys"]
                QuietButton {
                    required property string modelData
                    objectName: "tab-" + modelData
                    theme: appTheme
                    text: modelData
                    subtle: true
                    selected: window.page === modelData
                    onClicked: { window.page = modelData; closeSidebar() }
                }
                }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                Layout.row: 0; Layout.column: header.stacked ? 1 : 2
                Layout.alignment: Qt.AlignRight
                Layout.fillWidth: header.stacked
                spacing: 3
                Item { Layout.fillWidth: true; visible: header.stacked }
                QuietButton {
                    id: themeButton
                    objectName: "themeButton"
                    theme: appTheme; subtle: true
                    text: Catalog.find(appTheme.activeId).name + "  ▾"
                    Layout.maximumWidth: 200 * appTheme.scale
                    Accessible.name: "Choose theme"
                    onClicked: { closeSidebar(); themePicker.open() }
                }
                QuietButton {
                    objectName: "settingsButton"
                    theme: appTheme; text: "settings"; subtle: true; selected: page === "settings"
                    onClicked: { page = "settings"; closeSidebar() }
                }
                QuietButton {
                    objectName: "minimizeButton"
                    theme: appTheme; text: "−"; subtle: true; implicitWidth: 36
                    Accessible.name: "Minimize"
                    onClicked: window.showMinimized()
                }
                QuietButton {
                    objectName: "maximizeButton"
                    theme: appTheme; text: window.visibility === Window.Maximized || window.visibility === Window.FullScreen ? "❐" : "□"; subtle: true; implicitWidth: 36
                    Accessible.name: window.visibility === Window.Maximized || window.visibility === Window.FullScreen ? "Restore window" : "Maximize"
                    onClicked: window.toggleMaximized()
                }
                QuietButton {
                    objectName: "closeButton"
                    theme: appTheme; text: "×"; subtle: true; implicitWidth: 36
                    Accessible.name: "Close"
                    onClicked: window.close()
                }
            }
        }
        Rule { }
        Item {
            id: workspace
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Pinned mode reserves space; compact mode floats above the editor.
            // Only the drawer moves during reveal, so the content does not jump.
            Item {
                id: content
                NumberAnimation { id: pageFade; target: content; property: "opacity"; from: 0.7; to: 1; duration: appTheme.motion }
                anchors.fill: parent
                anchors.leftMargin: page === "sequences" && !preferences.compactSidebar ? sidebarWidth : 0
                ScrollView {
                    id: scroll
                    anchors.fill: parent
                    contentWidth: availableWidth
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: scroll.availableWidth
                        spacing: 24 * appTheme.scale
                        Item { Layout.preferredHeight: 3 }
                        ColumnLayout {
                            visible: page === "sequences"
                            Layout.fillWidth: true
                            Layout.leftMargin: 32
                            Layout.rightMargin: 32
                            spacing: 22 * appTheme.scale
                            RowLayout {
                                Layout.fillWidth: true
                                Heading { text: sequenceName; Layout.fillWidth: true }
                                QuietButton { theme: appTheme; text: "Import"; onClicked: inform("Import will connect to your presets in the next stage.") }
                                QuietButton { theme: appTheme; text: "Export"; onClicked: inform("This sample stays in memory; export comes in the next stage.") }
                            }
                            RowLayout {
                                spacing: 18
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Caption { text: "server" }
                                    QuietButton { theme: appTheme; text: "Practice server  ▾"; alignLeft: true; Layout.fillWidth: true; onClicked: inform("Sample server • 40 ms baseline. Profiles connect in the next stage.") }
                                }
                                ColumnLayout {
                                    Caption { text: "baseline" }
                                    Label { text: window.baselineMs + " ms"; color: appTheme.text; Layout.preferredHeight: 38 * appTheme.scale; verticalAlignment: Text.AlignVCenter }
                                }
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: content.width >= 870 * appTheme.scale ? 2 : 1
                                columnSpacing: 32
                                rowSpacing: 24
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 440
                                    Layout.alignment: Qt.AlignTop
                                    spacing: 8
                                    Caption { text: "steps"; Layout.bottomMargin: 8 }
                                    Repeater {
                                        model: steps
                                        delegate: AbstractButton {
                                            id: stepRow
                                            required property int index
                                            required property string name
                                            required property int target
                                            required property bool lowest
                                            objectName: "step" + index
                                            Layout.fillWidth: true
                                            implicitHeight: 58 * appTheme.scale
                                            hoverEnabled: true
                                            Accessible.name: name + ", " + (lowest ? "Lowest available" : target + " milliseconds")
                                            onClicked: selectedStep = index
                                            background: Rectangle {
                                                radius: 4
                                                color: selectedStep === stepRow.index ? appTheme.selected : stepRow.hovered ? appTheme.hover : appTheme.background
                                                border.width: stepRow.visualFocus ? 1 : 0
                                                border.color: appTheme.accent
                                                Rectangle { x: 7; width: 3; height: parent.height - 24; y: 12; radius: 1; color: appTheme.accent; visible: selectedStep === stepRow.index }
                                            }
                                            contentItem: RowLayout {
                                                spacing: 14
                                                Caption { text: (stepRow.index + 1).toString().padStart(2, "0"); Layout.leftMargin: 15 }
                                                Label { text: stepRow.name; color: appTheme.text; Layout.fillWidth: true; elide: Text.ElideRight }
                                                Label { text: stepRow.lowest ? "Lowest available" : stepRow.target + " ms"; color: appTheme.text; Layout.rightMargin: 15; font.pixelSize: 13 * appTheme.scale }
                                            }
                                        }
                                    }
                                }
                                ColumnLayout {
                                    id: stepEditor
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 300
                                    Layout.alignment: Qt.AlignTop
                                    spacing: 16
                                    NumberAnimation { id: editorFade; target: stepEditor; property: "opacity"; from: 0.65; to: 1; duration: appTheme.motion }
                                    Caption { text: "edit step " + (selectedStep + 1) }
                                    Heading { text: steps.get(selectedStep).name; Layout.fillWidth: true }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        QuietButton {
                                            objectName: "targetMode"
                                            theme: appTheme; text: "Target ping"; subtle: true
                                            selected: !steps.get(selectedStep).lowest
                                            onClicked: if (steps.get(selectedStep).lowest) window.setLowest(false)
                                        }
                                        QuietButton {
                                            objectName: "lowestMode"
                                            theme: appTheme; text: "Lowest available"; subtle: true
                                            selected: steps.get(selectedStep).lowest
                                            onClicked: if (!steps.get(selectedStep).lowest) window.setLowest(true)
                                        }
                                    }
                                    QuietField {
                                        id: targetField
                                        objectName: "targetField"
                                        theme: appTheme
                                        Layout.fillWidth: true
                                        enabled: !steps.get(selectedStep).lowest
                                        text: steps.get(selectedStep).lowest ? "" : steps.get(selectedStep).target.toString()
                                        placeholderText: "No added delay"
                                        font.pixelSize: 26 * appTheme.scale
                                        validator: IntValidator { bottom: 0; top: 60000 }
                                        Accessible.name: "Target ping in milliseconds"
                                        onEditingFinished: {
                                            if (acceptableInput && !steps.get(selectedStep).lowest) steps.setProperty(selectedStep, "target", parseInt(text));
                                            text = Qt.binding(function() { return steps.get(selectedStep).lowest ? "" : steps.get(selectedStep).target.toString() });
                                        }
                                    }
                                    Caption {
                                        Layout.fillWidth: true
                                        text: steps.get(selectedStep).lowest ? "No added delay for this step."
                                              : "Adds " + Math.max(0, steps.get(selectedStep).target - window.baselineMs) + " ms for this server."
                                    }
                                    Caption { visible: running; Layout.fillWidth: true; text: "Editing the sample does not change the active preview." }
                                }
                            }
                            Rule { Layout.topMargin: 8 }
                            QuietButton { theme: appTheme; text: (wrapSteps ? "✓ " : "  ") + "Wrap at end"; subtle: true; selected: wrapSteps; onClicked: wrapSteps = !wrapSteps }
                        }
                        ColumnLayout {
                            visible: page === "quick controls"
                            Layout.fillWidth: true; Layout.leftMargin: 32; Layout.rightMargin: 32
                            spacing: 22
                            Heading { text: "A little delay." }
                            Caption { text: "Try the controls. This preview does not touch network traffic."; Layout.fillWidth: true }
                            Caption { text: "added delay" }
                            Heading { text: Math.round(delaySlider.value) + " ms" }
                            Slider { id: delaySlider; Layout.fillWidth: true; from: 0; to: 500; value: 160; stepSize: 10; Accessible.name: "Sample added delay" }
                        }
                        ColumnLayout {
                            visible: page === "hotkeys"
                            Layout.fillWidth: true; Layout.leftMargin: 32; Layout.rightMargin: 32
                            spacing: 22
                            Heading { text: "Keep your hands in the game."; Layout.fillWidth: true }
                            Caption { text: "Preview shortcuts work only while this window is focused. Your saved global bindings are untouched."; Layout.fillWidth: true }
                            Repeater {
                                model: ["F7    Start / stop preview", "F8    Next preview step", "Ctrl+B    Show / hide sequence list"]
                                Label { required property string modelData; text: modelData; color: appTheme.text }
                            }
                        }
                        ColumnLayout {
                            visible: page === "settings"
                            Layout.fillWidth: true; Layout.leftMargin: 32; Layout.rightMargin: 32
                            spacing: 20
                            Heading { text: "Make room for what matters."; Layout.fillWidth: true }
                            Caption { text: "These preferences are saved separately for the UI prototype."; Layout.fillWidth: true }
                            QuietButton { theme: appTheme; text: (preferences.compactSidebar ? "✓ " : "  ") + "Compact sequence list"; selected: preferences.compactSidebar; onClicked: { preferences.compactSidebar = !preferences.compactSidebar; closeSidebar() } }
                            Caption { text: "In compact mode, move to the left edge to reveal the list. Ctrl+B also opens it."; Layout.fillWidth: true }
                            QuietButton { theme: appTheme; text: (preferences.showHints ? "✓ " : "  ") + "Show bottom hints"; selected: preferences.showHints; onClicked: preferences.showHints = !preferences.showHints }
                            QuietButton { theme: appTheme; text: (preferences.reducedMotion ? "✓ " : "  ") + "Reduce motion"; selected: preferences.reducedMotion; onClicked: preferences.reducedMotion = !preferences.reducedMotion }
                            Caption { text: "interface size" }
                            RowLayout {
                                Repeater {
                                    model: [1, 1.15, 1.3]
                                    QuietButton { required property real modelData; objectName: "scale-" + Math.round(modelData * 100); theme: appTheme; text: Math.round(modelData * 100) + "%"; selected: preferences.uiScale === modelData; onClicked: preferences.uiScale = modelData }
                                }
                            }
                            Caption { text: "Choose a theme from the header. HUD preferences arrive in a later stage."; Layout.fillWidth: true }
                        }
                        Item { Layout.preferredHeight: 20 }
                    }
                }
            }
            TapHandler {
                onTapped: function(eventPoint) {
                    if (preferences.compactSidebar && sidebarRevealed && eventPoint.position.x > sidebarWidth + 10)
                        closeSidebar();
                }
            }
            Timer {
                id: hideTimer
                interval: 160
                onTriggered: if (sidebarReveal === "mouse") closeSidebar()
            }
            Rectangle {
                id: sidebar
                objectName: "sequenceSidebar"
                parent: window.contentItem
                z: 10
                readonly property real inset: preferences.compactSidebar ? 10 : 0
                width: sidebarWidth
                y: preferences.compactSidebar ? inset : workspace.y
                height: preferences.compactSidebar ? window.height - 2 * inset : workspace.height
                visible: page === "sequences"
                x: sidebarOpen ? inset : -width - 12
                enabled: sidebarOpen
                color: preferences.compactSidebar
                       ? Qt.rgba(appTheme.surface.r, appTheme.surface.g, appTheme.surface.b, 0.94)
                       : appTheme.background
                radius: preferences.compactSidebar ? 12 : 0
                border.width: preferences.compactSidebar ? 1 : 0
                border.color: appTheme.border
                clip: true
                Behavior on x { NumberAnimation { duration: appTheme.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic } }
                // Empty drawer space must not click or scroll the editor underneath.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.AllButtons
                    onWheel: function(wheel) { wheel.accepted = true }
                }
                Rectangle { anchors.right: parent.right; height: parent.height; width: 1; color: appTheme.border; visible: !preferences.compactSidebar }
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        QuietButton {
                            objectName: "sidebarToggle"
                            theme: appTheme; transparentIdle: preferences.compactSidebar; text: preferences.compactSidebar ? "›" : "‹"; subtle: true
                            implicitWidth: 28; implicitHeight: 28
                            Accessible.name: preferences.compactSidebar ? "Pin sequence list open" : "Enable compact sequence list"
                            contentItem: Item {
                                implicitWidth: 16; implicitHeight: 14
                                Rectangle {
                                    anchors.centerIn: parent; width: 16; height: 13; radius: 2
                                    color: "transparent"; border.color: appTheme.muted
                                    Rectangle { x: 4; y: 1; width: 1; height: 11; color: appTheme.muted }
                                }
                            }
                            onClicked: { preferences.compactSidebar = !preferences.compactSidebar; closeSidebar() }
                        }
                        Caption { text: "sequences"; Layout.fillWidth: true }
                    }
                    Repeater {
                        model: ["Utopia", "Bridge practice", "Custom sequence"]
                        QuietButton {
                            required property string modelData
                            theme: appTheme; transparentIdle: preferences.compactSidebar; text: modelData; subtle: true; selected: sequenceName === modelData
                            alignLeft: true
                            Layout.fillWidth: true
                            onClicked: selectSequence(modelData)
                        }
                    }
                    Rule { Layout.topMargin: 12; Layout.bottomMargin: 8 }
                    QuietButton { objectName: "newSequenceButton"; theme: appTheme; transparentIdle: preferences.compactSidebar; text: "+ New sequence"; subtle: true; alignLeft: true; Layout.fillWidth: true; onClicked: inform("Sequence creation comes with the real preset connection.") }
                    Item { Layout.fillHeight: true }
                }
            }
        }
        Rule { }
        RowLayout {
            Layout.fillWidth: true; Layout.margins: 24
            spacing: 20
            ColumnLayout {
                Layout.fillWidth: true
                Label { text: running ? "● Running preview" : "● Stopped"; color: running ? appTheme.good : appTheme.text; font.pixelSize: 18 * appTheme.scale }
                Caption { text: running ? steps.get(activeStep).name + " · expected ~" + previewPing + " ms" : "UI prototype · no network traffic is changed"; Layout.fillWidth: true }
            }
            QuietButton { theme: appTheme; text: "Next  F8"; subtle: true; visible: running; onClicked: advance() }
            QuietButton { objectName: "startButton"; theme: appTheme; text: running ? "■  Stop   F7" : "▷  Start   F7"; implicitWidth: 180 * appTheme.scale; onClicked: toggleRunning() }
        }
        Caption { text: notice; visible: notice !== ""; Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.bottomMargin: 12; color: appTheme.accent }
        Rule { visible: preferences.showHints }
        RowLayout {
            visible: preferences.showHints
            Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.preferredHeight: 42 * appTheme.scale
            Caption { text: Catalog.find(appTheme.activeId).name + " · UI preview" }
            Item { Layout.fillWidth: true }
            Caption { text: "F7 start/stop    F8 next    Ctrl+B sequences" }
        }
    }
    // Observe hover independently of buttons and resize handles. The trigger runs
    // the full window height, including the header/footer and the inset gap.
    Item {
        anchors.fill: parent
        z: 30
        HoverHandler {
            id: sidebarPointer
            blocking: false
            onPointChanged: {
                if (themePicker.visible || window.page !== "sequences" || !preferences.compactSidebar) return;
                const px = point.position.x;
                if (px <= 16) {
                    hideTimer.stop();
                    if (!sidebarRevealed) sidebarReveal = "mouse";
                } else if (sidebarRevealed && px <= sidebarWidth + 18) {
                    // The gap and rounded corners belong to the hover corridor.
                    hideTimer.stop();
                } else if (sidebarReveal === "mouse" && !hideTimer.running) {
                    hideTimer.start();
                }
            }
            onHoveredChanged: {
                if (!hovered && sidebarReveal === "mouse") hideTimer.restart();
            }
        }
    }
    Repeater {
        model: [Qt.LeftEdge, Qt.RightEdge, Qt.TopEdge, Qt.BottomEdge,
                Qt.LeftEdge | Qt.TopEdge, Qt.RightEdge | Qt.TopEdge,
                Qt.LeftEdge | Qt.BottomEdge, Qt.RightEdge | Qt.BottomEdge]
        MouseArea {
            required property int modelData
            readonly property bool edgeLeft: (modelData & Qt.LeftEdge) !== 0
            readonly property bool edgeRight: (modelData & Qt.RightEdge) !== 0
            readonly property bool edgeTop: (modelData & Qt.TopEdge) !== 0
            readonly property bool edgeBottom: (modelData & Qt.BottomEdge) !== 0
            readonly property bool corner: (edgeLeft || edgeRight) && (edgeTop || edgeBottom)
            z: 20
            visible: window.visibility !== Window.Maximized && window.visibility !== Window.FullScreen
            x: edgeRight ? window.width - width : edgeLeft ? 0 : 6
            y: edgeBottom ? window.height - height : edgeTop ? 0 : 6
            width: edgeLeft || edgeRight ? 6 : window.width - 12
            height: edgeTop || edgeBottom ? 6 : window.height - 12
            cursorShape: corner ? (edgeLeft === edgeTop ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor)
                                : edgeLeft || edgeRight ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: window.startSystemResize(modelData)
        }
    }

}
