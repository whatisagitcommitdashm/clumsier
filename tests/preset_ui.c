// Exercise the real editor callbacks and library panel with IUP controls, but
// replace modal interaction so this test never opens windows or captures traffic.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "iup.h"
static int testPopup(Ihandle *dialog, int x, int y);
#define IupPopup testPopup
#include "../src/ui/preset_editor.c"
#include "../src/ui/sequence_controls.c"
#undef IupPopup

static unsigned paused, popupCase, changedCount;
void hotkeysPause(void) { ++paused; }
void hotkeysResume(void) { assert(paused); --paused; }
Module *modules[MODULE_CNT];
static Module fakeModules[MODULE_CNT];
static short enabled[MODULE_CNT];
static bool capturing;
int divertStart(const char *filter, char *error) { (void)filter; (void)error; return 0; }
void divertStop(void) {}
BOOL divertIsRunning(void) { return FALSE; }
void windowsLagConfigure(const LagSettings *settings) { (void)settings; }
static bool fakeStart(void *ctx, const CaptureTarget *target, const LagSettings *lag, char *error) {
    (void)ctx; (void)target; (void)lag; (void)error; capturing = true; return true;
}
static bool fakeApply(void *ctx, const LagSettings *lag, char *error) { (void)ctx; (void)lag; (void)error; return true; }
static void fakeStop(void *ctx) { (void)ctx; capturing = false; }
static bool fakeRunning(void *ctx) { (void)ctx; return capturing; }
static void changed(void) { ++changedCount; sequenceUIRefresh(); }
static void action(AppAction requested) {
    char error[NETWORK_ERROR_SIZE]; assert(controllerExecute(application, requested, error)); sequenceUIRefresh();
}
static int testPopup(Ihandle *dialog, int x, int y) {
    (void)x; (void)y;
    assert(paused);
    assert(IupMap(dialog) == IUP_NOERROR);
    if (popupCase == 1) {
        assert(!editor.advanced && !strcmp(IupGetAttribute(editor.traffic_fields, "FLOATING"), "YES"));
        IupSetAttribute(editor.name, "VALUE", "Four leaps from the editor");
        IupSetAttribute(editor.target, "VALUE", "200"); addStep(NULL);
        IupSetAttribute(editor.kind, "VALUE", "2"); addStep(NULL);
        IupSetAttribute(editor.target, "VALUE", "150"); addStep(NULL);
        IupSetAttribute(editor.target, "VALUE", "100"); savePreset(NULL);
        assert(editor.saved);
    } else if (popupCase == 2) {
        IupSetAttribute(editor.name, "VALUE", "Unsaved rename"); cancelPreset(NULL);
    } else if (popupCase == 3) {
        IupSetAttribute(profileEditor.name, "VALUE", "Test server");
        IupSetAttribute(profileEditor.baseline, "VALUE", "50"); saveProfile(NULL); assert(profileEditor.saved);
    } else if (popupCase == 4) {
        IupSetAttribute(editor.target, "VALUE", "350"); savePreset(NULL); assert(editor.saved);
    } else if (popupCase == 5) {
        IupSetAttribute(editor.target, "VALUE", "nonsense"); savePreset(NULL);
        assert(!editor.saved); cancelPreset(NULL);
    } else if (popupCase == 6) {
        assert(!editor.advanced);
        assert(!strcmp(IupGetAttribute(editor.traffic_fields, "FLOATING"), "YES"));
        // Even stale values in hidden controls must not replace imported traffic.
        IupSetAttribute(editor.native, "VALUE", "");
        IupSetAttribute(editor.protocol, "VALUE", "1");
        IupSetAttribute(editor.target, "VALUE", "175");
        savePreset(NULL); assert(editor.saved);
    } else if (popupCase == 7) {
        assert(!strcmp(IupGetAttribute(editor.target_fields, "FLOATING"), "YES"));
        assert(!strcmp(IupGetAttribute(editor.outbound_fields, "FLOATING"), "YES"));
        IupSetAttribute(editor.inbound, "VALUE", "75");
        savePreset(NULL); assert(editor.saved);
    } else assert(0);
    return IUP_NOERROR;
}
int main(int argc, char **argv) {
    const NetworkBackendOps ops = {fakeStart, fakeApply, fakeStop, fakeRunning};
    NetworkBackend backend = {&ops, NULL};
    AppController app;
    wchar_t temp[MAX_PATH], root[MAX_PATH], path[MAX_PATH];
    wchar_t id[LIBRARY_ID_SIZE] = {0}, profileId[LIBRARY_ID_SIZE] = {0};
    Preset preset, original, reloaded;
    BaselineProfile baseline = {"", 0};
    char error[NETWORK_ERROR_SIZE];
    Ihandle *dialog, *sequences;
    int i;
    assert(IupOpen(&argc, &argv) == IUP_NOERROR);
    for (i = 0; i < MODULE_CNT; ++i) { modules[i] = &fakeModules[i]; modules[i]->enabledFlag = &enabled[i]; }
    controllerInit(&app, backend); sequences = sequenceUICreate(&app, action, changed);
    dialog = IupDialog(IupVbox(sequenceUIStatus(), sequences, NULL)); assert(IupMap(dialog) == IUP_NOERROR);
    assert(GetTempPathW(MAX_PATH, temp)); assert(GetTempFileNameW(temp, L"cpu", 0, root)); assert(DeleteFileW(root));
    assert(presetStoreOpen(&store, root, error));
    presetDefault(&preset); popupCase = 1;
    assert(presetUIEdit(dialog, &store, id, &preset, false)); assert(!paused);
    assert(preset.step_count == 4 && preset.steps[0].target_ms == 200 && preset.steps[1].kind == STEP_LOWEST);
    assert(preset.steps[2].target_ms == 150 && preset.steps[3].target_ms == 100);
    original = preset; popupCase = 2;
    assert(!presetUIEdit(dialog, &store, id, &preset, true)); assert(!memcmp(&original, &preset, sizeof(preset)));
    popupCase = 5; assert(!presetUIEdit(dialog, &store, id, &preset, true)); assert(!memcmp(&original, &preset, sizeof(preset)));
    popupCase = 3; assert(profileUIEdit(dialog, &store, profileId, &baseline)); assert(baseline.baseline_ms == 50);
    fillLibrary(false, id); fillLibrary(true, profileId); previewSelection(NULL);
    assert(strstr(IupGetAttribute(preview, "VALUE"), "expected ~200 ms"));
    sequenceUISetAdvanced(true);
    assert(strstr(IupGetAttribute(preview, "VALUE"), "+150 in / +0 out"));
    sequenceUISetAdvanced(false);
    enabled[1] = 1; loadPreset(NULL); assert(!app.preset_loaded);
    enabled[1] = 0; loadPreset(NULL); assert(app.preset_loaded && app.lag.inbound_ms == 150 && changedCount == 1);
    action(ACTION_START_CAPTURE); action(ACTION_NEXT_STEP);
    assert(app.active_step == 1 && !app.lag.enabled && capturing);
    assert(strstr(IupGetAttribute(activeStatus, "TITLE"), "Lowest available"));
    popupCase = 4; assert(presetUIEdit(dialog, &store, id, &preset, true));
    assert(presetStoreRead(&store, id, &reloaded, error) && reloaded.steps[0].target_ms == 350);
    assert(app.preset.steps[0].target_ms == 200 && app.active_step == 1);
    unloadPreset(NULL); assert(!app.preset_loaded && capturing && !paused);
    controllerShutdown(&app);
    memset(&preset.target, 0, sizeof(preset.target));
    strcpy(preset.target.native_backend, "windivert"); strcpy(preset.target.native_filter, "tcp and inbound");
    original = preset; popupCase = 6;
    assert(presetUIEdit(dialog, &store, id, &preset, false));
    assert(captureTargetsEqual(&preset.target, &original.target) && preset.policy == original.policy);
    assert(preset.steps[0].target_ms == 175);
    presetDefault(&preset);
    assert(preset.policy == DELAY_INBOUND && preset.target.traffic.protocol == TRAFFIC_ANY && preset.target.traffic.direction == TRAFFIC_INBOUND);
    assert(!preset.target.traffic.remote_address[0] && !preset.target.traffic.remote_port && !preset.target.native_filter[0]);
    preset.mode = PRESET_ADDED_DELAY; preset.steps[0].kind = STEP_DELAY; preset.steps[0].target_ms = 0;
    popupCase = 7; assert(presetUIEdit(dialog, &store, id, &preset, false));
    assert(preset.steps[0].inbound_ms == 75 && preset.steps[0].outbound_ms == 0);
    fillLibrary(false, id); previewSelection(NULL);
    assert(IupGetChild(panel, 0) == baselineArea);
    assert(presetStoreDelete(&store, false, id, error)); assert(presetStoreDelete(&store, true, profileId, error));
    swprintf(path, MAX_PATH, L"%ls\\presets", root); assert(RemoveDirectoryW(path));
    swprintf(path, MAX_PATH, L"%ls\\profiles", root); assert(RemoveDirectoryW(path)); assert(RemoveDirectoryW(root));
    IupDestroy(dialog); IupClose();
    puts("PASS IUP callback integration: create four steps, cancel/invalid edits, local baseline, preview/load, effect exclusion, live snapshots and unload");
    return 0;
}
