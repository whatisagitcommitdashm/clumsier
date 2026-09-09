#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "actions.h"

// Input IDs are supplied by the platform adapter; zero means no key. The
// matcher treats them as opaque set indexes. Native names/serialization belong
// to the adapter (existing Windows settings keep their current names).
#define HOTKEY_KEY_COUNT 256
typedef struct { uint8_t keys[HOTKEY_KEY_COUNT]; } HotkeyBinding;
typedef struct { HotkeyBinding bindings[ACTION_COUNT]; } HotkeySettings;
bool hotkeyContains(const HotkeyBinding *set, const HotkeyBinding *subset);
bool hotkeyEmpty(const HotkeyBinding *binding);
