#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hotkeys.h"

// Deterministic registration failures exercise rollback without depending on
// which shortcuts happen to be occupied on the developer's desktop.
static struct { int id; UINT modifiers, key; } registrations[16];
static int registerCalls, failOnCall, realRegistration;
static BOOL testRegister(HWND window, int id, UINT modifiers, UINT key) {
    int i;
    ++registerCalls;
    if (realRegistration) return RegisterHotKey(window, id, modifiers, key);
    if (failOnCall && registerCalls == failOnCall) {
        SetLastError(ERROR_HOTKEY_ALREADY_REGISTERED); return FALSE;
    }
    assert(modifiers & MOD_NOREPEAT);
    for (i = 0; i < 16; ++i) {
        if (registrations[i].id && registrations[i].modifiers == modifiers && registrations[i].key == key) {
            SetLastError(ERROR_HOTKEY_ALREADY_REGISTERED); return FALSE;
        }
    }
    for (i = 0; i < 16; ++i) if (!registrations[i].id) {
        registrations[i].id = id; registrations[i].modifiers = modifiers;
        registrations[i].key = key; return TRUE;
    }
    assert(0); return FALSE;
}
static BOOL testUnregister(HWND window, int id) {
    int i;
    if (realRegistration) return UnregisterHotKey(window, id);
    for (i = 0; i < 16; ++i) if (registrations[i].id == id) {
        registrations[i].id = 0; return TRUE;
    }
    assert(0); return FALSE;
}
#define RegisterHotKey testRegister
#define UnregisterHotKey testUnregister
#include "../src/hotkeys.c"
#undef RegisterHotKey
#undef UnregisterHotKey

static int dispatched[ACTION_COUNT], captureRunning, starts, stops;
static void recordAction(AppAction action) { ++dispatched[action]; }
static int isRunning(void *context) { (void)context; return captureRunning; }
static int start(void *context) { (void)context; ++starts; captureRunning = 1; return 1; }
static void stop(void *context) { (void)context; ++stops; captureRunning = 0; }
static void sameSettings(const HotkeySettings *a, const HotkeySettings *b) {
    int i;
    for (i = 0; i < ACTION_COUNT; ++i) assert(sameBinding(a->bindings[i], b->bindings[i]));
}
static int registrationCount(void) {
    int i, count = 0;
    for (i = 0; i < 16; ++i) if (registrations[i].id) ++count;
    return count;
}
static void writeText(const wchar_t *path, const char *text) {
    FILE *file = _wfopen(path, L"wb"); assert(file);
    assert(fwrite(text, 1, strlen(text), file) == strlen(text));
    assert(!fclose(file));
}

int main(void) {
    char error[HOTKEY_ERROR_SIZE], text[HOTKEY_TEXT_SIZE];
    wchar_t directory[MAX_PATH], path[MAX_PATH], badPath[MAX_PATH];
    HotkeySettings defaults, proposed, loaded;
    HotkeyBinding binding;
    ActionTarget target = {NULL, isRunning, start, stop};
    ActiveHotkey previous[ACTION_COUNT];
    int i, staleId;

    assert(actionExecute(ACTION_TOGGLE_CAPTURE, &target) && captureRunning);
    assert(actionExecute(ACTION_TOGGLE_CAPTURE, &target) && !captureRunning);
    assert(starts == 1 && stops == 1);
    captureRunning = 1; // State changed outside Toggle, e.g. the Start button.
    assert(actionExecute(ACTION_TOGGLE_CAPTURE, &target) && !captureRunning);
    assert(!actionExecute(ACTION_COUNT, &target));
    puts("PASS action dispatch and toggle using current engine state");

    hotkeyDefaults(&defaults);
    assert(defaults.bindings[0].key == VK_F5 && defaults.bindings[1].key == VK_F6 && defaults.bindings[2].key == VK_F7);
    assert(hotkeyParse(" ctrl + ALT + s ", &binding, error));
    hotkeyFormat(binding, text); assert(!strcmp(text, "Ctrl+Alt+S"));
    assert(!hotkeyParse("Ctrl+Ctrl+S", &binding, error));
    assert(!hotkeyParse("Ctrl+", &binding, error));
    assert(!hotkeyParse("F12", &binding, error));
    assert(!hotkeyParse("F25", &binding, error));
    assert(!hotkeyParse("S", &binding, error));
    assert(hotkeyParse("None", &binding, error) && !binding.key);
    proposed = defaults; proposed.bindings[2] = proposed.bindings[1];
    assert(!hotkeyValidate(&proposed, error));
    puts("PASS defaults, parsing, reserved keys, and duplicate validation");

    assert(GetTempPathW(MAX_PATH, directory));
    assert(GetTempFileNameW(directory, L"chk", 0, path));
    assert(DeleteFileW(path));
    assert(hotkeyLoad(path, &loaded, error)); sameSettings(&loaded, &defaults);
    proposed = defaults; assert(hotkeyParse("Ctrl+Alt+S", &proposed.bindings[0], error));
    assert(hotkeySave(path, &proposed, error));
    assert(hotkeyLoad(path, &loaded, error)); sameSettings(&loaded, &proposed);
    writeText(path, "version=2\nstart=F5\nstop=F6\ntoggle=F7\n");
    assert(!hotkeyLoad(path, &loaded, error)); sameSettings(&loaded, &proposed);
    writeText(path, "version=1\nstart=F5\nstop=F5\ntoggle=F7\n");
    assert(!hotkeyLoad(path, &loaded, error));
    writeText(path, "version=1\nstart=F5\nstop=F6\n");
    assert(!hotkeyLoad(path, &loaded, error));
    assert(hotkeySave(path, &defaults, error));
    puts("PASS settings round-trip, missing file, malformed file, and version rejection");

    assert(hotkeysOpen(recordAction, error));
    assert(hotkeysApply(&defaults, NULL, error));
    memcpy(previous, active, sizeof(previous));
    proposed = defaults;
    proposed.bindings[0] = defaults.bindings[1];
    proposed.bindings[1] = defaults.bindings[0];
    i = registerCalls;
    assert(hotkeysApply(&proposed, path, error));
    assert(registerCalls == i && active[0].registrationId == previous[1].registrationId);
    SendMessageW(messageWindow, WM_HOTKEY, active[0].registrationId, MAKELPARAM(0, VK_F6));
    assert(dispatched[ACTION_START_CAPTURE] == 1);
    assert(hotkeyLoad(path, &loaded, error)); sameSettings(&loaded, &proposed);
    puts("PASS rebinding swaps without releasing working shortcuts; action delivery");

    memcpy(previous, active, sizeof(previous));
    proposed = defaults;
    proposed.bindings[0].key = VK_F8; proposed.bindings[1].key = VK_F9;
    failOnCall = registerCalls + 2;
    assert(!hotkeysApply(&proposed, path, error));
    assert(!memcmp(previous, active, sizeof(previous)) && registrationCount() == 3);
    failOnCall = 0;
    // Use the existing settings file as a directory: saving must fail.
    swprintf(badPath, MAX_PATH, L"%ls\\settings.ini", path);
    assert(!hotkeysApply(&proposed, badPath, error));
    assert(!memcmp(previous, active, sizeof(previous)) && registrationCount() == 3);
    assert(hotkeyLoad(path, &proposed, error)); sameSettings(&loaded, &proposed);
    puts("PASS registration and save failures preserve previous bindings and file");

    staleId = active[2].registrationId;
    proposed.bindings[2].key = proposed.bindings[2].modifiers = 0;
    assert(hotkeysApply(&proposed, NULL, error));
    SendMessageW(messageWindow, WM_HOTKEY, staleId, MAKELPARAM(0, VK_F7));
    assert(dispatched[ACTION_TOGGLE_CAPTURE] == 0 && registrationCount() == 2);
    hotkeysClose(); assert(registrationCount() == 0);
    assert(hotkeysOpen(recordAction, error));
    assert(hotkeyLoad(path, &loaded, error));
    assert(hotkeysApply(&loaded, NULL, error));
    hotkeysClose(); assert(registrationCount() == 0);
    puts("PASS unassignment, stale messages, cleanup, and reopening saved settings");

    // Exercise actual Windows registration and conflict reporting with an
    // uncommon shortcut. No keyboard input or capture action is generated.
    realRegistration = 1;
    assert(hotkeysOpen(recordAction, error));
    memset(&proposed, 0, sizeof(proposed));
    proposed.bindings[0].key = VK_F23;
    proposed.bindings[0].modifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT;
    if (hotkeysApply(&proposed, NULL, error)) {
        assert(!RegisterHotKey(NULL, 12345, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, VK_F23));
        hotkeysClose();
        assert(RegisterHotKey(NULL, 12345, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, VK_F23));
        assert(UnregisterHotKey(NULL, 12345));
        puts("PASS actual Windows registration, conflict, and release");
    } else {
        hotkeysClose();
        printf("SKIP Windows registration integration: %s\n", error);
    }
    assert(DeleteFileW(path));
    return 0;
}
