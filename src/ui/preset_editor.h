#pragma once
#include "iup.h"
#include "core/preset.h"
#include "platform/windows/preset_store.h"

Ihandle *presetUIText(const char *value, int capacity);
Ihandle *presetUIChoice(const char *a, const char *b, const char *c, int selected);
Ihandle *presetUIButton(const char *title, Icallback callback);
bool presetUIReadText(Ihandle *field, char *value, size_t size);
bool presetUIReadNumber(Ihandle *field, uint32_t limit, uint32_t *value);
bool presetUIValidateWindows(const Preset *preset, char *error);
// The editor owns a draft. Cancel, validation errors, and failed saves never
// change the caller's preset or the running controller.
bool presetUIEdit(Ihandle *parent, PresetStore *store, wchar_t *id, Preset *preset, bool advanced);
void presetUIShow(Ihandle *control, bool visible);
bool profileUIEdit(Ihandle *parent, PresetStore *store, wchar_t *id, BaselineProfile *profile);
