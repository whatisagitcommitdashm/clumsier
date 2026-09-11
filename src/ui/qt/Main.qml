import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
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
    title: backend ? "Clumsier" : "Clumsier — UI prototype"
    property var backend: null
    readonly property bool inputPaused: switchDialog.visible || unsavedDialog.visible || themePicker.visible || (active && (page === "hotkeys" || !!activeFocusItem && (activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit))) || (liveLoader.item ? liveLoader.item.dialogOpen : false)
    onInputPausedChanged: if (backend) backend.setInputPaused(inputPaused)
    onClosing: function(event) {
        if (backend && backend.dirty) {
            event.accepted = false;
            backend.execute(1);
            requestSwitch("close", "");
        } else {
            hud.shuttingDown = true;
            hud.hide();
            // Finish the editor's close event before the application-wide quit.
            Qt.callLater(Qt.quit);
        }
    }
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
        property bool skipSwitchWarning: false
        property bool advancedMode: false
        property bool showHud: false
        property real hudScale: 1
        property real hudOpacity: 0.85
        property bool hudControls: true
    }
    function saveHudPreference(key, value) {
        preferences[key] = value;
        preferences.setValue(key, value); preferences.sync();
    }
    Hud {
        id: hud; bridge: window.backend; paletteTheme: appTheme
        visible: !!window.backend && preferences.showHud
        scaleFactor: preferences.hudScale; backgroundOpacity: preferences.hudOpacity
        showControls: preferences.hudControls; controlsEnabled: !window.inputPaused
        onHideRequested: window.saveHudPreference("showHud", false)
    }
    property url settingsLocation: ""
    readonly property real interfaceScale: preferences.uiScale
    property string page: "sequences"
    property string pendingSwitch: ""
    property string pendingValue: ""
    readonly property bool switchingActivity: switchDialog.visible
    function clearPendingSwitch() { pendingSwitch = ""; pendingValue = ""; }
    function requestSwitch(kind, value) {
        if (!backend) { if (kind === "page") page = value; return; }
        if (switchDialog.visible || unsavedDialog.visible) return;
        if (kind === "page" && value === page) return;
        if (kind === "sequence" && value === backend.selectedId && backend.state.activity === "sequences") return;
        pendingSwitch = kind; pendingValue = value;
        closeSidebar();
        // Resolve the draft before changing pages or replacing the selection.
        // Entering the editor itself does not discard or replace anything.
        if (backend.dirty && kind !== "edit") unsavedDialog.open();
        else continueSwitch();
    }
    function continueSwitch() {
        const changesActivity = pendingSwitch !== "page"
            || (pendingValue !== "hotkeys" && pendingValue !== "settings" && pendingValue !== backend.state.activity);
        if (backend.state.running && changesActivity && !preferences.skipSwitchWarning) {
            suppressSwitchWarning.checked = false;
            switchDialog.open();
        } else completeSwitch();
    }
    function completeSwitch() {
        const kind = pendingSwitch, value = pendingValue;
        let changed = false;
        if (kind === "page") changed = (value === "hotkeys" || value === "settings") || backend.switchActivity(value);
        else if (kind === "sequence") changed = backend.selectPreset(value);
        else if (kind === "new") changed = backend.newPreset();
        else if (kind === "duplicate") changed = backend.duplicate();
        else if (kind === "import") changed = backend.importPreset(value);
        else if (kind === "edit" || kind === "delete") {
            if (backend.execute(1) && liveLoader.item) {
                if (kind === "edit") liveLoader.item.beginEditing(value);
                else liveLoader.item.confirmDeleteSequence();
            }
        }
        clearPendingSwitch();
        if (changed) { page = kind === "page" ? value : "sequences"; closeSidebar(); }
        if (kind === "close") window.close();
    }
    QuietDialog {
        id: unsavedDialog; theme: appTheme; parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(490 * appTheme.scale, parent.width - 32); modal: true
        title: "Save your changes?"; closePolicy: Popup.CloseOnEscape
        onRejected: window.clearPendingSwitch()
        contentItem: ColumnLayout {
            spacing: 16
            Caption { Layout.fillWidth: true; text: "This sequence has unsaved changes. Save them, discard them, or stay here to keep editing." }
            Caption { Layout.fillWidth: true; text: backend ? backend.error : ""; visible: text !== ""; color: appTheme.accent }
            RowLayout {
                Item { Layout.fillWidth: true }
                QuietButton { objectName: "cancelUnsavedSwitch"; theme: appTheme; text: "Cancel"; onClicked: unsavedDialog.reject() }
                QuietButton { objectName: "discardUnsavedSwitch"; theme: appTheme; text: "Discard"; onClicked: { backend.discard(); unsavedDialog.close(); window.continueSwitch(); } }
                QuietButton { objectName: "saveUnsavedSwitch"; theme: appTheme; text: "Save"; onClicked: { if (backend.save()) { unsavedDialog.close(); window.continueSwitch(); } } }
            }
        }
    }
    QuietDialog {
        theme: appTheme
        id: switchDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(470 * appTheme.scale, parent.width - 32)
        modal: true
        title: pendingSwitch === "edit" ? "Stop and edit?" : "Stop and switch?"
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: appTheme.surface; radius: 8; border.color: appTheme.border }
        onRejected: { pendingSwitch = ""; pendingValue = ""; }
        contentItem: ColumnLayout {
            spacing: 18
            Caption {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                text: pendingSwitch === "edit" ? "Editing will stop the current network delay. Save your changes, then press Start when you’re ready to use the sequence." : "Switching will stop the current network delay and select the new controls or sequence. Press Start when you’re ready to use it."
            }
            QuietCheckBox { theme: appTheme; id: suppressSwitchWarning; objectName: "skipSwitchWarning"; text: "Don’t show this message again"; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignRight
                Item { Layout.fillWidth: true }
                QuietButton { objectName: "cancelActivitySwitch"; theme: appTheme; text: "Cancel"; onClicked: switchDialog.reject() }
                QuietButton {
                    objectName: "confirmActivitySwitch"; theme: appTheme; text: "Continue"
                    onClicked: {
                        if (suppressSwitchWarning.checked) {
                            preferences.skipSwitchWarning = true;
                            preferences.setValue("skipSwitchWarning", true); preferences.sync();
                        }
                        completeSwitch(); switchDialog.close();
                    }
                }
            }
        }
    }
    onActiveChanged: if (!active) { closeSidebar(); if (backend) backend.cancelRecording(); }
    // Mouse peeks ignore incidental focus from clicks; keyboard opens stay until dismissed.
    property string sidebarReveal: "closed"
    readonly property bool sidebarRevealed: sidebarReveal !== "closed"
    readonly property bool sidebarOpen: !preferences.compactSidebar || sidebarRevealed
    readonly property real sidebarWidth: 210 * appTheme.scale
    property int selectedStep: 0
    property int activeStep: 0
    property bool running: backend ? backend.state.running : false
    property bool wrapSteps: true
    property string sequenceName: "Utopia"
    property int baselineMs: 40
    property string notice: ""
    property int previewPing: 200
    onSelectedStepChanged: editorFade.restart()
    onPageChanged: {
        if (backend && page !== "hotkeys") backend.cancelRecording();
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
        if (backend) { backend.execute(2); return; }
        running = !running;
        if (running) { activeStep = selectedStep; previewPing = expected(activeStep) }
    }
    function expected(index) {
        let step = steps.get(index);
        return step.lowest ? baselineMs : Math.max(baselineMs, step.target);
    }
    function advance() {
        if (backend) { backend.execute(3); return; }
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
    Shortcut { enabled: !backend && !themePicker.visible; sequence: "F7"; autoRepeat: false; onActivated: toggleRunning() }
    Shortcut { enabled: !backend && !themePicker.visible; sequence: "F8"; autoRepeat: false; onActivated: advance() }
    Shortcut {
        enabled: !themePicker.visible && !switchDialog.visible && !unsavedDialog.visible && !(liveLoader.item && liveLoader.item.dialogOpen)
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
    Shortcut { enabled: !themePicker.visible && !switchDialog.visible && !unsavedDialog.visible && !(liveLoader.item && liveLoader.item.dialogOpen); sequence: "Escape"; onActivated: closeSidebar() }

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
                    onClicked: requestSwitch("page", modelData)
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
                    onClicked: requestSwitch("page", "settings")
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
                        Loader {
                            id: liveLoader
                            active: !!backend
                            visible: active && page !== "settings"
                            Layout.fillWidth: true; Layout.leftMargin: 32; Layout.rightMargin: 32
                            sourceComponent: LiveWorkspace {
                                bridge: window.backend; theme: appTheme; page: window.page; advancedMode: preferences.advancedMode
                                onSwitchRequested: function(kind, value) { window.requestSwitch(kind, value); }
                            }
                        }
                        ColumnLayout {
                            visible: !backend && page === "sequences"
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
                                        delegate: QuietButton {
                                            id: stepRow
                                            theme: appTheme
                                            required property int index
                                            required property string name
                                            required property int target
                                            required property bool lowest
                                            objectName: "step" + index
                                            Layout.fillWidth: true
                                            implicitHeight: 58 * appTheme.scale
                                            Accessible.name: name + ", " + (lowest ? "Lowest available" : target + " milliseconds")
                                            onClicked: selectedStep = index
                                            background: Rectangle {
                                                radius: 4
                                                color: selectedStep === stepRow.index ? appTheme.selected : stepRow.pointerInside ? appTheme.hover : appTheme.background
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
                            visible: !backend && page === "quick controls"
                            Layout.fillWidth: true; Layout.leftMargin: 32; Layout.rightMargin: 32
                            spacing: 22
                            Heading { text: "Delay" }
                            Caption { text: "Try the controls. This preview does not touch network traffic."; Layout.fillWidth: true }
                            Caption { text: "added delay" }
                            Heading { text: Math.round(delaySlider.value) + " ms" }
                            Slider { id: delaySlider; Layout.fillWidth: true; from: 0; to: 500; value: 160; stepSize: 10; Accessible.name: "Sample added delay" }
                        }
                        ColumnLayout {
                            visible: !backend && page === "hotkeys"
                            Layout.fillWidth: true; Layout.leftMargin: 32; Layout.rightMargin: 32
                            spacing: 22
                            Heading { text: "Hotkeys"; Layout.fillWidth: true }
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
                            QuietCheckBox {
                                objectName: "advancedModeSetting"; theme: appTheme; text: "Advanced mode"; checked: preferences.advancedMode
                                onToggled: { preferences.advancedMode = checked; preferences.setValue("advancedMode", checked); preferences.sync(); }
                            }
                            QuietCheckBox {
                                objectName: "switchWarningSetting"; theme: appTheme; text: "Confirm before switching a running activity"; checked: !preferences.skipSwitchWarning
                                onToggled: { preferences.skipSwitchWarning = !checked; preferences.setValue("skipSwitchWarning", !checked); preferences.sync(); }
                            }
                            Caption { text: "interface size" }
                            RowLayout {
                                Repeater {
                                    model: [1, 1.15, 1.3]
                                    QuietButton { required property real modelData; objectName: "scale-" + Math.round(modelData * 100); theme: appTheme; text: Math.round(modelData * 100) + "%"; selected: preferences.uiScale === modelData; onClicked: preferences.uiScale = modelData }
                                }
                            }
                            Heading { text: "HUD"; visible: !!backend }
                            QuietCheckBox { objectName: "showHudSetting"; visible: !!backend; theme: appTheme; text: "Show always-on-top HUD"; checked: preferences.showHud; onToggled: window.saveHudPreference("showHud", checked) }
                            Caption { visible: !!backend; text: "Drag the HUD by its Running / Stopped label. It stays visible when the editor is minimized."; Layout.fillWidth: true }
                            Flow {
                                visible: !!backend; Layout.fillWidth: true; spacing: 8
                                Repeater {
                                    model: [0.75, 1, 1.25, 1.5]
                                    QuietButton { required property real modelData; theme: appTheme; text: "Size " + Math.round(modelData * 100) + "%"; selected: preferences.hudScale === modelData; onClicked: window.saveHudPreference("hudScale", modelData) }
                                }
                            }
                            Flow {
                                visible: !!backend; Layout.fillWidth: true; spacing: 8
                                Repeater {
                                    model: [0.25, 0.5, 0.85, 1]
                                    QuietButton { required property real modelData; theme: appTheme; text: "Background " + Math.round(modelData * 100) + "%"; selected: preferences.hudOpacity === modelData; onClicked: window.saveHudPreference("hudOpacity", modelData) }
                                }
                            }
                            QuietCheckBox { visible: !!backend; theme: appTheme; text: "Show HUD playback controls"; checked: preferences.hudControls; onToggled: window.saveHudPreference("hudControls", checked) }
                            QuietButton { visible: !!backend; theme: appTheme; text: "Bring HUD here"; onClicked: { hud.x = window.x + 40; hud.y = window.y + 100; window.saveHudPreference("showHud", true); } }
                            Caption { text: "Choose a theme from the header. The HUD follows the same theme."; Layout.fillWidth: true }
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
                        model: backend ? backend.presets : ["Utopia", "Bridge practice", "Custom sequence"]
                        QuietButton {
                            required property var modelData
                            theme: appTheme; transparentIdle: preferences.compactSidebar; text: backend ? modelData.name : modelData; subtle: true; selected: backend ? backend.selectedId === modelData.id : sequenceName === modelData
                            alignLeft: true
                            Layout.fillWidth: true
                            onClicked: { if (backend) requestSwitch("sequence", modelData.id); else selectSequence(modelData); }
                        }
                    }
                    Rule { Layout.topMargin: 12; Layout.bottomMargin: 8 }
                    QuietButton { objectName: "newSequenceButton"; theme: appTheme; transparentIdle: preferences.compactSidebar; text: "+ New sequence"; subtle: true; alignLeft: true; Layout.fillWidth: true; onClicked: { if (backend) requestSwitch("new", ""); else inform("Sequence creation comes with the real preset connection."); } }
                    Item { Layout.fillHeight: true }
                }
            }
        }
        Rule { }
        GridLayout {
            columns: window.width > 1000 * appTheme.scale ? 2 : 1
            Layout.fillWidth: true; Layout.margins: 24
            columnSpacing: 20; rowSpacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                Label { text: running ? (backend ? "● Running" : "● Running preview") : "● Stopped"; color: running ? appTheme.good : appTheme.text; font.pixelSize: 18 * appTheme.scale }
                Caption { text: backend ? backend.state.description + (backend.state.targetPing ? " · expected ~" + backend.state.expected + " ms" : "") + (backend.state.belowBaseline ? " · target below your normal ping" : "") : running ? steps.get(activeStep).name + " · expected ~" + previewPing + " ms" : "UI prototype · no network traffic is changed"; Layout.fillWidth: true }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                readonly property bool sequenceControls: backend && backend.state.sequence
                QuietButton { objectName: "showHudButton"; theme: appTheme; text: "HUD"; subtle: true; visible: !!backend; selected: preferences.showHud; onClicked: window.saveHudPreference("showHud", !preferences.showHud) }
                QuietButton { objectName: "previousPlaybackButton"; theme: appTheme; text: "Previous"; subtle: true; visible: parent.sequenceControls; enabled: !backend || !backend.dirty; onClicked: backend.execute(4) }
                QuietButton { objectName: "resetPlaybackButton"; theme: appTheme; text: "Reset"; subtle: true; visible: parent.sequenceControls; enabled: !backend || !backend.dirty; onClicked: backend.execute(5) }
                QuietButton { objectName: "nextPlaybackButton"; theme: appTheme; text: backend ? "Next" : "Next  F8"; subtle: true; visible: parent.sequenceControls || (!backend && running); enabled: !backend || !backend.dirty; onClicked: advance() }
                QuietButton {
                    id: captureButton; objectName: "startButton"; theme: appTheme
                    enabled: !backend || running || backend.state.canStart
                    text: running ? "Stop" : "Start"
                    implicitWidth: 180 * appTheme.scale
                    onClicked: toggleRunning()
                    contentItem: RowLayout {
                        spacing: 10 * appTheme.scale
                        opacity: captureButton.enabled ? 1 : 0.5
                        Item {
                            Layout.preferredWidth: 16 * appTheme.scale
                            Layout.preferredHeight: 16 * appTheme.scale
                            // Vector outlines keep the weight and alignment
                            // consistent across fonts and interface sizes.
                            Shape {
                                anchors.centerIn: parent; width: 16; height: 16; scale: appTheme.scale
                                preferredRendererType: Shape.CurveRenderer
                                ShapePath {
                                    strokeColor: appTheme.text; strokeWidth: 1.5
                                    fillColor: "transparent"; joinStyle: ShapePath.RoundJoin
                                    PathSvg { path: running ? "M 5 3 H 11 Q 13 3 13 5 V 11 Q 13 13 11 13 H 5 Q 3 13 3 11 V 5 Q 3 3 5 3 Z" : "M 4 2.5 L 13 8 L 4 13.5 Z" }
                                }
                            }
                        }
                        Label { text: captureButton.text; font: captureButton.font; color: appTheme.text }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: backend ? (backend.bindings.length > 2 ? backend.bindings[2].text : "") : "F7"
                            font: captureButton.font; color: appTheme.muted
                            Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight
                        }
                    }
                }
            }
        }
        Caption { text: backend ? backend.error : ""; visible: backend && backend.error !== ""; Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; color: appTheme.accent }
        Caption { text: notice; visible: notice !== ""; Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.bottomMargin: 12; color: appTheme.accent }
        Rule { visible: preferences.showHints }
        RowLayout {
            visible: preferences.showHints
            Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.preferredHeight: 42 * appTheme.scale
            Caption { text: Catalog.find(appTheme.activeId).name + " · UI preview" }
            Item { Layout.fillWidth: true }
            Caption { text: backend ? (backend.globalHotkeysAvailable ? "Ctrl+B sequences · Global bindings in Hotkeys" : "Ctrl+B sequences") : "F7 start/stop    F8 next    Ctrl+B sequences" }
        }
    }
    // Observe hover independently of buttons and resize handles. The trigger runs
    // the full window height, including the header/footer and the inset gap.
    HoverHandler {
        // Observe the content directly. A full-window item above the page
        // can intercept hover delivery to the controls underneath it.
        parent: window.contentItem
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
