#include <string.h>
#include "hotkey_matcher.h"

BOOL hotkeyEmpty(const HotkeyBinding *binding) {
    int i;
    for (i = 1; i < HOTKEY_KEY_COUNT; ++i) if (binding->keys[i]) return FALSE;
    return TRUE;
}

BOOL hotkeyContains(const HotkeyBinding *set, const HotkeyBinding *subset) {
    // Extra held keys are fine: holding W to move shouldn't block Q+E.
    int i;
    for (i = 1; i < HOTKEY_KEY_COUNT; ++i)
        if (subset->keys[i] && !set->keys[i]) return FALSE;
    return TRUE;
}

UINT hotkeyNormalize(UINT key) {
    switch (key) {
        case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
        case VK_LSHIFT: case VK_RSHIFT: return VK_SHIFT;
        case VK_LMENU: case VK_RMENU: return VK_MENU;
        case VK_RWIN: return VK_LWIN;
        default: return key;
    }
}

void hotkeyMatcherCancel(HotkeyMatcher *matcher) {
    // Also used when applying bindings and entering recording. Wait out any
    // held keys so an old press doesn't become an action in the new mode.
    matcher->recording = FALSE;
    matcher->waitForRelease = !hotkeyEmpty(&matcher->down);
    memset(matcher->latched, 0, sizeof(matcher->latched));
}

void hotkeyMatcherApply(HotkeyMatcher *matcher, const HotkeySettings *settings) {
    matcher->settings = *settings;
    hotkeyMatcherCancel(matcher);
}

void hotkeyMatcherRecord(HotkeyMatcher *matcher) {
    hotkeyMatcherCancel(matcher);
    matcher->recording = TRUE;
    matcher->recordingStarted = FALSE;
    matcher->recordingReleased = FALSE;
    memset(&matcher->recorded, 0, sizeof(matcher->recorded));
}

unsigned int hotkeyMatcherInput(HotkeyMatcher *matcher, UINT key, BOOL down) {
    unsigned int actions = 0;
    int i;
    BOOL wasDown;
    if (!key || key >= HOTKEY_KEY_COUNT) return 0;
    wasDown = matcher->physical[key] != 0;
    matcher->physical[key] = (BYTE)(down != 0);
    memset(&matcher->down, 0, sizeof(matcher->down));
    // Keep left/right physical state separate: releasing one Ctrl must not
    // release the logical Ctrl when its counterpart is still held.
    for (i = 1; i < HOTKEY_KEY_COUNT; ++i)
        if (matcher->physical[i]) matcher->down.keys[hotkeyNormalize(i)] = 1;
    if (matcher->waitForRelease) {
        if (hotkeyEmpty(&matcher->down)) matcher->waitForRelease = FALSE;
        return 0;
    }
    if (matcher->recording) {
        // Until the first release, every new press extends the held combination.
        // Afterwards, just wait for everything to come up. Q down, Q up, E down
        // is a sequence, not a Q+E binding.
        if (down && !wasDown && !matcher->recordingReleased) {
            matcher->recordingStarted = TRUE;
            matcher->recorded = matcher->down;
        }
        if (!down && wasDown && matcher->recordingStarted) matcher->recordingReleased = TRUE;
        if (matcher->recordingReleased && hotkeyEmpty(&matcher->down)) matcher->recording = FALSE;
        return 0;
    }
    for (i = 0; i < ACTION_COUNT; ++i) {
        BOOL matches = !hotkeyEmpty(&matcher->settings.bindings[i]) &&
            hotkeyContains(&matcher->down, &matcher->settings.bindings[i]);
        // Rearm as soon as a member is released. Someone can hold Ctrl and tap
        // Q repeatedly, but the OS's repeated key-downs won't trigger actions.
        if (!matches) matcher->latched[i] = FALSE;
        else if (down && !wasDown && !matcher->latched[i]) {
            matcher->latched[i] = TRUE;
            actions |= 1u << i; // One bit per action; more than one may match.
        }
    }
    return actions;
}
