#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "hotkeys.h"
#include <shlobj.h>

void hotkeyDefaults(HotkeySettings *settings) {
    int i;
    memset(settings, 0, sizeof(*settings));
    for (i = 0; i < ACTION_COUNT; ++i) settings->bindings[i].key = VK_F5 + i;
}

BOOL hotkeyParse(const char *text, HotkeyBinding *binding, char *error) {
    char compact[HOTKEY_TEXT_SIZE], *token, *next;
    size_t length = 0;
    HotkeyBinding parsed = {0};
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (isspace(c)) continue;
        if (length + 1 >= sizeof(compact)) goto invalid;
        compact[length++] = (char)toupper(c);
    }
    compact[length] = '\0';
    if (!strcmp(compact, "NONE") || !length) { *binding = parsed; return TRUE; }
    token = compact;
    while (token) {
        UINT modifier = 0;
        next = strchr(token, '+');
        if (next) *next++ = '\0';
        if (!strcmp(token, "CTRL")) modifier = MOD_CONTROL;
        else if (!strcmp(token, "ALT")) modifier = MOD_ALT;
        else if (!strcmp(token, "SHIFT")) modifier = MOD_SHIFT;
        else if (!strcmp(token, "WIN")) modifier = MOD_WIN;
        if (modifier) {
            if (parsed.key || (parsed.modifiers & modifier)) goto invalid;
            parsed.modifiers |= modifier;
        } else {
            char *end;
            long number;
            if (parsed.key || next) goto invalid;
            if (strlen(token) == 1 && (isupper((unsigned char)*token) || isdigit((unsigned char)*token))) {
                parsed.key = (UINT)*token;
            } else if (*token == 'F' && isdigit((unsigned char)token[1])) {
                number = strtol(token + 1, &end, 10);
                if (*end || number < 1 || number > 24 || number == 12) goto invalid;
                parsed.key = VK_F1 + (UINT)number - 1;
            } else goto invalid;
        }
        token = next;
    }
    if (!parsed.key) goto invalid;
    if (parsed.key < VK_F1 && !parsed.modifiers) {
        strcpy(error, "Letters and digits need a modifier, such as Ctrl+Alt+S.");
        return FALSE;
    }
    *binding = parsed;
    return TRUE;
invalid:
    strcpy(error, "Use F1-F24 (except reserved F12), Ctrl/Alt/Shift/Win plus a key, or None.");
    return FALSE;
}

void hotkeyFormat(HotkeyBinding binding, char text[HOTKEY_TEXT_SIZE]) {
    char key[12];
    text[0] = '\0';
    if (!binding.key) { strcpy(text, "None"); return; }
    if (binding.modifiers & MOD_CONTROL) strcat(text, "Ctrl+");
    if (binding.modifiers & MOD_ALT) strcat(text, "Alt+");
    if (binding.modifiers & MOD_SHIFT) strcat(text, "Shift+");
    if (binding.modifiers & MOD_WIN) strcat(text, "Win+");
    if (binding.key >= VK_F1 && binding.key <= VK_F24) sprintf(key, "F%u", binding.key - VK_F1 + 1);
    else { key[0] = (char)binding.key; key[1] = '\0'; }
    strcat(text, key);
}

BOOL hotkeyValidate(const HotkeySettings *settings, char *error) {
    int i, j;
    for (i = 0; i < ACTION_COUNT; ++i) {
        HotkeyBinding value = settings->bindings[i], parsed;
        char text[HOTKEY_TEXT_SIZE];
        if (value.modifiers & ~(MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN)) goto invalid;
        if (!value.key && value.modifiers) goto invalid;
        if (value.key && !((value.key >= 'A' && value.key <= 'Z') ||
            (value.key >= '0' && value.key <= '9') ||
            (value.key >= VK_F1 && value.key <= VK_F24 && value.key != VK_F12))) goto invalid;
        hotkeyFormat(value, text);
        if (!hotkeyParse(text, &parsed, error)) return FALSE;
        for (j = 0; j < i; ++j) {
            if (value.key && value.key == settings->bindings[j].key &&
                value.modifiers == settings->bindings[j].modifiers) {
                sprintf(error, "%s and %s cannot share %s.", actionName((AppAction)j), actionName((AppAction)i), text);
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
    char file[512], *line, *next;
    DWORD length;
    unsigned int seen = 0;
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
            if ((seen & 8) || strcmp(equals, "1")) goto invalid;
            seen |= 8;
        } else {
            if (!strcmp(line, "start")) index = ACTION_START_CAPTURE;
            else if (!strcmp(line, "stop")) index = ACTION_STOP_CAPTURE;
            else if (!strcmp(line, "toggle")) index = ACTION_TOGGLE_CAPTURE;
            else goto invalid;
            if ((seen & (1u << index)) || !hotkeyParse(equals, &loaded.bindings[index], error)) goto invalid;
            seen |= 1u << index;
        }
        line = next;
    }
    if (seen != 15 || !hotkeyValidate(&loaded, error)) goto invalid;
    *settings = loaded;
    return TRUE;
invalid:
    strcpy(error, "Invalid hotkeys.ini; defaults are shown. Apply & Save explicitly to replace it.");
    return FALSE;
}

BOOL hotkeySave(const wchar_t *path, const HotkeySettings *settings, char *error) {
    wchar_t temporary[MAX_PATH];
    char start[HOTKEY_TEXT_SIZE], stop[HOTKEY_TEXT_SIZE], toggle[HOTKEY_TEXT_SIZE], file[512];
    DWORD length, written;
    HANDLE handle;
    BOOL saved;
    if (!hotkeyValidate(settings, error)) return FALSE;
    if (wcslen(path) + 4 >= MAX_PATH) { strcpy(error, "Settings path is too long."); return FALSE; }
    swprintf(temporary, MAX_PATH, L"%ls.tmp", path);
    hotkeyFormat(settings->bindings[0], start);
    hotkeyFormat(settings->bindings[1], stop);
    hotkeyFormat(settings->bindings[2], toggle);
    length = (DWORD)sprintf(file, "version=1\nstart=%s\nstop=%s\ntoggle=%s\n", start, stop, toggle);
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
