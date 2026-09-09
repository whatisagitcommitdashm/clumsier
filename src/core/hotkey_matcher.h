#pragma once
#include "input.h"

// The UI thread owns this state. Keeping Windows event handling elsewhere lets
// tests feed in presses/releases without touching the user's keyboard.
typedef struct {
    unsigned int (*normalize)(unsigned int key); // Optional platform modifier mapping.
    uint8_t physical[HOTKEY_KEY_COUNT]; // Separate left/right modifier states.
    HotkeyBinding down;              // Held keys, with modifiers combined.
    HotkeySettings settings;
    bool latched[ACTION_COUNT];      // Already fired; must become incomplete to rearm.
    bool waitForRelease;             // Don't carry held inputs across a mode change.
    bool recording;
    bool recordingStarted;
    bool recordingReleased;         // First release freezes the combination.
    HotkeyBinding recorded;
} HotkeyMatcher;

void hotkeyMatcherApply(HotkeyMatcher *matcher, const HotkeySettings *settings);
void hotkeyMatcherRecord(HotkeyMatcher *matcher);
void hotkeyMatcherCancel(HotkeyMatcher *matcher);
// Returns an action bitmask. Repeated down events never trigger actions.
unsigned int hotkeyMatcherInput(HotkeyMatcher *matcher, unsigned int key, bool down);
