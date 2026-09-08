#pragma once
#include <windows.h>
#include "actions.h"

#define HOTKEY_TEXT_SIZE 64
#define HOTKEY_ERROR_SIZE 256

typedef struct {
    UINT modifiers;
    UINT key; // Zero means unassigned.
} HotkeyBinding;

typedef struct {
    HotkeyBinding bindings[ACTION_COUNT];
} HotkeySettings;

void hotkeyDefaults(HotkeySettings *settings);
BOOL hotkeyParse(const char *text, HotkeyBinding *binding, char *error);
void hotkeyFormat(HotkeyBinding binding, char text[HOTKEY_TEXT_SIZE]);
BOOL hotkeyValidate(const HotkeySettings *settings, char *error);
BOOL hotkeySettingsPath(wchar_t path[MAX_PATH], char *error);
// Missing files produce defaults. Invalid files leave the supplied settings unchanged.
BOOL hotkeyLoad(const wchar_t *path, HotkeySettings *settings, char *error);
BOOL hotkeySave(const wchar_t *path, const HotkeySettings *settings, char *error);

typedef void (*HotkeyActionCallback)(AppAction action);
// Open, apply, and close on the UI thread. Actions are delivered on that thread.
BOOL hotkeysOpen(HotkeyActionCallback callback, char *error);
// A NULL path applies without saving (used only at startup).
BOOL hotkeysApply(const HotkeySettings *settings, const wchar_t *path, char *error);
void hotkeysClose(void);
