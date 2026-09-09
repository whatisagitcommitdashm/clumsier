#pragma once
#include <windows.h>
#include "core/preset.h"

#define LIBRARY_MAX_ITEMS 256
#define LIBRARY_ID_SIZE 40

typedef struct { wchar_t root[MAX_PATH]; } PresetStore;
typedef struct { wchar_t id[LIBRARY_ID_SIZE]; char name[PRESET_NAME_SIZE]; } LibraryEntry;
typedef struct { char name[PRESET_NAME_SIZE]; uint32_t baseline_ms; } BaselineProfile;

// Pass a dedicated root for tests, or NULL for %LOCALAPPDATA%\Clumsier.
// IDs are generated filenames, never names supplied by an imported preset.
bool presetStoreOpen(PresetStore *store, const wchar_t *root, char *error);
bool presetStoreList(const PresetStore *store, bool profiles, LibraryEntry *entries, size_t *count, size_t *skipped, char *error);
bool presetStoreRead(const PresetStore *store, const wchar_t *id, Preset *preset, char *error);
bool presetStoreSave(const PresetStore *store, wchar_t id[LIBRARY_ID_SIZE], const Preset *preset, char *error);
bool profileStoreRead(const PresetStore *store, const wchar_t *id, BaselineProfile *profile, char *error);
bool profileStoreSave(const PresetStore *store, wchar_t id[LIBRARY_ID_SIZE], const BaselineProfile *profile, char *error);
bool presetStoreDelete(const PresetStore *store, bool profiles, const wchar_t *id, char *error);
bool presetFileRead(const wchar_t *path, Preset *preset, char *error);
bool presetFileWrite(const wchar_t *path, const Preset *preset, char *error);
