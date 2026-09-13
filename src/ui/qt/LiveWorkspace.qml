import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: panel
    required property var bridge
    required property var theme
    required property string page
    property int stepIndex: 0
    property bool advancedMode: false
    property bool newSequenceShortcutEnabled: true
    property bool searchShortcutEnabled: true
    readonly property bool localShortcutRecording: searchRecorder.recording || newRecorder.recording
    property string newSequenceShortcut: "Ctrl+T"
    property string searchShortcut: "Ctrl+F"
    signal searchShortcutToggled(bool enabled)
    signal localShortcutChanged(string kind, string shortcut)
    signal newSequenceShortcutToggled(bool enabled)
    property bool trafficExpanded: false
    // The backend polls status frequently. Only accepted value changes should
    // refresh these fields; a status tick must not overwrite typing in progress.
    readonly property int acceptedQuickMs: bridge.state.quickMs
    readonly property int acceptedQuickDirection: bridge.state.quickDirection
    readonly property int playbackStep: bridge.state.sequence ? bridge.state.step : -1
    readonly property bool playing: bridge.state.running
    readonly property string selectedSequenceId: bridge.selectedId
    onPlaybackStepChanged: if (playbackStep >= 0 && playbackStep < steps.length) stepIndex = playbackStep
    onPlayingChanged: if (playing && playbackStep >= 0) stepIndex = playbackStep
    onSelectedSequenceIdChanged: stepIndex = 0
    readonly property var draft: bridge.draft
    readonly property var steps: draft.steps || []
    readonly property var step: steps[stepIndex] || ({})
    readonly property bool dialogOpen: baselineSetup.visible || profileDialog.visible || serverManager.visible || actionsDialog.visible || descriptionDialog.visible || deleteDialog.visible || importer.visible || exporter.visible
    property var deferredServers: []
    function offerBaselineSetup() {
        if (baselineSetup.visible) return;
        const name = bridge.missingServers.find(function(name) { return !deferredServers.includes(name); });
        if (!name) return;
        baselineSetup.serverName = name;
        setupPing.text = "";
        baselineSetup.open();
        Qt.callLater(function() { setupPing.forceActiveFocus(); });
    }
    Component.onCompleted: Qt.callLater(offerBaselineSetup)
    property string deleteKind: ""
    property string editingProfile: ""
    signal switchRequested(string kind, string value)
    readonly property var server: bridge.profiles.find(function(p) { return p.id === bridge.profileId; }) || null
    function beginEditing(destination) {
        if (destination === "servers") serverManager.open();
        else sequenceName.forceActiveFocus();
    }
    function stepValue(s) {
        if (s.type === "lowest") return "Lowest available";
        if (s.type === "target") return s.target_ms + " ms";
        if (draft.policy === "inbound") return s.inbound_ms + " ms";
        if (draft.policy === "outbound") return s.outbound_ms + " ms";
        return s.inbound_ms + " / " + s.outbound_ms + " ms";
    }
    function estimate() {
        if (step.type === "delay") return "Added delay · " + stepValue(step);
        if (!server) return "Choose a server to estimate the added delay.";
        if (step.type === "lowest") return "No added delay · Expected ~" + server.baseline + " ms";
        const difference = Math.max(0, Number(step.target_ms) - server.baseline);
        return "Target " + step.target_ms + " ms · Adds " + difference + " ms"
            + (Number(step.target_ms) < server.baseline ? " · Expected ~" + server.baseline + " ms (your baseline)" : "");
    }
    spacing: 18 * theme.scale
    function edit(key, value, activeStep) {
        let copy = JSON.parse(JSON.stringify(draft));
        copy[key] = value;
        const accepted = bridge.commitDraft(copy, activeStep === undefined ? -1 : activeStep);
        if (accepted && playing) stepIndex = bridge.state.step;
        return accepted;
    }
    function editStep(key, value) {
        let copy = JSON.parse(JSON.stringify(steps));
        copy[stepIndex][key] = value;
        return edit("steps", copy);
    }
    function stepType(lowest) {
        let copy = JSON.parse(JSON.stringify(steps));
        copy[stepIndex] = {name: step.name, note: step.note, type: lowest ? "lowest" : "target"};
        if (!lowest) copy[stepIndex].target_ms = 0;
        return edit("steps", copy);
    }
    function changeMode(mode) {
        let copy = JSON.parse(JSON.stringify(draft));
        copy.mode = mode;
        copy.steps = steps.map(function(s) {
            let result = {name: s.name, note: s.note, type: mode === "target_ping" ? "target" : "delay"};
            if (mode === "target_ping") result.target_ms = 0;
            else { result.inbound_ms = 0; result.outbound_ms = 0; }
            return result;
        });
        return bridge.commitDraft(copy);
    }
    function moveStep(delta) {
        let copy = JSON.parse(JSON.stringify(steps));
        let next = stepIndex + delta;
        if (next < 0 || next >= copy.length) return;
        const item = copy.splice(stepIndex, 1)[0]; copy.splice(next, 0, item);
        if (edit("steps", copy, next)) stepIndex = next;
    }
    function openProfile(id, name, baseline) {
        editingProfile = id; profileName.text = name; profilePing.value = baseline; profilePing.restore(); profileDialog.open();
    }
    Connections {
        target: panel.bridge
        function onLibraryChanged() { Qt.callLater(panel.offerBaselineSetup); }
        function onDraftChanged() { if (panel.stepIndex >= panel.steps.length) panel.stepIndex = Math.max(0, panel.steps.length - 1); }
    }
    component Copy: Label { color: panel.theme.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    component Title: Label { color: panel.theme.text; font.pixelSize: 26 * panel.theme.scale; Layout.fillWidth: true; elide: Text.ElideRight }
    component Choice: QuietChoice { theme: panel.theme; Layout.fillWidth: true }

    ColumnLayout {
        visible: panel.page === "sequences"
        Layout.fillWidth: true; spacing: 16
        RowLayout {
            Layout.fillWidth: true
            QuietField {
                id: sequenceName; objectName: "sequenceNameField"; theme: panel.theme
                Layout.fillWidth: true; visible: panel.steps.length > 0
                Layout.minimumWidth: 0
                Layout.preferredWidth: nameMetrics.advanceWidth + 12 * panel.theme.scale
                Layout.maximumWidth: Layout.preferredWidth
                TextMetrics { id: nameMetrics; text: sequenceName.text || sequenceName.placeholderText; font: sequenceName.font }
                text: panel.draft.name || ""; placeholderText: "Sequence name"
                font.pixelSize: 26 * panel.theme.scale
                leftPadding: 0; rightPadding: 8
                HoverHandler { id: nameHover; blocking: false; cursorShape: sequenceName.readOnly ? Qt.ArrowCursor : Qt.IBeamCursor }
                background: Rectangle {
                    color: nameHover.hovered && !sequenceName.readOnly ? panel.theme.hover : "transparent"; radius: 5
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: panel.theme.accent; visible: sequenceName.activeFocus || (nameHover.hovered && !sequenceName.readOnly) }
                }
                onEditingFinished: if (!panel.edit("name", text)) text = panel.draft.name || ""
            }
            QuietButton { objectName: "renameSequenceButton"; theme: panel.theme; text: "Rename"; subtle: true; visible: panel.steps.length > 0; onClicked: { sequenceName.forceActiveFocus(); sequenceName.selectAll(); } }
            Item { Layout.fillWidth: true }
            Title { visible: !panel.steps.length; text: "No sequence selected" }
            Label { objectName: "saveStatus"; text: bridge.dirty ? (bridge.autoSave ? "Saving changes…" : "Unsaved") : ""; color: panel.theme.accent; font.pixelSize: 11 * panel.theme.scale; Layout.preferredWidth: 140 * panel.theme.scale }
            QuietButton { objectName: "sequenceActionsButton"; theme: panel.theme; text: "···"; Accessible.name: "Sequence actions"; subtle: true; onClicked: actionsDialog.open() }
        }
        RowLayout {
            visible: bridge.dirty && !bridge.autoSave; Layout.fillWidth: true
            Copy { text: bridge.autoSave ? (bridge.error ? "Not saved — check the entry below" : "Saving changes…") : "Unsaved changes"; color: panel.theme.accent }
            QuietButton { objectName: "saveSequenceButton"; theme: panel.theme; text: "Save"; onClicked: bridge.save() }
            QuietButton { objectName: "discardSequenceButton"; theme: panel.theme; text: "Discard"; onClicked: bridge.discard() }
        }
        Copy { visible: !panel.steps.length; text: "Choose a sequence from the sidebar, create one, or import a shared JSON file." }
        RowLayout {
            visible: panel.steps.length > 0; Layout.fillWidth: true
            Choice {
                objectName: "sequenceServerSelector"
                model: [{id: "", name: "Choose a server"}].concat(bridge.profiles)
                textRole: "name"; currentIndex: model.findIndex(function(p) { return p.id === bridge.profileId; })
                onActivated: if (!bridge.selectProfile(model[index].id)) currentIndex = model.findIndex(function(p) { return p.id === bridge.profileId; })
            }
            Label { text: panel.server ? panel.server.baseline + " ms baseline" : ""; color: panel.theme.muted }
            QuietButton {
                objectName: "manageServersButton"; theme: panel.theme; text: "Manage servers"; subtle: true
                onClicked: serverManager.open()
            }
        }
        Copy { visible: panel.playing; text: "Click a step to use it now. Field changes apply when you finish editing." }
        ColumnLayout {
            visible: panel.steps.length > 0
            Layout.fillWidth: true; spacing: 16
            RowLayout {
                visible: true
                Choice { model: ["Target ping", "Added delay"]; currentIndex: panel.draft.mode === "target_ping" ? 0 : 1; onActivated: panel.changeMode(index === 0 ? "target_ping" : "added_delay") }
                Copy { text: "Changing mode resets step values to zero." }
            }
            GridLayout {
                columns: panel.width > 800 * panel.theme.scale ? 2 : 1
                Layout.fillWidth: true; columnSpacing: 24; rowSpacing: 16
                ColumnLayout {
                    Layout.fillWidth: true; Layout.preferredWidth: 380; Layout.alignment: Qt.AlignTop
                    Repeater {
                        model: panel.steps
                        QuietButton {
                            required property var modelData
                            required property int index
                            objectName: "liveStep" + index
                            theme: panel.theme; Layout.fillWidth: true; alignLeft: true; subtle: true; selected: index === panel.stepIndex
                            id: stepRow
                            text: modelData.name + ", " + panel.stepValue(modelData)
                            implicitHeight: 52 * panel.theme.scale
                            contentItem: RowLayout {
                                spacing: 12
                                Label { text: String(stepRow.index + 1).padStart(2, "0"); color: panel.theme.muted; Layout.preferredWidth: 24 * panel.theme.scale }
                                Label { text: stepRow.modelData.name; color: stepRow.selected ? panel.theme.accent : panel.theme.text; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { text: panel.stepValue(stepRow.modelData); color: stepRow.selected ? panel.theme.accent : panel.theme.text; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 150 * panel.theme.scale; font.pixelSize: 12 * panel.theme.scale }
                            }
                            onClicked: { if (!panel.playing || bridge.selectStep(index)) panel.stepIndex = index; }
                        }
                    }
                    RowLayout {
                        visible: true
                        QuietButton { theme: panel.theme; text: "+ Step"; enabled: panel.steps.length < 64; onClicked: {
                            let copy = JSON.parse(JSON.stringify(panel.steps));
                            let s = {name: "Step " + (copy.length + 1), note: "", type: panel.draft.mode === "target_ping" ? "target" : "delay"};
                            if (s.type === "target") s.target_ms = 0; else { s.inbound_ms = 0; s.outbound_ms = 0; }
                            copy.push(s); if (panel.edit("steps", copy, copy.length - 1)) panel.stepIndex = copy.length - 1;
                        } }
                        QuietButton { theme: panel.theme; text: "↑"; enabled: panel.stepIndex > 0; onClicked: panel.moveStep(-1) }
                        QuietButton { theme: panel.theme; text: "↓"; enabled: panel.stepIndex + 1 < panel.steps.length; onClicked: panel.moveStep(1) }
                        QuietButton { theme: panel.theme; text: "Remove"; enabled: panel.steps.length > 1; onClicked: { let copy = JSON.parse(JSON.stringify(panel.steps)); copy.splice(panel.stepIndex, 1); panel.edit("steps", copy); } }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.preferredWidth: 300; Layout.alignment: Qt.AlignTop
                    Copy { text: (panel.playing ? "Step " : "Edit step ") + (panel.stepIndex + 1) }
                    QuietField { theme: panel.theme; Layout.fillWidth: true; text: panel.step.name || ""; placeholderText: "Step name"; onEditingFinished: if (!panel.editStep("name", text)) text = panel.step.name || "" }
                    QuietField { theme: panel.theme; Layout.fillWidth: true; text: panel.step.note || ""; placeholderText: "Note (optional)"; onEditingFinished: if (!panel.editStep("note", text)) text = panel.step.note || "" }
                    RowLayout {
                        visible: panel.draft.mode === "target_ping"
                        QuietButton { theme: panel.theme; text: "Target ping"; selected: panel.step.type === "target"; onClicked: panel.stepType(false) }
                        QuietButton { theme: panel.theme; text: "Lowest available"; selected: panel.step.type === "lowest"; onClicked: panel.stepType(true) }
                    }
                    QuietNumber { objectName: "liveTargetField"; theme: panel.theme; Layout.fillWidth: true; visible: panel.step.type === "target"; value: Number(panel.step.target_ms || 0); maximum: 60000; onInvalidEntry: bridge.invalidEntry(); onValueCommitted: function(number) { panel.editStep("target_ms", number); } }
                    Copy { objectName: "stepEstimate"; text: panel.estimate(); color: panel.theme.text }
                    Copy { visible: panel.step.type === "lowest"; text: "No added delay. Your connection keeps its normal ping." }
                    Copy { visible: panel.step.type === "delay"; text: "Inbound / outbound delay (ms)" }
                    RowLayout {
                        visible: panel.step.type === "delay"
                        QuietNumber { theme: panel.theme; Layout.fillWidth: true; enabled: panel.draft.policy !== "outbound"; value: Number(panel.step.inbound_ms || 0); onInvalidEntry: bridge.invalidEntry(); onValueCommitted: function(number) { panel.editStep("inbound_ms", number); } }
                        QuietNumber { theme: panel.theme; Layout.fillWidth: true; enabled: panel.draft.policy !== "inbound"; value: Number(panel.step.outbound_ms || 0); onInvalidEntry: bridge.invalidEntry(); onValueCommitted: function(number) { panel.editStep("outbound_ms", number); } }
                    }
                }
            }
            QuietButton { objectName: "trafficSettingsButton"; visible: panel.advancedMode; theme: panel.theme; text: trafficOptions.visible ? "Hide traffic settings" : "Traffic settings"; onClicked: panel.trafficExpanded = !panel.trafficExpanded }
            ColumnLayout {
                id: trafficOptions; visible: panel.advancedMode && panel.trafficExpanded; Layout.fillWidth: true
                Copy { text: "These settings apply to every step. The defaults delay all incoming traffic." }
                Choice { model: ["Inbound delay", "Outbound delay", "Both directions"]; currentIndex: ["inbound", "outbound", "both"].indexOf(panel.draft.policy); onActivated: {
                    let copy = JSON.parse(JSON.stringify(panel.draft)); copy.policy = ["inbound", "outbound", "both"][index];
                    copy.traffic.direction = index === 2 ? "both" : copy.policy;
                    copy.steps.forEach(function(s) { if (s.type === "delay") { if (index === 0) s.outbound_ms = 0; if (index === 1) s.inbound_ms = 0; } }); bridge.commitDraft(copy);
                } }
                Repeater {
                    model: [{key:"protocol", choices:["any","tcp","udp"]}, {key:"direction", choices:["both","inbound","outbound"]}]
                    Choice { required property var modelData; model: modelData.choices; currentIndex: model.indexOf((panel.draft.traffic || {})[modelData.key]); onActivated: { let t = Object.assign({}, panel.draft.traffic); t[modelData.key] = model[index]; panel.edit("traffic", t); } }
                }
                Repeater {
                    model: [{key:"remote_address", label:"Remote IP (blank for any)"}, {key:"remote_port", label:"Remote port (0 for any)"}, {key:"native_filter", label:"Windows filter (optional)"}]
                    QuietField { required property var modelData; theme: panel.theme; Layout.fillWidth: true; placeholderText: modelData.label; text: String((panel.draft.traffic || {})[modelData.key] || (modelData.key === "remote_port" ? 0 : "")); onActiveFocusChanged: if (activeFocus && modelData.key === "remote_port" && text === "0") Qt.callLater(selectAll); onEditingFinished: {
                        if (modelData.key === "remote_port" && !/^[0-9]*$/.test(text)) { bridge.invalidEntry(); text = String(panel.draft.traffic.remote_port || 0); return; }
                        let t = Object.assign({}, panel.draft.traffic); t[modelData.key] = modelData.key === "remote_port" ? Number(text) : text;
                        if (modelData.key === "native_filter") t.native_backend = text ? "windivert" : "";
                        const accepted = panel.edit("traffic", t);
                        if (!accepted || modelData.key === "remote_port") text = String((panel.draft.traffic || {})[modelData.key] || (modelData.key === "remote_port" ? 0 : ""));
                    } }
                }
            }
            QuietCheckBox { theme: panel.theme; text: "Wrap at end"; checked: !!panel.draft.loop; onToggled: panel.edit("loop", checked) }

        }
    }
    ColumnLayout {
        visible: panel.page === "quick controls"; Layout.fillWidth: true; spacing: 18
        Title { text: "Delay" }
        Copy { text: "Enter a delay, then Start. Changes apply when you leave the field, including while running. Zero adds no delay." }
        QuietNumber {
            id: quickMs; objectName: "quickDelayField"; theme: panel.theme; Layout.fillWidth: true
            value: panel.acceptedQuickMs; onInvalidEntry: bridge.invalidEntry()
            onValueCommitted: function(number) { bridge.quickDelay(number, panel.acceptedQuickDirection); }
        }
        Choice {
            id: quickDirection; objectName: "quickDelayDirection"
            model: ["Inbound", "Outbound", "Both directions"]; currentIndex: panel.acceptedQuickDirection
            onActivated: {
                if (!bridge.quickDelay(panel.acceptedQuickMs, index)) currentIndex = panel.acceptedQuickDirection;
            }
        }
        Copy { text: "Accepted delay: " + bridge.state.inbound + " ms inbound / " + bridge.state.outbound + " ms outbound" }
    }
    ColumnLayout {
        visible: panel.page === "hotkeys"; Layout.fillWidth: true; spacing: 18
        Title { text: "Hotkeys" }
        RowLayout {
            visible: bridge.globalHotkeysAvailable; Layout.fillWidth: true
            Copy { text: "Global hotkeys" }
            QuietButton {
                objectName: "hotkeysEnabledToggle"; theme: panel.theme
                text: bridge.hotkeysEnabled ? "Enabled" : "Disabled"; selected: bridge.hotkeysEnabled
                onClicked: bridge.hotkeysEnabled = !bridge.hotkeysEnabled
                helpText: "Turn global shortcuts on or off. Mouse controls always remain available."
            }
        }
        Copy { visible: bridge.globalHotkeysAvailable && !bridge.hotkeysEnabled; text: "Hotkeys are off. You can still use all buttons and edit your bindings." }
        Copy { text: bridge.globalHotkeysAvailable
            ? "Click Record, hold your keyboard or mouse combination, then release. Input still reaches your game. Changes are saved immediately. Global actions pause while you edit text or use this tab."
            : "Global hotkeys are not yet available on Linux. Use the Start, Stop, and sequence playback buttons." }
        Repeater {
            model: bridge.bindings
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Copy { text: modelData.name }
                QuietButton { objectName: "bindingToggle" + modelData.action; theme: panel.theme; text: modelData.enabled ? "Enabled" : "Disabled"; selected: modelData.enabled; onClicked: bridge.setBindingEnabled(modelData.action, !modelData.enabled) }
                QuietButton { theme: panel.theme; text: modelData.text || "Unbound"; onClicked: bridge.recordBinding(modelData.action) }
                QuietButton { theme: panel.theme; text: "Record"; onClicked: bridge.recordBinding(modelData.action) }
                QuietButton { theme: panel.theme; text: "Clear"; onClicked: bridge.saveBinding(modelData.action, "None") }
            }
        }
        Copy { text: "Window shortcuts · Click a shortcut to record a key with optional Ctrl, Alt, Shift, or Meta. Escape cancels." }
        RowLayout {
            Layout.fillWidth: true
            Copy { text: "Search sequences" }
            QuietButton { objectName: "searchShortcutToggle"; theme: panel.theme; text: panel.searchShortcutEnabled ? "Enabled" : "Disabled"; selected: panel.searchShortcutEnabled; onClicked: panel.searchShortcutToggled(!panel.searchShortcutEnabled) }
            ShortcutRecorder { id: searchRecorder; objectName: "searchShortcutRecorder"; theme: panel.theme; bridge: panel.bridge; shortcut: panel.searchShortcut; onShortcutChosen: function(shortcut) { panel.localShortcutChanged("search", shortcut); } }
        }
        RowLayout {
            Layout.fillWidth: true
            Copy { text: "New sequence" }
            QuietButton { objectName: "newSequenceShortcutToggle"; theme: panel.theme; text: panel.newSequenceShortcutEnabled ? "Enabled" : "Disabled"; selected: panel.newSequenceShortcutEnabled; onClicked: panel.newSequenceShortcutToggled(!panel.newSequenceShortcutEnabled) }
            ShortcutRecorder { id: newRecorder; objectName: "newSequenceShortcutRecorder"; theme: panel.theme; bridge: panel.bridge; shortcut: panel.newSequenceShortcut; onShortcutChosen: function(shortcut) { panel.localShortcutChanged("new", shortcut); } }
        }
        Copy { visible: bridge.recording !== ""; text: bridge.recording }
        QuietButton { theme: panel.theme; visible: bridge.recording !== ""; text: "Cancel recording"; onClicked: bridge.cancelRecording() }
    }
    QuietDialog {
        id: actionsDialog; theme: panel.theme; parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(340 * panel.theme.scale, parent.width - 32); modal: true; title: "Sequence actions"
        contentItem: ColumnLayout {
            QuietButton { theme: panel.theme; text: "Description"; Layout.fillWidth: true; enabled: panel.steps.length > 0; onClicked: { actionsDialog.close(); descriptionDialog.open(); } }
            QuietButton { theme: panel.theme; text: "Duplicate"; Layout.fillWidth: true; enabled: panel.steps.length > 0; onClicked: { actionsDialog.close(); panel.switchRequested("duplicate", ""); } }
            QuietButton { theme: panel.theme; text: "Delete"; Layout.fillWidth: true; enabled: bridge.selectedId !== ""; onClicked: { actionsDialog.close(); panel.switchRequested("delete", ""); } }
            QuietButton { theme: panel.theme; text: "Import"; Layout.fillWidth: true; onClicked: { actionsDialog.close(); importer.open(); } }
            QuietButton { theme: panel.theme; text: "Export"; Layout.fillWidth: true; enabled: panel.steps.length > 0 && !bridge.dirty; onClicked: { actionsDialog.close(); exporter.open(); } }
        }
    }
    function confirmDeleteSequence() { deleteKind = "sequence"; deleteDialog.open(); }
    QuietDialog {
        id: descriptionDialog; theme: panel.theme; parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(500 * panel.theme.scale, parent.width - 32); modal: true; title: "Description"
        contentItem: ColumnLayout {
            QuietField { theme: panel.theme; Layout.fillWidth: true; placeholderText: "Description (optional)"; text: panel.draft.description || ""; onEditingFinished: if (!panel.edit("description", text)) text = panel.draft.description || "" }
            QuietButton { theme: panel.theme; text: "Done"; onClicked: descriptionDialog.close() }
        }
    }
    QuietDialog {
        id: serverManager; theme: panel.theme; parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(500 * panel.theme.scale, parent.width - 32); modal: true; title: "Manage servers"
        contentItem: ColumnLayout {
            spacing: 16
            Copy { text: "Add, edit, or remove saved servers. Baseline is your normal ping with Clumsier stopped." }
            Choice {
                id: servers; objectName: "serverSelector"
                model: [{id: "", name: "Choose a server", baseline: 0}].concat(bridge.profiles)
                textRole: "name"; currentIndex: 0
            }
            Copy { text: servers.currentIndex > 0 ? "Baseline: " + servers.model[servers.currentIndex].baseline + " ms" : "Add a server or choose one above." }
            RowLayout {
                QuietButton { objectName: "addServerButton"; theme: panel.theme; text: "Add server"; onClicked: panel.openProfile("", "", 0) }
                QuietButton { theme: panel.theme; text: "Edit"; enabled: servers.currentIndex > 0; onClicked: { const server = servers.model[servers.currentIndex]; panel.openProfile(server.id, server.name, server.baseline); } }
                QuietButton { theme: panel.theme; text: "Delete"; enabled: servers.currentIndex > 0; onClicked: { panel.deleteKind = "server"; deleteDialog.open(); } }
            }
            Copy { text: "Editing a server's baseline affects every sequence using that server." }
            QuietButton { theme: panel.theme; text: "Done"; onClicked: serverManager.close() }
        }
    }
    QuietDialog {
        id: baselineSetup; objectName: "baselineSetupDialog"
        property string serverName: ""
        parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(470 * panel.theme.scale, parent.width - 32)
        theme: panel.theme; modal: true; closePolicy: Popup.NoAutoClose
        title: "Your ping to " + serverName
        onClosed: Qt.callLater(panel.offerBaselineSetup)
        contentItem: ColumnLayout {
            spacing: 16
            Copy { text: "These sequences use " + baselineSetup.serverName + ". Enter your usual ping to this server with Clumsier stopped. We’ll use it to calculate how much delay to add." }
            QuietField {
                id: setupPing; objectName: "setupBaselinePing"; theme: panel.theme
                Layout.fillWidth: true; placeholderText: "Usual ping in ms"
            }
            Copy { text: "You can change this later in Manage servers." }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                QuietButton { theme: panel.theme; text: "Later"; onClicked: { panel.deferredServers = panel.deferredServers.concat([baselineSetup.serverName]); baselineSetup.close(); } }
                QuietButton {
                    objectName: "saveSetupBaseline"; theme: panel.theme; text: "Save baseline"
                    enabled: /^[0-9]+$/.test(setupPing.text) && Number(setupPing.text) <= 60000
                    onClicked: if (bridge.resolveServer(baselineSetup.serverName, Number(setupPing.text))) baselineSetup.close()
                }
            }
        }
    }
    FileDialog { id: importer; title: "Import sequence"; nameFilters: ["Sequence JSON (*.json)"]; onAccepted: panel.switchRequested("import", selectedFile.toString()) }
    FileDialog { id: exporter; title: "Export sequence"; fileMode: FileDialog.SaveFile; defaultSuffix: "json"; nameFilters: ["Sequence JSON (*.json)"]; onAccepted: bridge.exportPreset(selectedFile) }
    QuietDialog {
        theme: panel.theme
        id: profileDialog; parent: Overlay.overlay; anchors.centerIn: parent; width: Math.min(440, parent.width - 32); modal: true; title: "Server baseline"; standardButtons: Dialog.NoButton
        ColumnLayout {
            anchors.fill: parent
            QuietField { id: profileName; theme: panel.theme; Layout.fillWidth: true; placeholderText: "Server name" }
            QuietNumber { id: profilePing; theme: panel.theme; Layout.fillWidth: true; maximum: 60000; placeholderText: "Normal ping in ms"; onValueCommitted: function(number) { value = number; } onInvalidEntry: bridge.invalidEntry() }
            RowLayout {
                QuietButton { theme: panel.theme; text: "Save"; onClicked: if (bridge.saveProfile(panel.editingProfile, profileName.text, profilePing.value, false)) profileDialog.close() }
                QuietButton { theme: panel.theme; text: "Cancel"; onClicked: profileDialog.close() }
            }
            Copy { text: bridge.error; visible: text !== "" }
        }
    }
    QuietDialog {
        theme: panel.theme
        id: deleteDialog; parent: Overlay.overlay; anchors.centerIn: parent; width: Math.min(450 * panel.theme.scale, parent.width - 32); modal: true; title: "Delete this " + panel.deleteKind + "?"; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout {
            spacing: 16
            Copy { text: "Sequences using this server will need a new server selection." }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                QuietButton { theme: panel.theme; text: "Cancel"; onClicked: deleteDialog.reject() }
                QuietButton { theme: panel.theme; text: "Delete"; onClicked: deleteDialog.accept() }
            }
        }
        onAccepted: panel.deleteKind === "sequence" ? bridge.deletePreset() : bridge.deleteProfile(servers.model[servers.currentIndex].id)
    }
}
