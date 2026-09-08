#include <stdio.h>
#include <string.h>
#include "hotkey_matcher.h"

#define INPUT_MESSAGE (WM_APP + 1)

// This window and the matcher belong to the UI thread. The listener only posts
// messages to it. inputGeneration is shared, so use Interlocked to read/write it;
// volatile alone wouldn't provide the synchronization we need.
static HWND messageWindow;
static HANDLE inputThread, inputReady;
static DWORD inputThreadId, inputError;
static volatile LONG inputGeneration;
static HotkeyMatcher matcher;
static HotkeyActionCallback actionCallback;
static HotkeyRecordCallback recordCallback;

// The listener never runs application actions or touches IUP. Always forwarding
// the original event lets other active apps receive both presses and releases normally.
static LRESULT CALLBACK keyboardHook(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        const KBDLLHOOKSTRUCT *event = (const KBDLLHOOKSTRUCT*)data;
        BOOL down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        // Copy the key and press/release flag into the message. Windows owns
        // event, so don't queue its pointer for use after this callback returns.
        if (down || message == WM_KEYUP || message == WM_SYSKEYUP)
            PostMessageW(messageWindow, INPUT_MESSAGE, MAKEWPARAM(event->vkCode, down),
                InterlockedCompareExchange(&inputGeneration, 0, 0));
    }
    return CallNextHookEx(NULL, code, message, data);
}

static LRESULT CALLBACK mouseHook(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        const MSLLHOOKSTRUCT *event = (const MSLLHOOKSTRUCT*)data;
        UINT key = 0;
        BOOL down = FALSE;
        // Mouse movement and wheel events deliberately don't enter the matcher.
        // They're still forwarded below, just like every button event.
        switch (message) {
            case WM_LBUTTONDOWN: down = TRUE; // fall through
            case WM_LBUTTONUP: key = VK_LBUTTON; break;
            case WM_RBUTTONDOWN: down = TRUE; // fall through
            case WM_RBUTTONUP: key = VK_RBUTTON; break;
            case WM_MBUTTONDOWN: down = TRUE; // fall through
            case WM_MBUTTONUP: key = VK_MBUTTON; break;
            case WM_XBUTTONDOWN: down = TRUE; // fall through
            case WM_XBUTTONUP: key = HIWORD(event->mouseData) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2; break;
        }
        if (key) PostMessageW(messageWindow, INPUT_MESSAGE, MAKEWPARAM(key, down),
            InterlockedCompareExchange(&inputGeneration, 0, 0));
    }
    return CallNextHookEx(NULL, code, message, data);
}

static DWORD WINAPI listenForInput(void *unused) {
    HHOOK keyboard, mouse;
    MSG message;
    UNREFERENCED_PARAMETER(unused);
    // Create the message queue before advertising readiness to the UI thread.
    PeekMessageW(&message, NULL, 0, 0, PM_NOREMOVE);
    keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHook, GetModuleHandleW(NULL), 0);
    inputError = keyboard ? 0 : GetLastError();
    mouse = SetWindowsHookExW(WH_MOUSE_LL, mouseHook, GetModuleHandleW(NULL), 0);
    if (!mouse && !inputError) inputError = GetLastError();
    // Signal even on failure: hotkeysOpen is waiting for the startup result.
    SetEvent(inputReady);
    if (keyboard && mouse) {
        while (GetMessageW(&message, NULL, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (mouse) UnhookWindowsHookEx(mouse);
    if (keyboard) UnhookWindowsHookEx(keyboard);
    return 0;
}

static void deliverInput(UINT key, BOOL down) {
    // We're back on the UI thread here. The matcher can now report an action
    // or update the recorder without doing either from inside a Windows hook.
    BOOL wasRecording = matcher.recording;
    unsigned int actions = hotkeyMatcherInput(&matcher, key, down);
    int i;
    if (wasRecording && recordCallback) {
        HotkeyRecordCallback callback = recordCallback;
        // The final callback may close the recorder. Drop our reference first.
        if (!matcher.recording) recordCallback = NULL;
        callback(&matcher.recorded, !matcher.recording);
    }
    for (i = 0; i < ACTION_COUNT; ++i)
        if (actions & (1u << i)) actionCallback((AppAction)i);
}

static LRESULT CALLBACK hotkeyWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == INPUT_MESSAGE) {
        if ((LONG)lParam == InterlockedCompareExchange(&inputGeneration, 0, 0))
            deliverInput(LOWORD(wParam), HIWORD(wParam) != 0);
        return 0;
    }
    if (message == WM_TIMER) {
        UINT key;
        // Recover missed releases after desktop switches or a full message
        // queue. Never invent a press or trigger an action during recovery.
        for (key = 1; key < HOTKEY_KEY_COUNT; ++key)
            if (matcher.physical[key] && !(GetAsyncKeyState(key) & 0x8000)) deliverInput(key, FALSE);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static void synchronizeInput(void) {
    UINT key;
    // A queued press from before Apply or Record must not take on a new meaning.
    // Bump the generation to discard those messages, then sample what's held.
    InterlockedIncrement(&inputGeneration);
    memset(matcher.physical, 0, sizeof(matcher.physical));
    memset(&matcher.down, 0, sizeof(matcher.down));
    for (key = 1; key < HOTKEY_KEY_COUNT; ++key) {
        // Hooks report separate left/right modifiers, so do not seed a second,
        // generic modifier that would never receive a matching release.
        if (key == VK_CONTROL || key == VK_SHIFT || key == VK_MENU) continue;
        if (GetAsyncKeyState(key) & 0x8000) {
            matcher.physical[key] = 1;
            matcher.down.keys[hotkeyNormalize(key)] = 1;
        }
    }
    hotkeyMatcherCancel(&matcher);
}

BOOL hotkeysOpen(HotkeyActionCallback callback, char *error) {
    WNDCLASSW windowClass = {0};
    DWORD reason;
    if (messageWindow) return TRUE;
    windowClass.lpfnWndProc = hotkeyWindowProc;
    windowClass.hInstance = GetModuleHandleW(NULL);
    windowClass.lpszClassName = L"ClumsierHotkeys";
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) goto failed;
    // HWND_MESSAGE gives us a message destination without another visible window.
    // IUP's event loop dispatches its messages on this same thread.
    messageWindow = CreateWindowExW(0, windowClass.lpszClassName, L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, NULL, windowClass.hInstance, NULL);
    if (!messageWindow) goto failed;
    actionCallback = callback;
    inputReady = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!inputReady) goto failed;
    inputThread = CreateThread(NULL, 0, listenForInput, NULL, 0, &inputThreadId);
    if (!inputThread) goto failed;
    // Don't report success until both hooks are installed. The listener also
    // creates its queue before signaling, so shutdown can safely post WM_QUIT.
    WaitForSingleObject(inputReady, INFINITE);
    if (inputError) { SetLastError(inputError); goto failed; }
    if (!SetTimer(messageWindow, 1, 250, NULL)) goto failed;
    synchronizeInput();
    return TRUE;
failed:
    reason = GetLastError();
    hotkeysClose();
    sprintf(error, "Cannot initialize global input (Windows error %lu).", reason);
    return FALSE;
}

BOOL hotkeysApply(const HotkeySettings *settings, const wchar_t *path, char *error) {
    if (!messageWindow) { strcpy(error, "Global hotkeys are unavailable."); return FALSE; }
    if (!hotkeyValidate(settings, error)) return FALSE;
    // Save first. If disk access fails, the user's working bindings stay active.
    if (path && !hotkeySave(path, settings, error)) return FALSE;
    recordCallback = NULL;
    synchronizeInput();
    hotkeyMatcherApply(&matcher, settings);
    return TRUE;
}

void hotkeysRecordBegin(HotkeyRecordCallback callback) {
    synchronizeInput();
    hotkeyMatcherRecord(&matcher);
    recordCallback = callback;
}

void hotkeysRecordCancel(void) {
    recordCallback = NULL;
    synchronizeInput();
}

void hotkeysClose(void) {
    // Keep the message window alive until the listener has removed its hooks;
    // otherwise a last input event could be posted to a destroyed window.
    if (inputThread) {
        PostThreadMessageW(inputThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(inputThread, INFINITE);
        CloseHandle(inputThread);
    }
    if (inputReady) CloseHandle(inputReady);
    inputThread = inputReady = NULL;
    inputThreadId = 0;
    if (messageWindow) { KillTimer(messageWindow, 1); DestroyWindow(messageWindow); }
    messageWindow = NULL;
    actionCallback = NULL;
    recordCallback = NULL;
    memset(&matcher, 0, sizeof(matcher));
}
