#include <stdio.h>
#include <string.h>
#include "common.h"
#include "sequence_controls.h"
#include "preset_editor.h"
#include "hotkeys.h"

static AppController *application;
static void (*performAction)(AppAction);
static void (*configurationChanged)(void);
static PresetStore store;
static bool initialized;
static bool advancedView;
static LibraryEntry presets[LIBRARY_MAX_ITEMS], profiles[LIBRARY_MAX_ITEMS];
static size_t presetCount, profileCount;
static Ihandle *panel, *presetList, *profileList, *message, *activeStatus, *preview;
static Ihandle *baselineArea, *unloadButton, *networkHint;
static Ihandle *activeHeading, *activeSummary;

static void tell(const char *text) { IupStoreAttribute(message, "TITLE", text); }
static Ihandle *parentDialog(void) { return IupGetDialog(panel); }
static int selectedPreset(void) {
    int index = IupGetInt(presetList, "VALUE") - 1;
    if (index < 0 || index >= (int)presetCount) { tell("Select a saved preset first."); return -1; }
    return index;
}
static bool selectedProfile(BaselineProfile *profile, char *error) {
    int index = IupGetInt(profileList, "VALUE") - 2;
    if (index < 0 || index >= (int)profileCount) { strcpy(error, "Select a baseline profile for target-ping presets."); return false; }
    return profileStoreRead(&store, profiles[index].id, profile, error);
}
static void fillLibrary(bool baseline, const wchar_t *selectId) {
    Ihandle *list = baseline ? profileList : presetList;
    LibraryEntry *entries = baseline ? profiles : presets;
    size_t *count = baseline ? &profileCount : &presetCount;
    size_t skipped, i;
    int selection = 0;
    char error[NETWORK_ERROR_SIZE], label[PRESET_NAME_SIZE + 48];
    if (!presetStoreList(&store, baseline, entries, count, &skipped, error)) { tell(error); return; }
    IupSetAttribute(list, "REMOVEITEM", "ALL");
    if (baseline) IupSetStrAttributeId(list, "", 1, "Choose a server / usual ping");
    for (i = 0; i < *count; ++i) {
        // IDs stay out of shared data. A short suffix disambiguates same-name imports.
        size_t j;
        bool duplicate = false;
        for (j = 0; j < *count; ++j) if (j != i && !strcmp(entries[i].name, entries[j].name)) duplicate = true;
        if (duplicate) snprintf(label, sizeof(label), "%s [%.8ls]", entries[i].name, entries[i].id);
        else snprintf(label, sizeof(label), "%s", entries[i].name);
        IupSetStrAttributeId(list, "", (int)i + (baseline ? 2 : 1), label);
        if (selectId && !wcscmp(selectId, entries[i].id)) selection = (int)i + (baseline ? 2 : 1);
    }
    IupSetInt(list, "VALUE", selection ? selection : (*count || baseline ? 1 : 0));
    if (skipped) { snprintf(error, sizeof(error), "%u unreadable or excess library files were skipped. Original files are untouched (limit 256).", (unsigned)skipped); tell(error); }
}
static int previewSelection(Ihandle *ih) {
    Preset preset;
    BaselineProfile baseline;
    char error[NETWORK_ERROR_SIZE], text[PRESET_MAX_STEPS * 220 + 512];
    size_t i, used = 0;
    int index = IupGetInt(presetList, "VALUE") - 1;
    bool hasBaseline;
    (void)ih;
    if (index < 0 || index >= (int)presetCount) { IupSetAttribute(preview, "VALUE", "Create or import a preset to begin."); return IUP_DEFAULT; }
    if (!presetStoreRead(&store, presets[index].id, &preset, error)) { tell(error); return IUP_DEFAULT; }
    hasBaseline = selectedProfile(&baseline, error);
    used += (size_t)snprintf(text, sizeof(text), "%s\n%s\n%s\n", preset.name, preset.description,
        preset.loop ? "Wraps at the ends" : "Stays at the first / last step at the ends");
    for (i = 0; i < preset.step_count; ++i) {
        StepResult result;
        PresetStep *step = &preset.steps[i];
        char intent[64];
        if (step->kind == STEP_LOWEST) strcpy(intent, "Lowest available");
        else if (step->kind == STEP_TARGET) snprintf(intent, sizeof(intent), "Target %u ms", step->target_ms);
        else strcpy(intent, "Added delay");
        if (presetResolve(&preset, i, hasBaseline, hasBaseline ? baseline.baseline_ms : 0, &result, error)) {
            if (!advancedView && preset.mode == PRESET_TARGET_PING)
                used += (size_t)snprintf(text + used, sizeof(text) - used, "%u. %s - %s; expected ~%u ms%s\n", (unsigned)i + 1,
                    step->name, intent, result.estimated_ping_ms, result.below_baseline ? " (below your usual ping)" : "");
            else if (!advancedView)
                used += (size_t)snprintf(text + used, sizeof(text) - used, "%u. %s - add %u ms total\n", (unsigned)i + 1,
                    step->name, result.lag.inbound_ms + result.lag.outbound_ms);
            else
                used += (size_t)snprintf(text + used, sizeof(text) - used, "%u. %s - %s; +%u in / +%u out%s\n", (unsigned)i + 1,
                    step->name, intent, result.lag.inbound_ms, result.lag.outbound_ms,
                    result.below_baseline ? " (below baseline: cannot reach target)" : "");
        } else used += (size_t)snprintf(text + used, sizeof(text) - used, "%u. %s - %s; %s\n", (unsigned)i + 1, step->name, intent,
            hasBaseline ? "cannot apply this value" : "choose your usual ping first");
    }
    IupStoreAttribute(preview, "VALUE", text);
    if (IupGetDialog(panel)) IupRefresh(IupGetDialog(panel));
    return IUP_DEFAULT;
}
void sequenceUIRefresh(void) {
    char text[1600], intent[100], heading[PRESET_NAME_SIZE + 32];
    const PresetStep *step;
    if (!activeStatus) return;
    if (!application->preset_loaded) {
        IupSetAttribute(activeHeading, "TITLE", controllerIsRunning(application) ? "Quick controls (running)" : "Quick controls (stopped)");
        IupSetAttribute(activeStatus, "TITLE", "Turn effects on or off in Quick controls, or choose a saved sequence in Sequences."); return;
    }
    snprintf(heading, sizeof(heading), "%s (%s)", application->preset.name,
        controllerIsRunning(application) ? "running" : "stopped");
    IupStoreAttribute(activeHeading, "TITLE", heading);
    step = &application->preset.steps[application->active_step];
    if (step->kind == STEP_LOWEST) strcpy(intent, "Lowest available");
    else if (step->kind == STEP_TARGET) snprintf(intent, sizeof(intent), "Target %u ms", step->target_ms);
    else strcpy(intent, "Added delay");
    if (!advancedView && application->preset.mode == PRESET_TARGET_PING)
        snprintf(text, sizeof(text), "Step: %s\n%s | Expected: ~%u ms\n%s%s",
            step->name, intent, application->step_result.estimated_ping_ms,
            application->step_result.below_baseline ? "Cannot go below your usual ping; adding no delay. " : "", step->note);
    else if (application->preset.mode == PRESET_TARGET_PING)
        snprintf(text, sizeof(text), "Step %u/%u: %s\n%s | Baseline %u ms | Expected ~%u ms | Added: %u inbound / %u outbound ms\n%s%s",
            (unsigned)application->active_step + 1,
            (unsigned)application->preset.step_count, step->name, intent, application->baseline_ms, application->step_result.estimated_ping_ms,
            application->lag.inbound_ms, application->lag.outbound_ms,
            application->step_result.below_baseline ? "Target is below your baseline; adding zero delay. " : "", step->note);
    else if (!advancedView) snprintf(text, sizeof(text), "Step: %s\nAdding %u ms total\n%s",
        step->name, application->lag.inbound_ms + application->lag.outbound_ms, step->note);
    else snprintf(text, sizeof(text), "Step %u/%u: %s\nAdded: %u inbound / %u outbound ms\n%s",
        (unsigned)application->active_step + 1,
        (unsigned)application->preset.step_count, step->name, application->lag.inbound_ms, application->lag.outbound_ms, step->note);
    IupStoreAttribute(activeStatus, "TITLE", text);
}
static int loadPreset(Ihandle *ih) {
    Preset preset;
    BaselineProfile baseline = {0};
    char error[NETWORK_ERROR_SIZE];
    bool hasBaseline;
    int index = selectedPreset(), i;
    (void)ih;
    if (index < 0) return IUP_DEFAULT;
    // The inherited effects aren't part of sequence v1. Don't quietly mix them
    // into an exported Lag preset and make the recipient's result different.
    for (i = 1; i < MODULE_CNT; ++i) if (*modules[i]->enabledFlag) {
        tell("Turn off the other effects in Quick controls before using a Lag sequence."); return IUP_DEFAULT;
    }
    if (!presetStoreRead(&store, presets[index].id, &preset, error) || !presetUIValidateWindows(&preset, error)) { tell(error); return IUP_DEFAULT; }
    hasBaseline = selectedProfile(&baseline, error);
    if (!controllerLoadPreset(application, &preset, hasBaseline, baseline.baseline_ms, error)) { tell(error); return IUP_DEFAULT; }
    configurationChanged();
    tell("Sequence ready at step 1. Start to use it, then advance with Next or your hotkey. Your usual ping stays fixed until you choose Use again.");
    return IUP_DEFAULT;
}
static int unloadPreset(Ihandle *ih) {
    (void)ih;
    controllerUnloadPreset(application); configurationChanged();
    tell("Unloaded. Current delay and capture state are kept; manual controls are available again."); return IUP_DEFAULT;
}
static int sequenceAction(Ihandle *ih) { performAction((AppAction)IupGetInt(ih, "_ACTION")); return IUP_DEFAULT; }
static int editPreset(Ihandle *ih) {
    int operation = IupGetInt(ih, "_OPERATION"), index;
    wchar_t id[LIBRARY_ID_SIZE] = {0};
    Preset preset;
    char error[NETWORK_ERROR_SIZE];
    if (operation == 0) presetDefault(&preset);
    else {
        index = selectedPreset(); if (index < 0) return IUP_DEFAULT;
        if (!presetStoreRead(&store, presets[index].id, &preset, error)) { tell(error); return IUP_DEFAULT; }
        if (operation == 1) wcscpy(id, presets[index].id);
        // Duplicate opens a draft; the new ID is allocated only when saved.
    }
    if (presetUIEdit(parentDialog(), &store, id, &preset, advancedView)) {
        tell("Saved. Choose Use this sequence to apply it. Your current session is unchanged.");
        fillLibrary(false, id); previewSelection(NULL);
    }
    return IUP_DEFAULT;
}
static int editProfile(Ihandle *ih) {
    wchar_t id[LIBRARY_ID_SIZE] = {0};
    BaselineProfile profile = {"New server", 0};
    char error[NETWORK_ERROR_SIZE];
    int index = IupGetInt(profileList, "VALUE") - 2;
    if (IupGetInt(ih, "_OPERATION")) {
        if (index < 0 || index >= (int)profileCount) { tell("Select a baseline profile to edit."); return IUP_DEFAULT; }
        wcscpy(id, profiles[index].id);
        if (!profileStoreRead(&store, id, &profile, error)) { tell(error); return IUP_DEFAULT; }
    }
    if (profileUIEdit(parentDialog(), &store, id, &profile)) {
        tell("Usual ping saved. Choose Use this sequence to apply it; your current session is unchanged.");
        fillLibrary(true, id); previewSelection(NULL);
    }
    return IUP_DEFAULT;
}
static int deleteEntry(Ihandle *ih) {
    bool baseline = IupGetInt(ih, "_PROFILE") != 0;
    int index = IupGetInt(baseline ? profileList : presetList, "VALUE") - (baseline ? 2 : 1);
    char error[NETWORK_ERROR_SIZE];
    int answer;
    if (index < 0 || index >= (int)(baseline ? profileCount : presetCount)) { tell("Select an item to delete."); return IUP_DEFAULT; }
    hotkeysPause(); answer = IupAlarm("Delete library item", "Delete the selected saved file? A loaded sequence keeps its current snapshot.", "Delete", "Cancel", NULL); hotkeysResume();
    if (answer != 1) return IUP_DEFAULT;
    if (!presetStoreDelete(&store, baseline, (baseline ? profiles : presets)[index].id, error)) tell(error);
    else { tell("Deleted the saved file. The active sequence is unchanged."); fillLibrary(baseline, NULL); previewSelection(NULL); }
    return IUP_DEFAULT;
}
static int transferPreset(Ihandle *ih) {
    bool exporting = IupGetInt(ih, "_EXPORT") != 0;
    Preset preset;
    wchar_t path[MAX_PATH], id[LIBRARY_ID_SIZE] = {0};
    char error[NETWORK_ERROR_SIZE];
    Ihandle *file;
    const char *chosen;
    int index, status;
    if (exporting) {
        index = selectedPreset(); if (index < 0) return IUP_DEFAULT;
        if (!presetStoreRead(&store, presets[index].id, &preset, error)) { tell(error); return IUP_DEFAULT; }
    }
    file = IupFileDlg();
    IupSetAttribute(file, "DIALOGTYPE", exporting ? "SAVE" : "OPEN");
    IupSetAttribute(file, "EXTFILTER", "Preset JSON|*.json|All files|*.*|");
    IupSetAttribute(file, "EXTDEFAULT", "json");
    IupSetAttributeHandle(file, "PARENTDIALOG", parentDialog());
    hotkeysPause(); IupPopup(file, IUP_CENTERPARENT, IUP_CENTERPARENT); hotkeysResume();
    status = IupGetInt(file, "STATUS"); chosen = IupGetAttribute(file, "VALUE");
    if (status == -1 || !chosen) { IupDestroy(file); return IUP_DEFAULT; }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, chosen, -1, path, MAX_PATH)) {
        IupDestroy(file); tell("The file path is too long or is not valid UTF-8."); return IUP_DEFAULT;
    }
    IupDestroy(file);
    if (exporting) {
        if (!presetFileWrite(path, &preset, error)) tell(error);
        else tell("Exported. The file contains the preset's original targets, never your baseline or hotkeys.");
    } else if (!presetFileRead(path, &preset, error) || !presetStoreSave(&store, id, &preset, error)) tell(error);
    else {
        tell("Imported. Review the sequence, then choose Use this sequence when ready."); fillLibrary(false, id); previewSelection(NULL);
    }
    return IUP_DEFAULT;
}
static Ihandle *operationButton(const char *title, Icallback callback, int operation) {
    Ihandle *button = presetUIButton(title, callback); IupSetInt(button, "_OPERATION", operation); return button;
}
Ihandle *sequenceUICreate(AppController *app, void (*perform)(AppAction), void (*changed)(void)) {
    Ihandle *next, *previous, *reset, *exportButton, *deleteProfile, *serverHelp;
    application = app; performAction = perform; configurationChanged = changed;
    presetList = presetUIChoice("", NULL, NULL, 0); profileList = presetUIChoice("No baseline selected", NULL, NULL, 0);
    IupSetAttribute(presetList, "VISIBLECOLUMNS", "20"); IupSetAttribute(profileList, "VISIBLECOLUMNS", "20");
    IupSetAttribute(presetList, "EXPAND", "HORIZONTAL"); IupSetAttribute(profileList, "EXPAND", "HORIZONTAL");
    preview = presetUIText("Create or import a preset to begin.", PRESET_JSON_MAX);
    IupSetAttributes(preview, "MULTILINE=YES, READONLY=YES, VISIBLELINES=5, SCROLLBAR=YES");
    message = IupLabel("Library opens after elevation."); IupSetAttributes(message, "WORDWRAP=YES, EXPAND=HORIZONTAL, SIZE=0x42");
    activeHeading = IupLabel("No sequence selected");
    IupSetAttribute(activeHeading, "FONT", "Segoe UI, Bold 14");
    IupSetAttributes(activeHeading, "WORDWRAP=YES, EXPAND=HORIZONTAL, SIZE=0x26");
    activeStatus = IupLabel("Choose a sequence below, then Start.");
    IupSetAttribute(activeStatus, "FONT", "Segoe UI, 11");
    IupSetAttributes(activeStatus, "WORDWRAP=YES, EXPAND=HORIZONTAL, SIZE=0x62");
    activeSummary = IupVbox(activeHeading, activeStatus, NULL);
    IupSetAttributes(activeSummary, "MARGIN=8x6, GAP=4, EXPAND=HORIZONTAL");
    next = presetUIButton("Next", sequenceAction); IupSetInt(next, "_ACTION", ACTION_NEXT_STEP);
    previous = presetUIButton("Previous", sequenceAction); IupSetInt(previous, "_ACTION", ACTION_PREVIOUS_STEP);
    reset = presetUIButton("Reset to first", sequenceAction); IupSetInt(reset, "_ACTION", ACTION_RESET_SEQUENCE);
    exportButton = presetUIButton("Export", transferPreset); IupSetInt(exportButton, "_EXPORT", 1);
    deleteProfile = presetUIButton("Delete profile", deleteEntry); IupSetInt(deleteProfile, "_PROFILE", 1);
    serverHelp = IupLabel("Choose the server you play on. Its saved ping is your normal ping with Clumsier stopped.\nUse the buttons below to add a server or change its saved ping. This is used for target-ping sequences.");
    IupSetAttributes(serverHelp, "WORDWRAP=YES, EXPAND=HORIZONTAL, SIZE=0x34");
    baselineArea = IupVbox(IupHbox(IupLabel("Server:"), profileList, NULL),
        serverHelp,
        IupHbox(operationButton("Add server / ping", editProfile, 0), operationButton("Edit profile", editProfile, 1), deleteProfile, NULL), NULL);
    unloadButton = presetUIButton("Unload", unloadPreset);
    networkHint = IupLabel("Use normal remote-server traffic; custom filters must match the direction policy.");
    panel = IupVbox(baselineArea, IupHbox(IupLabel("Sequence:"), presetList, NULL),
        IupHbox(operationButton("New sequence", editPreset, 0), operationButton("Edit", editPreset, 1), operationButton("Duplicate", editPreset, 2),
            presetUIButton("Delete", deleteEntry), presetUIButton("Import", transferPreset), exportButton, NULL),
        IupLabel("Selected sequence - preview"), preview,
        IupHbox(presetUIButton("Use this sequence", loadPreset), unloadButton, previous, reset, next, NULL),
        IupLabel("Expected ping is an estimate. Your connection cannot go below its usual ping."), networkHint, message, NULL);
    presetUIShow(unloadButton, advancedView); presetUIShow(networkHint, advancedView);
    IupSetAttributes(panel, "MARGIN=5x5, GAP=6, TABTITLE=Sequences, ACTIVE=NO");
    IupSetCallback(presetList, "VALUECHANGED_CB", previewSelection);
    IupSetCallback(profileList, "VALUECHANGED_CB", previewSelection);
    return panel;
}
Ihandle *sequenceUIStatus(void) { return activeSummary; }
void sequenceUISetAdvanced(bool advanced) {
    advancedView = advanced;
    presetUIShow(unloadButton, advanced); presetUIShow(networkHint, advanced);
    previewSelection(NULL); sequenceUIRefresh();
}
void sequenceUIInitialize(void) {
    char error[NETWORK_ERROR_SIZE];
    if (initialized) return;
    if (!presetStoreOpen(&store, NULL, error)) { tell(error); return; }
    initialized = true; IupSetAttribute(panel, "ACTIVE", "YES");
    tell("Choose or create a sequence. For target ping, add your server and usual ping first.");
    fillLibrary(false, NULL); fillLibrary(true, NULL); previewSelection(NULL);
}
