#pragma once
#include <wchar.h>
#include "core/preset.h"
#define LIBRARY_MAX_ITEMS 256
#define LIBRARY_ID_SIZE 40
// Keep the bridge's storage interface compatible with the Windows adapter.
// Linux filesystem operations use Qt Unicode paths and atomic saves.
typedef struct { wchar_t root[4096]; } PresetStore;
typedef struct { wchar_t id[LIBRARY_ID_SIZE]; char name[PRESET_NAME_SIZE]; } LibraryEntry;
typedef struct { char name[PRESET_NAME_SIZE]; uint32_t baseline_ms; } BaselineProfile;
bool presetStoreOpen(PresetStore *, const wchar_t *, char *);
bool presetStoreList(const PresetStore *, bool, LibraryEntry *, size_t *, size_t *, char *);
bool presetStoreRead(const PresetStore *, const wchar_t *, Preset *, char *);
bool presetStoreSave(const PresetStore *, wchar_t *, const Preset *, char *);
bool profileStoreRead(const PresetStore *, const wchar_t *, BaselineProfile *, char *);
bool profileStoreSave(const PresetStore *, wchar_t *, const BaselineProfile *, char *);
bool presetStoreDelete(const PresetStore *, bool, const wchar_t *, char *);
bool presetFileRead(const wchar_t *, Preset *, char *);
bool presetFileWrite(const wchar_t *, const Preset *, char *);
