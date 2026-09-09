#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "platform/windows/preset_store.h"

static void writeRaw(const wchar_t *path, const char *text) {
    DWORD written, length = (DWORD)strlen(text);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    assert(file != INVALID_HANDLE_VALUE); assert(WriteFile(file, text, length, &written, NULL) && written == length); CloseHandle(file);
}
int main(void) {
    wchar_t temp[MAX_PATH], root[MAX_PATH], path[MAX_PATH], exportPath[MAX_PATH];
    wchar_t id[LIBRARY_ID_SIZE] = {0}, imported[LIBRARY_ID_SIZE] = {0}, profileId[LIBRARY_ID_SIZE] = {0};
    wchar_t badId[] = L"11111111-1111-1111-1111-111111111111";
    PresetStore store, reopened;
    Preset preset, loaded, before;
    BaselineProfile profile = {"Server A", 50}, readProfile;
    LibraryEntry entries[LIBRARY_MAX_ITEMS];
    size_t count, skipped;
    char error[NETWORK_ERROR_SIZE];
    HANDLE lock;
    assert(presetFileRead(L"examples\\four-leaps.json", &preset, error));
    assert(preset.step_count == 4 && preset.steps[0].target_ms == 200 && preset.steps[1].kind == STEP_LOWEST);
    assert(GetTempPathW(MAX_PATH, temp)); assert(GetTempFileNameW(temp, L"cps", 0, root)); assert(DeleteFileW(root));
    assert(presetStoreOpen(&store, root, error));
    presetDefault(&preset); strcpy(preset.name, "Shared name"); preset.steps[0].target_ms = 200;
    assert(presetStoreSave(&store, id, &preset, error) && *id);
    assert(presetStoreRead(&store, id, &loaded, error)); assert(!memcmp(&preset, &loaded, sizeof(preset)));
    assert(profileStoreSave(&store, profileId, &profile, error));
    assert(profileStoreRead(&store, profileId, &readProfile, error)); assert(readProfile.baseline_ms == 50);
    assert(presetStoreOpen(&reopened, root, error));
    assert(presetStoreRead(&reopened, id, &loaded, error)); assert(loaded.steps[0].target_ms == 200);
    swprintf(exportPath, MAX_PATH, L"%ls\\shared.json", root);
    assert(presetFileWrite(exportPath, &preset, error));
    assert(presetFileRead(exportPath, &loaded, error));
    assert(presetStoreSave(&store, imported, &loaded, error)); assert(wcscmp(id, imported));
    assert(presetStoreList(&store, false, entries, &count, &skipped, error) && count == 2 && !skipped);
    assert(presetStoreList(&store, true, entries, &count, &skipped, error) && count == 1 && !skipped);
    // Sharing never copies the local baseline into the preset object or file.
    profile.baseline_ms = 250; assert(profileStoreSave(&store, profileId, &profile, error));
    assert(presetFileRead(exportPath, &loaded, error)); assert(loaded.steps[0].target_ms == 200);
    swprintf(path, MAX_PATH, L"%ls\\presets\\%ls.json", root, id);
    lock = CreateFileW(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL); assert(lock != INVALID_HANDLE_VALUE);
    preset.steps[0].target_ms = 300;
    assert(!presetStoreSave(&store, id, &preset, error)); CloseHandle(lock);
    assert(presetStoreRead(&store, id, &loaded, error)); assert(loaded.steps[0].target_ms == 200);
    assert(!presetStoreRead(&store, L"..\\shared", &loaded, error));
    assert(!presetStoreDelete(&store, false, L"..\\shared", error));
    assert(presetStoreSave(&store, id, &preset, error)); assert(presetStoreRead(&store, id, &loaded, error));
    assert(loaded.steps[0].target_ms == 300);
    swprintf(path, MAX_PATH, L"%ls\\presets\\%ls.json", root, badId); writeRaw(path, "{broken");
    before = loaded; assert(!presetStoreRead(&store, badId, &loaded, error)); assert(!memcmp(&loaded, &before, sizeof(loaded)));
    assert(presetStoreList(&store, false, entries, &count, &skipped, error) && count == 2 && skipped == 1);
    assert(DeleteFileW(path));
    swprintf(path, MAX_PATH, L"%ls\\profiles\\%ls.json", root, profileId);
    writeRaw(path, "[]"); assert(!profileStoreRead(&store, profileId, &readProfile, error));
    writeRaw(path, "{\"format_version\":1,\"name\":\"A\",\"baseline_ms\":2,\"baseline_ms\":3}");
    assert(!profileStoreRead(&store, profileId, &readProfile, error));
    writeRaw(path, "{\"format_version\":1,\"name\":\"A\",\"baseline_ms\":2.5}");
    assert(!profileStoreRead(&store, profileId, &readProfile, error));
    assert(profileStoreSave(&store, profileId, &profile, error));
    assert(presetStoreDelete(&store, true, profileId, error));
    assert(presetStoreDelete(&store, false, id, error)); assert(presetStoreDelete(&store, false, imported, error));
    assert(DeleteFileW(exportPath));
    swprintf(path, MAX_PATH, L"%ls\\presets", root); assert(RemoveDirectoryW(path));
    swprintf(path, MAX_PATH, L"%ls\\profiles", root); assert(RemoveDirectoryW(path));
    assert(RemoveDirectoryW(root)); // Also proves failed writes left no temporary files.
    puts("PASS Windows library: reopen, import/export, duplicate IDs, local baselines, atomic save failure, malformed files and traversal rejection");
    return 0;
}
