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
    readonly property bool dialogOpen: profileDialog.visible || serverManager.visible || actionsDialog.visible || descriptionDialog.visible || deleteDialog.visible || importer.visible || exporter.visible
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
    function edit(key, value) {
        if (playing) return;
        let copy = JSON.parse(JSON.stringify(draft));
        copy[key] = value;
        bridge.updateDraft(copy);
    }
    function editStep(key, value) {
        let copy = JSON.parse(JSON.stringify(steps));
        copy[stepIndex][key] = value;
        edit("steps", copy);
    }
    function stepType(lowest) {
        let copy = JSON.parse(JSON.stringify(steps));
        copy[stepIndex] = {name: step.name, note: step.note, type: lowest ? "lowest" : "target"};
        if (!lowest) copy[stepIndex].target_ms = 0;
        edit("steps", copy);
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
        bridge.updateDraft(copy);
    }
    function moveStep(delta) {
        let copy = JSON.parse(JSON.stringify(steps));
        let next = stepIndex + delta;
        if (next < 0 || next >= copy.length) return;
        const item = copy.splice(stepIndex, 1)[0]; copy.splice(next, 0, item);
        stepIndex = next; edit("steps", copy);
    }
    function openProfile(id, name, baseline) {
        editingProfile = id; profileName.text = name; profilePing.text = String(baseline); profileDialog.open();
    }
    Connections {
        target: panel.bridge
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
                text: panel.draft.name || ""; placeholderText: "Sequence name"
                font.pixelSize: 26 * panel.theme.scale; readOnly: panel.playing
                background: Rectangle { color: "transparent"; radius: 5; border.width: sequenceName.activeFocus ? 1 : 0; border.color: panel.theme.border }
                onTextEdited: panel.edit("name", text)
            }
            Title { visible: !panel.steps.length; text: "No sequence selected" }
            QuietButton { objectName: "editSequenceButton"; theme: panel.theme; text: "Edit"; visible: panel.playing && panel.steps.length > 0; onClicked: panel.switchRequested("edit", "") }
            QuietButton { objectName: "sequenceActionsButton"; theme: panel.theme; text: "···"; Accessible.name: "Sequence actions"; subtle: true; onClicked: actionsDialog.open() }
        }
        RowLayout {
            visible: bridge.dirty; Layout.fillWidth: true
            Copy { text: "Unsaved changes"; color: panel.theme.accent }
            QuietButton { objectName: "saveSequenceButton"; theme: panel.theme; text: "Save"; onClicked: bridge.save() }
            QuietButton { objectName: "discardSequenceButton"; theme: panel.theme; text: "Discard"; onClicked: bridge.discard() }
        }
        Copy { visible: !panel.steps.length; text: "Choose a sequence from the sidebar, create one, or import a shared JSON file." }
        RowLayout {
            visible: panel.steps.length > 0; Layout.fillWidth: true
            Copy { text: panel.server ? panel.server.name + " · Baseline " + panel.server.baseline + " ms" : "No server selected"; color: panel.theme.text }
            QuietButton {
                objectName: "manageServersButton"; theme: panel.theme; text: "Manage servers"; subtle: true
                onClicked: panel.switchRequested("edit", "servers")
            }
        }
        Copy { visible: panel.playing; text: "Click a step to use it now. Choose Edit to stop playback and make changes." }
        ColumnLayout {
            visible: panel.steps.length > 0
            Layout.fillWidth: true; spacing: 16
            RowLayout {
                visible: !panel.playing
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
                        visible: !panel.playing
                        QuietButton { theme: panel.theme; text: "+ Step"; enabled: panel.steps.length < 64; onClicked: {
                            let copy = JSON.parse(JSON.stringify(panel.steps));
                            let s = {name: "Step " + (copy.length + 1), note: "", type: panel.draft.mode === "target_ping" ? "target" : "delay"};
                            if (s.type === "target") s.target_ms = 0; else { s.inbound_ms = 0; s.outbound_ms = 0; }
                            copy.push(s); panel.edit("steps", copy); panel.stepIndex = copy.length - 1;
                        } }
                        QuietButton { theme: panel.theme; text: "↑"; enabled: panel.stepIndex > 0; onClicked: panel.moveStep(-1) }
                        QuietButton { theme: panel.theme; text: "↓"; enabled: panel.stepIndex + 1 < panel.steps.length; onClicked: panel.moveStep(1) }
                        QuietButton { theme: panel.theme; text: "Remove"; enabled: panel.steps.length > 1; onClicked: { let copy = JSON.parse(JSON.stringify(panel.steps)); copy.splice(panel.stepIndex, 1); panel.edit("steps", copy); } }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.preferredWidth: 300; Layout.alignment: Qt.AlignTop
                    Copy { text: (panel.playing ? "Step " : "Edit step ") + (panel.stepIndex + 1) }
                    QuietField { theme: panel.theme; Layout.fillWidth: true; text: panel.step.name || ""; placeholderText: "Step name"; readOnly: panel.playing; onTextEdited: panel.editStep("name", text) }
                    QuietField { theme: panel.theme; Layout.fillWidth: true; text: panel.step.note || ""; placeholderText: "Note (optional)"; readOnly: panel.playing; onTextEdited: panel.editStep("note", text) }
                    RowLayout {
                        visible: panel.draft.mode === "target_ping"; enabled: !panel.playing
                        QuietButton { theme: panel.theme; text: "Target ping"; selected: panel.step.type === "target"; onClicked: panel.stepType(false) }
                        QuietButton { theme: panel.theme; text: "Lowest available"; selected: panel.step.type === "lowest"; onClicked: panel.stepType(true) }
                    }
                    QuietField { objectName: "liveTargetField"; readOnly: panel.playing; theme: panel.theme; Layout.fillWidth: true; visible: panel.step.type === "target"; text: String(panel.step.target_ms || 0); validator: IntValidator { bottom: 0; top: 60000 } onTextEdited: panel.editStep("target_ms", text === "" ? -1 : Number(text)) }
                    Copy { objectName: "stepEstimate"; text: panel.estimate(); color: panel.theme.text }
                    Copy { visible: panel.step.type === "lowest"; text: "No added delay. Your connection keeps its normal ping." }
                    Copy { visible: panel.step.type === "delay"; text: "Inbound / outbound delay (ms)" }
                    RowLayout {
                        visible: panel.step.type === "delay"
                        QuietField { theme: panel.theme; Layout.fillWidth: true; enabled: !panel.playing && panel.draft.policy !== "outbound"; text: String(panel.step.inbound_ms || 0); validator: IntValidator { bottom: 0; top: 15000 } onTextEdited: panel.editStep("inbound_ms", text === "" ? -1 : Number(text)) }
                        QuietField { theme: panel.theme; Layout.fillWidth: true; enabled: !panel.playing && panel.draft.policy !== "inbound"; text: String(panel.step.outbound_ms || 0); validator: IntValidator { bottom: 0; top: 15000 } onTextEdited: panel.editStep("outbound_ms", text === "" ? -1 : Number(text)) }
                    }
                }
            }
            QuietButton { objectName: "trafficSettingsButton"; visible: panel.advancedMode && !panel.playing; theme: panel.theme; text: trafficOptions.visible ? "Hide traffic settings" : "Traffic settings"; onClicked: panel.trafficExpanded = !panel.trafficExpanded }
            ColumnLayout {
                id: trafficOptions; visible: panel.advancedMode && panel.trafficExpanded && !panel.playing; Layout.fillWidth: true
                Copy { text: "These settings apply to every step. The defaults delay all incoming traffic." }
                Choice { model: ["Inbound delay", "Outbound delay", "Both directions"]; currentIndex: ["inbound", "outbound", "both"].indexOf(panel.draft.policy); onActivated: {
                    let copy = JSON.parse(JSON.stringify(panel.draft)); copy.policy = ["inbound", "outbound", "both"][index];
                    copy.traffic.direction = index === 2 ? "both" : copy.policy;
                    copy.steps.forEach(function(s) { if (s.type === "delay") { if (index === 0) s.outbound_ms = 0; if (index === 1) s.inbound_ms = 0; } }); bridge.updateDraft(copy);
                } }
                Repeater {
                    model: [{key:"protocol", choices:["any","tcp","udp"]}, {key:"direction", choices:["both","inbound","outbound"]}]
                    Choice { required property var modelData; model: modelData.choices; currentIndex: model.indexOf((panel.draft.traffic || {})[modelData.key]); onActivated: { let t = Object.assign({}, panel.draft.traffic); t[modelData.key] = model[index]; panel.edit("traffic", t); } }
                }
                Repeater {
                    model: [{key:"remote_address", label:"Remote IP (blank for any)"}, {key:"remote_port", label:"Remote port (0 for any)"}, {key:"native_filter", label:"Windows filter (optional)"}]
                    QuietField { required property var modelData; theme: panel.theme; Layout.fillWidth: true; placeholderText: modelData.label; text: String((panel.draft.traffic || {})[modelData.key] || ""); onTextEdited: {
                        let t = Object.assign({}, panel.draft.traffic); t[modelData.key] = modelData.key === "remote_port" ? Number(text) : text;
                        if (modelData.key === "native_filter") t.native_backend = text ? "windivert" : ""; panel.edit("traffic", t);
                    } }
                }
            }
            QuietCheckBox { theme: panel.theme; text: "Wrap at end"; checked: !!panel.draft.loop; enabled: !panel.playing; onToggled: panel.edit("loop", checked) }

        }
    }
    ColumnLayout {
        visible: panel.page === "quick controls"; Layout.fillWidth: true; spacing: 18
        Title { text: "Delay" }
        Copy { text: "Enter a delay, then Start. Valid changes apply immediately, even while running. Zero adds no delay. Stop before changing the traffic direction." }
        QuietField {
            id: quickMs; objectName: "quickDelayField"; theme: panel.theme; Layout.fillWidth: true
            placeholderText: "Added delay in ms"
            // Keep the draft independent of status polling and backend echoes:
            // typing must not move the cursor or silently replace invalid input.
            Component.onCompleted: text = String(panel.acceptedQuickMs)
            function isValid(value) { return /^[0-9]+$/.test(value) && Number(value) <= 15000; }
            readonly property bool validDelay: isValid(text)
            onTextEdited: if (isValid(text)) bridge.quickDelay(Number(text), panel.acceptedQuickDirection)
        }
        Copy {
            objectName: "quickDelayError"; visible: !quickMs.validDelay
            text: "Enter a whole number from 0 to 15000 ms. The last valid delay is still selected."
            color: panel.theme.accent
        }
        Choice {
            id: quickDirection; objectName: "quickDelayDirection"
            model: ["Inbound", "Outbound", "Both directions"]; currentIndex: panel.acceptedQuickDirection
            enabled: !panel.playing && quickMs.validDelay
            onActivated: {
                if (!bridge.quickDelay(Number(quickMs.text), index)) currentIndex = panel.acceptedQuickDirection;
            }
        }
        Copy { text: "Accepted delay: " + bridge.state.inbound + " ms inbound / " + bridge.state.outbound + " ms outbound" }
    }
    ColumnLayout {
        visible: panel.page === "hotkeys"; Layout.fillWidth: true; spacing: 18
        Title { text: "Hotkeys" }
        Copy { text: bridge.globalHotkeysAvailable
            ? "Click Record, hold your keyboard or mouse combination, then release. Input still reaches your game. Changes are saved immediately. Global actions pause while you edit text or use this tab."
            : "Global hotkeys are not yet available on Linux. Use the Start, Stop, and sequence playback buttons." }
        Repeater {
            model: bridge.bindings
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Copy { text: modelData.name }
                QuietButton { theme: panel.theme; text: modelData.text || "Unbound"; onClicked: bridge.recordBinding(modelData.action) }
                QuietButton { theme: panel.theme; text: "Record"; onClicked: bridge.recordBinding(modelData.action) }
                QuietButton { theme: panel.theme; text: "Clear"; onClicked: bridge.saveBinding(modelData.action, "None") }
            }
        }
        Copy { visible: bridge.recording !== ""; text: bridge.recording }
        QuietButton { theme: panel.theme; visible: bridge.recording !== ""; text: "Cancel recording"; onClicked: bridge.cancelRecording() }
    }
    QuietDialog {
        id: actionsDialog; theme: panel.theme; parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(340 * panel.theme.scale, parent.width - 32); modal: true; title: "Sequence actions"
        contentItem: ColumnLayout {
            QuietButton { theme: panel.theme; text: "Description"; Layout.fillWidth: true; enabled: panel.steps.length > 0 && !panel.playing; onClicked: { actionsDialog.close(); descriptionDialog.open(); } }
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
            QuietField { theme: panel.theme; Layout.fillWidth: true; placeholderText: "Description (optional)"; text: panel.draft.description || ""; onTextEdited: panel.edit("description", text) }
            QuietButton { theme: panel.theme; text: "Done"; onClicked: descriptionDialog.close() }
        }
    }
    QuietDialog {
        id: serverManager; theme: panel.theme; parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(500 * panel.theme.scale, parent.width - 32); modal: true; title: "Server for this sequence"
        contentItem: ColumnLayout {
            spacing: 16
            Copy { text: "Choose the server you play on. Its baseline is your normal ping with Clumsier stopped. This sequence remembers your choice." }
            Choice {
                id: servers; objectName: "serverSelector"
                model: [{id: "", name: "Choose a server", baseline: 0}].concat(bridge.profiles)
                textRole: "name"; currentIndex: model.findIndex(function(p) { return p.id === bridge.profileId; })
                onActivated: bridge.selectProfile(model[index].id)
            }
            Copy { text: panel.server ? "Baseline: " + panel.server.baseline + " ms" : "Add a server or choose one above." }
            RowLayout {
                QuietButton { objectName: "addServerButton"; theme: panel.theme; text: "Add server"; onClicked: panel.openProfile("", "", 0) }
                QuietButton { theme: panel.theme; text: "Edit"; enabled: !!panel.server; onClicked: panel.openProfile(panel.server.id, panel.server.name, panel.server.baseline) }
                QuietButton { theme: panel.theme; text: "Delete"; enabled: !!panel.server; onClicked: { panel.deleteKind = "server"; deleteDialog.open(); } }
            }
            Copy { text: "Editing a server's baseline affects every sequence using that server." }
            QuietButton { theme: panel.theme; text: "Done"; onClicked: serverManager.close() }
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
            QuietField { id: profilePing; theme: panel.theme; Layout.fillWidth: true; placeholderText: "Normal ping in ms"; validator: IntValidator { bottom: 0; top: 60000 } }
            RowLayout {
                QuietButton { theme: panel.theme; text: "Save"; enabled: profilePing.acceptableInput; onClicked: if (bridge.saveProfile(panel.editingProfile, profileName.text, Number(profilePing.text))) profileDialog.close() }
                QuietButton { theme: panel.theme; text: "Cancel"; onClicked: profileDialog.close() }
            }
            Copy { text: bridge.error; visible: text !== "" }
        }
    }
    QuietDialog {
        theme: panel.theme
        id: deleteDialog; parent: Overlay.overlay; anchors.centerIn: parent; modal: true; title: "Delete this " + panel.deleteKind + "?"; standardButtons: Dialog.NoButton
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            QuietButton { theme: panel.theme; text: "Cancel"; onClicked: deleteDialog.reject() }
            QuietButton { theme: panel.theme; text: "Delete"; onClicked: deleteDialog.accept() }
        }
        onAccepted: panel.deleteKind === "sequence" ? bridge.deletePreset() : bridge.deleteProfile(bridge.profileId)
    }
}
