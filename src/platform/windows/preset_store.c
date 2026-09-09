#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <wchar.h>
#include <shlobj.h>
#include "preset_store.h"
#include "../../../external/cjson/cJSON.h"

static bool fail(char *error, const char *message) { strcpy(error, message); return false; }
static bool directory(const wchar_t *path, char *error) {
    DWORD attributes;
    if (CreateDirectoryW(path, NULL)) return true;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        attributes = GetFileAttributesW(path);
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) return true;
    }
    return fail(error, "Cannot create the preset library folder.");
}
bool presetStoreOpen(PresetStore *store, const wchar_t *root, char *error) {
    PresetStore opened;
    wchar_t path[MAX_PATH];
    if (root) {
        if (wcslen(root) >= MAX_PATH) return fail(error, "Library path is too long.");
        wcscpy(opened.root, root);
    } else {
        if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, opened.root)))
            return fail(error, "Cannot locate your personal settings folder.");
        if (wcslen(opened.root) + 10 >= MAX_PATH) return fail(error, "Library path is too long.");
        wcscat(opened.root, L"\\Clumsier");
    }
    // Reserve room for a GUID, extension, and a second GUID for atomic writes.
    if (wcslen(opened.root) + 100 >= MAX_PATH) return fail(error, "Library path is too long.");
    if (!directory(opened.root, error)) return false;
    swprintf(path, MAX_PATH, L"%ls\\presets", opened.root);
    if (!directory(path, error)) return false;
    swprintf(path, MAX_PATH, L"%ls\\profiles", opened.root);
    if (!directory(path, error)) return false;
    *store = opened; return true;
}
static bool newId(wchar_t id[LIBRARY_ID_SIZE], char *error) {
    GUID guid;
    wchar_t text[40];
    if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(&guid, text, 40)) return fail(error, "Cannot create a new library ID.");
    // Drop the braces, keeping the usual 36-character GUID spelling.
    wmemcpy(id, text + 1, 36); id[36] = 0; return true;
}
static bool validId(const wchar_t *id) {
    size_t i;
    if (!id || wcslen(id) != 36) return false;
    for (i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (id[i] != L'-') return false; }
        else if (!iswxdigit(id[i])) return false;
    }
    return true;
}
static bool libraryPath(const PresetStore *store, bool profiles, const wchar_t *id, wchar_t *path, char *error) {
    if (!validId(id)) return fail(error, "Invalid library ID.");
    swprintf(path, MAX_PATH, L"%ls\\%ls\\%ls.json", store->root, profiles ? L"profiles" : L"presets", id);
    return true;
}
static char *readText(const wchar_t *path, size_t *length, char *error) {
    HANDLE file;
    LARGE_INTEGER size;
    DWORD read;
    char *text = NULL;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) { fail(error, "Cannot read the selected file."); return NULL; }
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 1 || size.QuadPart > PRESET_JSON_MAX) goto failed;
    text = malloc((size_t)size.QuadPart + 1);
    if (!text || !ReadFile(file, text, (DWORD)size.QuadPart, &read, NULL) || read != size.QuadPart || memchr(text, 0, read)) goto failed;
    CloseHandle(file); text[read] = 0; *length = read; return text;
failed:
    free(text); CloseHandle(file); fail(error, "Cannot read file: empty, too large, or incomplete."); return NULL;
}
static bool writeText(const wchar_t *path, const char *text, bool replace, char *error) {
    wchar_t temporary[MAX_PATH], id[LIBRARY_ID_SIZE];
    HANDLE file;
    DWORD written, length = (DWORD)strlen(text);
    bool saved;
    if (wcslen(path) + 42 >= MAX_PATH) return fail(error, "File path is too long.");
    if (!newId(id, error)) return false;
    swprintf(temporary, MAX_PATH, L"%ls.%ls.tmp", path, id);
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return fail(error, "Cannot create a temporary file beside the destination.");
    // An interrupted save leaves the old file intact. The temporary file lives
    // on the same volume so the final rename doesn't become a copy/delete.
    saved = WriteFile(file, text, length, &written, NULL) && written == length && FlushFileBuffers(file);
    CloseHandle(file);
    if (saved) saved = MoveFileExW(temporary, path, MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0)) != 0;
    if (!saved) { DeleteFileW(temporary); return fail(error, "Could not save the file. The previous file has been kept."); }
    return true;
}
bool presetFileRead(const wchar_t *path, Preset *preset, char *error) {
    size_t length;
    char *text = readText(path, &length, error);
    bool parsed;
    if (!text) return false;
    parsed = presetParse(text, length, preset, error); free(text); return parsed;
}
bool presetFileWrite(const wchar_t *path, const Preset *preset, char *error) {
    char *text = presetSerialize(preset, error);
    bool saved;
    if (!text) return false;
    saved = writeText(path, text, true, error); free(text); return saved;
}
bool presetStoreRead(const PresetStore *store, const wchar_t *id, Preset *preset, char *error) {
    wchar_t path[MAX_PATH];
    return libraryPath(store, false, id, path, error) && presetFileRead(path, preset, error);
}
static bool saveObject(const PresetStore *store, bool profiles, wchar_t *id, const char *text, char *error) {
    wchar_t path[MAX_PATH], chosen[LIBRARY_ID_SIZE];
    bool replace = *id != 0;
    if (replace) {
        if (!validId(id)) return fail(error, "Invalid library ID.");
        wcscpy(chosen, id);
    } else if (!newId(chosen, error)) return false;
    if (!libraryPath(store, profiles, chosen, path, error) || !writeText(path, text, replace, error)) return false;
    wcscpy(id, chosen); return true;
}
bool presetStoreSave(const PresetStore *store, wchar_t *id, const Preset *preset, char *error) {
    char *text = presetSerialize(preset, error);
    bool saved;
    if (!text) return false;
    saved = saveObject(store, false, id, text, error); free(text); return saved;
}
bool profileStoreRead(const PresetStore *store, const wchar_t *id, BaselineProfile *profile, char *error) {
    wchar_t path[MAX_PATH];
    size_t length;
    char *text;
    cJSON *root, *item;
    BaselineProfile parsed = {0};
    unsigned seen = 0;
    bool ok = true;
    if (!libraryPath(store, true, id, path, error)) return false;
    text = readText(path, &length, error); if (!text) return false;
    root = strstr(text, "\\u0000") ? NULL : cJSON_ParseWithLengthOpts(text, length + 1, NULL, 1);
    free(text);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return fail(error, "Invalid baseline profile object."); }
    cJSON_ArrayForEach(item, root) {
        unsigned bit = 0;
        if (!strcmp(item->string, "format_version")) { bit = 1; if (!cJSON_IsNumber(item) || item->valuedouble != 1) ok = false; }
        else if (!strcmp(item->string, "name")) {
            bit = 2;
            if (!cJSON_IsString(item) || !*item->valuestring || strlen(item->valuestring) >= PRESET_NAME_SIZE) ok = false;
            else strcpy(parsed.name, item->valuestring);
        } else if (!strcmp(item->string, "baseline_ms")) {
            bit = 4;
            if (!cJSON_IsNumber(item) || !(item->valuedouble >= 0 && item->valuedouble <= PING_MAX_MS)) ok = false;
            else { parsed.baseline_ms = (uint32_t)item->valuedouble; if (item->valuedouble != parsed.baseline_ms) ok = false; }
        } else ok = false;
        if (seen & bit) ok = false;
        seen |= bit;
    }
    cJSON_Delete(root);
    if (!ok || seen != 7) return fail(error, "Invalid baseline profile. The file has been left untouched.");
    *profile = parsed; return true;
}
bool profileStoreSave(const PresetStore *store, wchar_t *id, const BaselineProfile *profile, char *error) {
    cJSON *root;
    char *text;
    bool saved;
    if (!memchr(profile->name, 0, PRESET_NAME_SIZE) || !*profile->name || profile->baseline_ms > PING_MAX_MS)
        return fail(error, "Enter a profile name and a baseline between 0 and 60000 ms.");
    root = cJSON_CreateObject();
    if (!root || !cJSON_AddNumberToObject(root, "format_version", 1) || !cJSON_AddStringToObject(root, "name", profile->name) ||
        !cJSON_AddNumberToObject(root, "baseline_ms", profile->baseline_ms)) {
        cJSON_Delete(root); return fail(error, "Cannot allocate profile JSON.");
    }
    text = cJSON_Print(root); cJSON_Delete(root);
    if (!text) return fail(error, "Cannot allocate profile JSON.");
    saved = saveObject(store, true, id, text, error); free(text); return saved;
}
bool presetStoreDelete(const PresetStore *store, bool profiles, const wchar_t *id, char *error) {
    wchar_t path[MAX_PATH];
    if (!libraryPath(store, profiles, id, path, error)) return false;
    if (!DeleteFileW(path)) return fail(error, "Cannot delete the selected library file.");
    return true;
}
bool presetStoreList(const PresetStore *store, bool profiles, LibraryEntry *entries, size_t *count, size_t *skipped, char *error) {
    wchar_t pattern[MAX_PATH];
    WIN32_FIND_DATAW found;
    HANDLE search;
    *count = *skipped = 0;
    swprintf(pattern, MAX_PATH, L"%ls\\%ls\\*.json", store->root, profiles ? L"profiles" : L"presets");
    search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return true;
        return fail(error, "Cannot list the preset library.");
    }
    do {
        LibraryEntry entry = {0};
        char detail[NETWORK_ERROR_SIZE];
        Preset preset;
        BaselineProfile profile;
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (wcslen(found.cFileName) != 41) { ++*skipped; continue; }
        wmemcpy(entry.id, found.cFileName, 36);
        if (*count == LIBRARY_MAX_ITEMS || !validId(entry.id) ||
            !(profiles ? profileStoreRead(store, entry.id, &profile, detail) : presetStoreRead(store, entry.id, &preset, detail))) {
            ++*skipped; continue;
        }
        strcpy(entry.name, profiles ? profile.name : preset.name);
        entries[(*count)++] = entry;
    } while (FindNextFileW(search, &found));
    {
        DWORD reason = GetLastError();
        FindClose(search);
        if (reason != ERROR_NO_MORE_FILES) return fail(error, "Could not finish reading the preset library.");
    }
    return true;
}
