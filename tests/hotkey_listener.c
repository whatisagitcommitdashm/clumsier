#include <assert.h>
#include <stdio.h>
#include "hotkeys.h"

static void ignoreAction(AppAction action) { (void)action; }

int main(void) {
    char error[HOTKEY_ERROR_SIZE];
    HotkeySettings unassigned = {0};
    DWORD before, after;
    // Install actual hooks and exercise their dedicated message thread. No
    // shortcuts are assigned, no input is injected, and no capture is started.
    assert(hotkeysOpen(ignoreAction, error));
    assert(hotkeysApply(&unassigned, NULL, error));
    hotkeysClose();
    assert(GetProcessHandleCount(GetCurrentProcess(), &before));
    for (int i = 0; i < 10; ++i) {
        if (!hotkeysOpen(ignoreAction, error)) { puts(error); return 1; }
        assert(hotkeysApply(&unassigned, NULL, error));
        hotkeysClose();
    }
    assert(GetProcessHandleCount(GetCurrentProcess(), &after));
    assert(after == before);
    puts("PASS actual keyboard/mouse hook startup and 10 shutdown cycles without handle growth");
    return 0;
}
