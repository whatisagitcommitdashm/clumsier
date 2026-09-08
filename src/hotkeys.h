#pragma once
#include <windows.h>
#include "actions.h"

// A binding is a set of Windows virtual keys, including mouse buttons.
// Each slot is 0 or 1; all zeros means unassigned. Order doesn't matter.
#define HOTKEY_KEY_COUNT 256
#define HOTKEY_TEXT_SIZE 4096
#define HOTKEY_ERROR_SIZE 256

typedef struct { BYTE keys[HOTKEY_KEY_COUNT]; } HotkeyBinding;
typedef struct { HotkeyBinding bindings[ACTION_COUNT]; } HotkeySettings;

UINT hotkeyNormalize(UINT key);
BOOL hotkeyContains(const HotkeyBinding *set, const HotkeyBinding *subset);
BOOL hotkeyEmpty(const HotkeyBinding *binding);
void hotkeyDefaults(HotkeySettings *settings);
BOOL hotkeyParse(const char *text, HotkeyBinding *binding, char *error);
void hotkeyFormat(HotkeyBinding binding, char text[HOTKEY_TEXT_SIZE]);
BOOL hotkeyValidate(const HotkeySettings *settings, char *error);
BOOL hotkeySettingsPath(wchar_t path[MAX_PATH], char *error);
// Both version 1 and version 2 files load. Saving writes version 2 atomically.
BOOL hotkeyLoad(const wchar_t *path, HotkeySettings *settings, char *error);
BOOL hotkeySave(const wchar_t *path, const HotkeySettings *settings, char *error);

typedef void (*HotkeyActionCallback)(AppAction action);
typedef void (*HotkeyRecordCallback)(const HotkeyBinding *binding, BOOL finished);
// Call these from the UI thread. The listener has its own thread, but delivers
// both callbacks on the UI thread so callers can safely update their controls.
BOOL hotkeysOpen(HotkeyActionCallback callback, char *error);
// NULL path applies without saving, for startup. A failed save keeps the old
// bindings active. Error buffers passed to this API need HOTKEY_ERROR_SIZE bytes.
BOOL hotkeysApply(const HotkeySettings *settings, const wchar_t *path, char *error);
// The recording callback gets previews, then one finished notification. Its
// binding pointer belongs to the matcher; copy it if you need to keep the result.
void hotkeysRecordBegin(HotkeyRecordCallback callback);
void hotkeysRecordCancel(void);
void hotkeysClose(void);
