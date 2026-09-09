#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "hotkeys.h"
#include <shlobj.h>

UINT hotkeyNormalize(UINT key) {
    switch (key) {
        case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
        case VK_LSHIFT: case VK_RSHIFT: return VK_SHIFT;
        case VK_LMENU: case VK_RMENU: return VK_MENU;
        case VK_RWIN: return VK_LWIN;
        default: return key;
    }
}

// These names are also the file format. Renaming one needs a parser alias for
// existing files. Punctuation uses US key names, not the character a particular
// keyboard layout would type; we're saving virtual keys, not text input.
static const struct { UINT key; const char *name; } keyNames[] = {
    {VK_CONTROL,"Ctrl"}, {VK_MENU,"Alt"}, {VK_SHIFT,"Shift"}, {VK_LWIN,"Win"},
    {VK_LBUTTON,"Mouse1"}, {VK_RBUTTON,"Mouse2"}, {VK_MBUTTON,"Mouse3"},
    {VK_XBUTTON1,"Mouse4"}, {VK_XBUTTON2,"Mouse5"},
    {VK_BACK,"Backspace"}, {VK_TAB,"Tab"}, {VK_RETURN,"Enter"}, {VK_ESCAPE,"Escape"},
    {VK_SPACE,"Space"}, {VK_CAPITAL,"CapsLock"}, {VK_PAUSE,"Pause"},
    {VK_PRIOR,"PageUp"}, {VK_NEXT,"PageDown"}, {VK_END,"End"}, {VK_HOME,"Home"},
    {VK_LEFT,"Left"}, {VK_UP,"Up"}, {VK_RIGHT,"Right"}, {VK_DOWN,"Down"},
    {VK_SNAPSHOT,"PrintScreen"}, {VK_INSERT,"Insert"}, {VK_DELETE,"Delete"},
    {VK_APPS,"Menu"}, {VK_NUMLOCK,"NumLock"}, {VK_SCROLL,"ScrollLock"},
    {VK_MULTIPLY,"NumpadMultiply"}, {VK_ADD,"NumpadAdd"}, {VK_SUBTRACT,"NumpadSubtract"},
    {VK_DECIMAL,"NumpadDecimal"}, {VK_DIVIDE,"NumpadDivide"}, {VK_SEPARATOR,"Separator"},
    {VK_OEM_1,"Semicolon"}, {VK_OEM_PLUS,"Equals"}, {VK_OEM_COMMA,"Comma"},
    {VK_OEM_MINUS,"Minus"}, {VK_OEM_PERIOD,"Period"}, {VK_OEM_2,"Slash"},
    {VK_OEM_3,"Backtick"}, {VK_OEM_4,"LeftBracket"}, {VK_OEM_5,"Backslash"},
    {VK_OEM_6,"RightBracket"}, {VK_OEM_7,"Quote"},
    {VK_VOLUME_MUTE,"Mute"}, {VK_VOLUME_DOWN,"VolumeDown"}, {VK_VOLUME_UP,"VolumeUp"},
    {VK_MEDIA_NEXT_TRACK,"MediaNext"}, {VK_MEDIA_PREV_TRACK,"MediaPrevious"},
    {VK_MEDIA_STOP,"MediaStop"}, {VK_MEDIA_PLAY_PAUSE,"MediaPlayPause"}
};

static void keyName(UINT key, char name[32]) {
    unsigned int i;
    for (i = 0; i < sizeof(keyNames)/sizeof(keyNames[0]); ++i)
        if (keyNames[i].key == key) { strcpy(name, keyNames[i].name); return; }
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) {
        name[0] = (char)key; name[1] = '\0';
    } else if (key >= VK_F1 && key <= VK_F24) sprintf(name, "F%u", key - VK_F1 + 1);
    else if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) sprintf(name, "Numpad%u", key - VK_NUMPAD0);
    else sprintf(name, "Key%02X", key); // Preserve keys we haven't given a friendly name.
}

void hotkeyDefaults(HotkeySettings *settings) {
    memset(settings, 0, sizeof(*settings));
    settings->bindings[ACTION_START_CAPTURE].keys[VK_F5] = 1;
    settings->bindings[ACTION_STOP_CAPTURE].keys[VK_F6] = 1;
    settings->bindings[ACTION_TOGGLE_CAPTURE].keys[VK_F7] = 1;
}

BOOL hotkeyParse(const char *text, HotkeyBinding *binding, char *error) {
    char compact[HOTKEY_TEXT_SIZE], *token, *next;
    size_t length = 0;
    HotkeyBinding parsed = {0}; // Don't change the caller's binding on a parse error.
    if (!text) text = "";
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (isspace(c)) continue;
        if (length + 1 >= sizeof(compact)) goto invalid;
        compact[length++] = (char)c;
    }
    compact[length] = '\0';
    if (!_stricmp(compact, "None") || !length) { *binding = parsed; return TRUE; }
    token = compact;
    while (token) {
        UINT key;
        char name[32];
        next = strchr(token, '+');
        if (next) *next++ = '\0';
        // Reuse the formatter's names so every recorded key can be read back.
        // With only 256 possible codes, a separate reverse lookup isn't needed.
        for (key = 1; key < HOTKEY_KEY_COUNT; ++key) {
            keyName(key, name);
            if (!_stricmp(token, name)) break;
        }
        if (key == HOTKEY_KEY_COUNT) goto invalid;
        key = hotkeyNormalize(key);
        if (parsed.keys[key]) goto invalid;
        parsed.keys[key] = 1;
        token = next;
    }
    *binding = parsed;
    return TRUE;
invalid:
    strcpy(error, "Invalid or repeated key name. Use the recorder to choose a combination.");
    return FALSE;
}

static void appendKey(char *text, UINT key) {
    char name[32];
    keyName(key, name);
    if (*text) strcat(text, " + ");
    strcat(text, name);
}

void hotkeyFormat(HotkeyBinding binding, char text[HOTKEY_TEXT_SIZE]) {
    // binding is a copy, so clearing modifiers below won't alter the caller's
    // set. Put them first, then sort other keys by code for consistent labels.
    UINT i;
    const UINT modifiers[] = {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN};
    text[0] = '\0';
    for (i = 0; i < sizeof(modifiers)/sizeof(modifiers[0]); ++i) {
        if (binding.keys[modifiers[i]]) appendKey(text, modifiers[i]);
        binding.keys[modifiers[i]] = 0;
    }
    for (i = 1; i < HOTKEY_KEY_COUNT; ++i) if (binding.keys[i]) appendKey(text, i);
    if (!*text) strcpy(text, "None");
}

BOOL hotkeyValidate(const HotkeySettings *settings, char *error) {
    int i, j, key;
    for (i = 0; i < ACTION_COUNT; ++i) {
        const HotkeyBinding *binding = &settings->bindings[i];
        if (binding->keys[0]) goto invalid;
        for (key = 1; key < HOTKEY_KEY_COUNT; ++key)
            if (binding->keys[key] > 1 || (binding->keys[key] && hotkeyNormalize(key) != (UINT)key)) goto invalid;
        if (hotkeyEmpty(binding)) continue;
        // Q and Q+E can't coexist: Q would fire before the user could finish
        // Q+E. Partial overlap (Q+E and E+R) is allowed; both may then fire.
        for (j = 0; j < i; ++j) {
            const HotkeyBinding *other = &settings->bindings[j];
            if (!hotkeyEmpty(other) && (hotkeyContains(binding, other) || hotkeyContains(other, binding))) {
                sprintf(error, "%s and %s overlap: one combination contains the other. Choose distinct combinations.",
                    actionName((AppAction)j), actionName((AppAction)i));
                return FALSE;
            }
        }
    }
    return TRUE;
invalid:
    strcpy(error, "Invalid hotkey settings.");
    return FALSE;
}

BOOL hotkeySettingsPath(wchar_t path[MAX_PATH], char *error) {
    wchar_t directory[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, directory)) ||
        wcslen(directory) + wcslen(L"\\Clumsier\\hotkeys.ini") >= MAX_PATH) {
        strcpy(error, "Cannot locate the personal settings folder."); return FALSE;
    }
    wcscat(directory, L"\\Clumsier");
    if (!CreateDirectoryW(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        strcpy(error, "Cannot create the personal settings folder."); return FALSE;
    }
    swprintf(path, MAX_PATH, L"%ls\\hotkeys.ini", directory);
    return TRUE;
}

BOOL hotkeyLoad(const wchar_t *path, HotkeySettings *settings, char *error) {
    char file[HOTKEY_TEXT_SIZE * ACTION_COUNT + 128], *line, *next;
    DWORD length;
    unsigned int seen = 0;
    int version = 0;
    HotkeySettings loaded;
    HANDLE handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) { hotkeyDefaults(settings); return TRUE; }
        strcpy(error, "Cannot read hotkey settings; defaults are shown."); return FALSE;
    }
    if (!ReadFile(handle, file, sizeof(file) - 1, &length, NULL)) {
        CloseHandle(handle); strcpy(error, "Cannot read hotkey settings."); return FALSE;
    }
    CloseHandle(handle);
    if (length == sizeof(file) - 1 || memchr(file, '\0', length)) goto invalid;
    file[length] = '\0';
    memset(&loaded, 0, sizeof(loaded));
    line = file;
    while (line && *line) {
        char *equals;
        int index;
        next = strchr(line, '\n'); if (next) *next++ = '\0';
        if (strlen(line) && line[strlen(line) - 1] == '\r') line[strlen(line) - 1] = '\0';
        if (!*line) { line = next; continue; }
        equals = strchr(line, '='); if (!equals) goto invalid;
        *equals++ = '\0';
        if (!strcmp(line, "version")) {
            if (version || (strcmp(equals, "1") && strcmp(equals, "2") && strcmp(equals, "3"))) goto invalid;
            version = atoi(equals);
        } else {
            if (!strcmp(line, "start")) index = ACTION_START_CAPTURE;
            else if (!strcmp(line, "stop")) index = ACTION_STOP_CAPTURE;
            else if (!strcmp(line, "toggle")) index = ACTION_TOGGLE_CAPTURE;
            else if (!strcmp(line, "next_step")) index = ACTION_NEXT_STEP;
            else if (!strcmp(line, "previous_step")) index = ACTION_PREVIOUS_STEP;
            else if (!strcmp(line, "reset_sequence")) index = ACTION_RESET_SEQUENCE;
            else goto invalid;
            if ((seen & (1u << index)) || !hotkeyParse(equals, &loaded.bindings[index], error)) goto invalid;
            seen |= 1u << index;
        }
        line = next;
    }
    // Old files retain their three bindings; new actions start unassigned.
    // Don't rewrite a user's file until they explicitly choose Apply & Save.
    if (!version || seen != (version < 3 ? 7u : (1u << ACTION_COUNT) - 1u) || !hotkeyValidate(&loaded, error)) goto invalid;
    // Publish only a complete, validated file. A bad line must not leave the
    // caller with a mixture of old settings and partially loaded ones.
    *settings = loaded;
    return TRUE;
invalid:
    strcpy(error, "Invalid hotkeys.ini; defaults are shown. Apply & Save explicitly to replace it.");
    return FALSE;
}

BOOL hotkeySave(const wchar_t *path, const HotkeySettings *settings, char *error) {
    wchar_t temporary[MAX_PATH];
    char binding[HOTKEY_TEXT_SIZE], file[HOTKEY_TEXT_SIZE * ACTION_COUNT + 256];
    const char *keys[ACTION_COUNT] = {"start", "stop", "toggle", "next_step", "previous_step", "reset_sequence"};
    int i;
    DWORD length, written;
    HANDLE handle;
    BOOL saved;
    if (!hotkeyValidate(settings, error)) return FALSE;
    if (wcslen(path) + 4 >= MAX_PATH) { strcpy(error, "Settings path is too long."); return FALSE; }
    swprintf(temporary, MAX_PATH, L"%ls.tmp", path);
    length = (DWORD)sprintf(file, "version=3\n");
    for (i = 0; i < ACTION_COUNT; ++i) {
        hotkeyFormat(settings->bindings[i], binding);
        length += (DWORD)sprintf(file + length, "%s=%s\n", keys[i], binding);
    }
    // Write beside the destination, then replace it after the write is flushed.
    // A failed write should leave the previous settings file available to load.
    handle = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) goto failed;
    saved = WriteFile(handle, file, length, &written, NULL) && written == length && FlushFileBuffers(handle);
    CloseHandle(handle);
    if (!saved || !MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary);
        goto failed;
    }
    return TRUE;
failed:
    strcpy(error, "Could not save hotkeys. The previous bindings are still active.");
    return FALSE;
}
