#pragma once
#include "hotkeys.h"

// The UI thread owns this state. Keeping Windows event handling elsewhere lets
// tests feed in presses/releases without touching the user's keyboard.
typedef struct {
    BYTE physical[HOTKEY_KEY_COUNT]; // Separate left/right modifier states.
    HotkeyBinding down;              // Held keys, with modifiers combined.
    HotkeySettings settings;
    BOOL latched[ACTION_COUNT];      // Already fired; must become incomplete to rearm.
    BOOL waitForRelease;             // Don't carry held inputs across a mode change.
    BOOL recording;
    BOOL recordingStarted;
    BOOL recordingReleased;         // First release freezes the combination.
    HotkeyBinding recorded;
} HotkeyMatcher;

void hotkeyMatcherApply(HotkeyMatcher *matcher, const HotkeySettings *settings);
void hotkeyMatcherRecord(HotkeyMatcher *matcher);
void hotkeyMatcherCancel(HotkeyMatcher *matcher);
// Returns an action bitmask. Repeated down events never trigger actions.
unsigned int hotkeyMatcherInput(HotkeyMatcher *matcher, UINT key, BOOL down);
