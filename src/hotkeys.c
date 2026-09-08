#include <stdio.h>
#include <string.h>
#include "hotkeys.h"

typedef struct {
    HotkeyBinding binding;
    int registrationId; // Zero means unassigned.
} ActiveHotkey;

static HWND messageWindow;
static HotkeyActionCallback actionCallback;
static ActiveHotkey active[ACTION_COUNT];
static int nextRegistrationId = 1;

static BOOL sameBinding(HotkeyBinding a, HotkeyBinding b) {
    return a.key == b.key && a.modifiers == b.modifiers;
}

static LRESULT CALLBACK hotkeyWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    int i;
    if (message == WM_HOTKEY) {
        for (i = 0; i < ACTION_COUNT; ++i) {
            if (active[i].registrationId && active[i].registrationId == (int)wParam &&
                active[i].binding.key == HIWORD(lParam) &&
                active[i].binding.modifiers == (LOWORD(lParam) & ~MOD_NOREPEAT)) {
                actionCallback((AppAction)i);
                break;
            }
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

BOOL hotkeysOpen(HotkeyActionCallback callback, char *error) {
    WNDCLASSW windowClass = {0};
    if (messageWindow) return TRUE;
    windowClass.lpfnWndProc = hotkeyWindowProc;
    windowClass.hInstance = GetModuleHandle(NULL);
    windowClass.lpszClassName = L"ClumsierHotkeys";
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) goto failed;
    messageWindow = CreateWindowExW(0, windowClass.lpszClassName, L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, NULL, windowClass.hInstance, NULL);
    if (!messageWindow) goto failed;
    actionCallback = callback;
    return TRUE;
failed:
    sprintf(error, "Cannot initialize global hotkeys (Windows error %lu).", GetLastError());
    return FALSE;
}

BOOL hotkeysApply(const HotkeySettings *settings, const wchar_t *path, char *error) {
    ActiveHotkey proposed[ACTION_COUNT] = {0};
    BOOL newlyRegistered[ACTION_COUNT] = {0};
    int i, j;
    MSG message;
    if (!messageWindow) { strcpy(error, "Global hotkeys are unavailable."); return FALSE; }
    if (!hotkeyValidate(settings, error)) return FALSE;

    // Reserve new combinations first. Reuse existing registrations, including
    // swaps between actions, so failed edits never surrender working shortcuts.
    for (i = 0; i < ACTION_COUNT; ++i) {
        proposed[i].binding = settings->bindings[i];
        if (!proposed[i].binding.key) continue;
        for (j = 0; j < ACTION_COUNT; ++j) {
            if (active[j].registrationId && sameBinding(active[j].binding, proposed[i].binding)) {
                proposed[i].registrationId = active[j].registrationId;
                break;
            }
        }
        if (proposed[i].registrationId) continue;
        if (nextRegistrationId > 0xBFFF) {
            strcpy(error, "Please restart Clumsier before changing hotkeys again."); goto rollback;
        }
        proposed[i].registrationId = nextRegistrationId++;
        if (!RegisterHotKey(messageWindow, proposed[i].registrationId,
            proposed[i].binding.modifiers | MOD_NOREPEAT, proposed[i].binding.key)) {
            char text[HOTKEY_TEXT_SIZE];
            DWORD reason = GetLastError();
            hotkeyFormat(proposed[i].binding, text);
            sprintf(error, "%s: %s is unavailable or reserved (Windows error %lu). Previous bindings kept.",
                actionName((AppAction)i), text, reason);
            goto rollback;
        }
        newlyRegistered[i] = TRUE;
    }
    if (path && !hotkeySave(path, settings, error)) goto rollback;

    for (i = 0; i < ACTION_COUNT; ++i) {
        if (!active[i].registrationId) continue;
        for (j = 0; j < ACTION_COUNT; ++j)
            if (proposed[j].registrationId == active[i].registrationId) break;
        if (j == ACTION_COUNT) UnregisterHotKey(messageWindow, active[i].registrationId);
    }
    // Do not reinterpret a queued press using a newly swapped action mapping.
    while (PeekMessageW(&message, messageWindow, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {}
    memcpy(active, proposed, sizeof(active));
    return TRUE;

rollback:
    for (i = 0; i < ACTION_COUNT; ++i)
        if (newlyRegistered[i]) UnregisterHotKey(messageWindow, proposed[i].registrationId);
    return FALSE;
}

void hotkeysClose(void) {
    int i;
    for (i = 0; i < ACTION_COUNT; ++i) {
        if (active[i].registrationId) UnregisterHotKey(messageWindow, active[i].registrationId);
    }
    memset(active, 0, sizeof(active));
    if (messageWindow) DestroyWindow(messageWindow);
    messageWindow = NULL;
    actionCallback = NULL;
}
