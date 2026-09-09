#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "preset_editor.h"
#include "backends/windows/backend.h"
#include "hotkeys.h"
#include "windivert.h"

Ihandle *presetUIText(const char *value, int capacity) {
    Ihandle *field = IupText(NULL);
    IupSetInt(field, "NC", capacity - 1);
    IupSetAttribute(field, "EXPAND", "HORIZONTAL");
    IupStoreAttribute(field, "VALUE", value);
    return field;
}
Ihandle *presetUIChoice(const char *a, const char *b, const char *c, int selected) {
    Ihandle *list = IupList(NULL);
    IupSetAttribute(list, "DROPDOWN", "YES");
    IupSetStrAttributeId(list, "", 1, a);
    if (b) IupSetStrAttributeId(list, "", 2, b);
    if (c) IupSetStrAttributeId(list, "", 3, c);
    IupSetInt(list, "VALUE", selected + 1);
    return list;
}
Ihandle *presetUIButton(const char *title, Icallback callback) {
    Ihandle *button = IupButton(title, NULL);
    IupSetCallback(button, "ACTION", callback); return button;
}
void presetUIShow(Ihandle *control, bool visible) {
    // Hidden fields should not leave an empty hole in a box layout.
    IupSetAttribute(control, "FLOATING", visible ? "NO" : "YES");
    IupSetAttribute(control, "VISIBLE", visible ? "YES" : "NO");
}
bool presetUIReadText(Ihandle *field, char *value, size_t size) {
    const char *text = IupGetAttribute(field, "VALUE");
    if (!text) text = "";
    if (strlen(text) >= size) return false;
    strcpy(value, text); return true;
}
bool presetUIReadNumber(Ihandle *field, uint32_t limit, uint32_t *value) {
    const char *text = IupGetAttribute(field, "VALUE");
    const char *p;
    unsigned long number;
    char *end;
    if (!text || !*text) return false;
    for (p = text; *p; ++p) if (*p < '0' || *p > '9') return false;
    errno = 0; number = strtoul(text, &end, 10);
    if (errno || *end || number > limit) return false;
    *value = (uint32_t)number; return true;
}
bool presetUIValidateWindows(const Preset *preset, char *error) {
    char filter[NATIVE_FILTER_SIZE];
    const char *detail;
    UINT position;
    if (!presetValidate(preset, error) || !windowsBuildFilter(&preset->target, filter, error)) return false;
    if (!WinDivertHelperCompileFilter(filter, WINDIVERT_LAYER_NETWORK, NULL, 0, &detail, &position)) {
        snprintf(error, NETWORK_ERROR_SIZE, "Invalid Windows traffic filter at character %u: %s", position, detail);
        return false;
    }
    return true;
}

typedef struct {
    Preset draft;
    PresetStore *store;
    wchar_t *id;
    bool saved;
    bool advanced;
    size_t selected;
    Ihandle *dialog, *status, *name, *description, *mode, *policy, *loop;
    Ihandle *protocol, *direction, *address, *port, *native;
    Ihandle *list, *step_name, *note, *kind, *target, *inbound, *outbound;
    Ihandle *target_fields, *delay_fields, *traffic_fields, *inbound_fields, *outbound_fields;
} PresetEditor;
static PresetEditor editor;

static void notice(const char *message) { IupStoreAttribute(editor.status, "TITLE", message); }
static void updateFields(void) {
    bool target = editor.draft.mode == PRESET_TARGET_PING;
    bool lowest = IupGetInt(editor.kind, "VALUE") == 2;
    IupSetAttribute(editor.kind, "ACTIVE", target ? "YES" : "NO");
    IupSetAttribute(editor.target, "ACTIVE", target && !lowest ? "YES" : "NO");
    IupSetAttribute(editor.inbound, "ACTIVE", !target ? "YES" : "NO");
    IupSetAttribute(editor.outbound, "ACTIVE", !target ? "YES" : "NO");
    presetUIShow(editor.target_fields, target);
    presetUIShow(editor.delay_fields, !target);
    presetUIShow(editor.inbound_fields, editor.advanced || editor.draft.policy != DELAY_OUTBOUND);
    presetUIShow(editor.outbound_fields, editor.advanced || editor.draft.policy != DELAY_INBOUND);
    if (editor.dialog) IupRefresh(editor.dialog);
}
static void showStep(void) {
    PresetStep *step = &editor.draft.steps[editor.selected];
    IupStoreAttribute(editor.step_name, "VALUE", step->name);
    IupStoreAttribute(editor.note, "VALUE", step->note);
    IupSetInt(editor.kind, "VALUE", step->kind == STEP_LOWEST ? 2 : 1);
    IupSetInt(editor.target, "VALUE", (int)step->target_ms);
    IupSetInt(editor.inbound, "VALUE", (int)step->inbound_ms);
    IupSetInt(editor.outbound, "VALUE", (int)step->outbound_ms);
    updateFields();
}
static bool readStep(void) {
    PresetStep step = {0};
    if (!presetUIReadText(editor.step_name, step.name, sizeof(step.name)) || !*step.name ||
        !presetUIReadText(editor.note, step.note, sizeof(step.note))) {
        notice("Give the step a name; names and notes must fit their fields."); return false;
    }
    if (editor.draft.mode == PRESET_TARGET_PING) {
        step.kind = IupGetInt(editor.kind, "VALUE") == 2 ? STEP_LOWEST : STEP_TARGET;
        if (step.kind == STEP_TARGET && !presetUIReadNumber(editor.target, PING_MAX_MS, &step.target_ms)) {
            notice("Target ping must be a whole number from 0 to 60000 ms."); return false;
        }
    } else {
        step.kind = STEP_DELAY;
        if (!presetUIReadNumber(editor.inbound, LAG_MAX_MS, &step.inbound_ms) || !presetUIReadNumber(editor.outbound, LAG_MAX_MS, &step.outbound_ms)) {
            notice("Each delay must be a whole number from 0 to 15000 ms."); return false;
        }
    }
    editor.draft.steps[editor.selected] = step; return true;
}
static void updateList(void) {
    size_t i;
    IupSetAttribute(editor.list, "REMOVEITEM", "ALL");
    for (i = 0; i < editor.draft.step_count; ++i) {
        char label[160];
        snprintf(label, sizeof(label), "%u. %s", (unsigned)i + 1, editor.draft.steps[i].name);
        IupSetStrAttributeId(editor.list, "", (int)i + 1, label);
    }
    IupSetInt(editor.list, "VALUE", (int)editor.selected + 1);
}
static int selectStep(Ihandle *ih) {
    int next = IupGetInt(ih, "VALUE") - 1;
    if (next < 0 || next >= (int)editor.draft.step_count || next == (int)editor.selected) return IUP_DEFAULT;
    if (!readStep()) { IupSetInt(ih, "VALUE", (int)editor.selected + 1); return IUP_DEFAULT; }
    editor.selected = (size_t)next; updateList(); showStep(); return IUP_DEFAULT;
}
static int kindChanged(Ihandle *ih) { (void)ih; updateFields(); return IUP_DEFAULT; }
static int modeChanged(Ihandle *ih) {
    PresetMode mode = (PresetMode)(IupGetInt(ih, "VALUE") - 1);
    size_t i;
    if (mode == editor.draft.mode) return IUP_DEFAULT;
    if (!readStep()) { IupSetInt(ih, "VALUE", editor.draft.mode + 1); return IUP_DEFAULT; }
    if (IupAlarm("Change mode", "Changing mode resets all step values to zero. Names and notes are kept. Continue?", "Reset values", "Cancel", NULL) != 1) {
        IupSetInt(ih, "VALUE", editor.draft.mode + 1); return IUP_DEFAULT;
    }
    editor.draft.mode = mode;
    for (i = 0; i < editor.draft.step_count; ++i) {
        PresetStep *step = &editor.draft.steps[i];
        step->kind = mode == PRESET_TARGET_PING ? STEP_TARGET : STEP_DELAY;
        step->target_ms = step->inbound_ms = step->outbound_ms = 0;
    }
    showStep(); notice("Mode changed. Review the values for every step before saving."); return IUP_DEFAULT;
}
static int addStep(Ihandle *ih) {
    PresetStep *step;
    (void)ih;
    if (editor.draft.step_count == PRESET_MAX_STEPS) { notice("A preset can contain up to 64 steps."); return IUP_DEFAULT; }
    if (!readStep()) return IUP_DEFAULT;
    editor.selected = editor.draft.step_count++;
    step = &editor.draft.steps[editor.selected]; memset(step, 0, sizeof(*step));
    snprintf(step->name, sizeof(step->name), "Step %u", (unsigned)editor.selected + 1);
    step->kind = editor.draft.mode == PRESET_TARGET_PING ? STEP_TARGET : STEP_DELAY;
    updateList(); showStep(); return IUP_DEFAULT;
}
static int removeStep(Ihandle *ih) {
    (void)ih;
    if (editor.draft.step_count == 1) { notice("Keep at least one step."); return IUP_DEFAULT; }
    memmove(&editor.draft.steps[editor.selected], &editor.draft.steps[editor.selected + 1],
        (editor.draft.step_count - editor.selected - 1) * sizeof(PresetStep));
    --editor.draft.step_count;
    if (editor.selected == editor.draft.step_count) --editor.selected;
    updateList(); showStep(); return IUP_DEFAULT;
}
static int moveStep(Ihandle *ih) {
    int next = (int)editor.selected + IupGetInt(ih, "_DELTA");
    PresetStep swap;
    if (next < 0 || next >= (int)editor.draft.step_count || !readStep()) return IUP_DEFAULT;
    swap = editor.draft.steps[next]; editor.draft.steps[next] = editor.draft.steps[editor.selected];
    editor.draft.steps[editor.selected] = swap; editor.selected = (size_t)next;
    updateList(); showStep(); return IUP_DEFAULT;
}
static int savePreset(Ihandle *ih) {
    char error[NETWORK_ERROR_SIZE];
    uint32_t port;
    (void)ih;
    if (!readStep()) return IUP_DEFAULT;
    if (!presetUIReadText(editor.name, editor.draft.name, PRESET_NAME_SIZE) ||
        !presetUIReadText(editor.description, editor.draft.description, PRESET_NOTE_SIZE)) {
        notice("The name or description is too long."); return IUP_DEFAULT;
    }
    editor.draft.loop = IupGetInt(editor.loop, "VALUE") != 0;
    // Simple mode edits steps, not hidden traffic settings. In particular,
    // opening an imported preset must never replace its filter with defaults.
    if (editor.advanced) {
        editor.draft.policy = (DelayPolicy)(IupGetInt(editor.policy, "VALUE") - 1);
        memset(&editor.draft.target, 0, sizeof(editor.draft.target));
        if (!presetUIReadText(editor.native, editor.draft.target.native_filter, NATIVE_FILTER_SIZE)) {
            notice("The Windows filter is too long."); return IUP_DEFAULT;
        }
        if (*editor.draft.target.native_filter) strcpy(editor.draft.target.native_backend, "windivert");
        else {
            editor.draft.target.traffic.protocol = (TrafficProtocol)(IupGetInt(editor.protocol, "VALUE") - 1);
            editor.draft.target.traffic.direction = (TrafficDirection)(IupGetInt(editor.direction, "VALUE") - 1);
            if (!presetUIReadNumber(editor.port, 65535, &port) ||
                !presetUIReadText(editor.address, editor.draft.target.traffic.remote_address, TRAFFIC_ADDRESS_SIZE)) {
                notice("Enter a numeric IP address (or leave blank) and a port from 0 to 65535."); return IUP_DEFAULT;
            }
            editor.draft.target.traffic.remote_port = (uint16_t)port;
        }
    }
    if (!presetUIValidateWindows(&editor.draft, error) || !presetStoreSave(editor.store, editor.id, &editor.draft, error)) {
        notice(error); return IUP_DEFAULT;
    }
    editor.saved = true; IupHide(editor.dialog); return IUP_DEFAULT;
}
static int cancelPreset(Ihandle *ih) { (void)ih; IupHide(editor.dialog); return IUP_DEFAULT; }
static Ihandle *row(const char *label, Ihandle *control) {
    Ihandle *name = IupLabel(label);
    IupSetAttribute(name, "SIZE", "85x"); return IupHbox(name, control, NULL);
}
static Ihandle *editorHint(const char *text) {
    Ihandle *label = IupLabel(text);
    IupSetAttributes(label, "WORDWRAP=YES, EXPAND=HORIZONTAL, SIZE=400x28");
    return label;
}
bool presetUIEdit(Ihandle *parent, PresetStore *store, wchar_t *id, Preset *preset, bool advanced) {
    Ihandle *body, *up, *down;
    Preset defaults;
    bool customTraffic;
    presetDefault(&defaults);
    customTraffic = preset->policy != defaults.policy || !captureTargetsEqual(&preset->target, &defaults.target);
    memset(&editor, 0, sizeof(editor)); editor.draft = *preset; editor.store = store; editor.id = id;
    editor.advanced = advanced;
    editor.name = presetUIText(preset->name, PRESET_NAME_SIZE);
    editor.description = presetUIText(preset->description, PRESET_NOTE_SIZE);
    editor.mode = presetUIChoice("Added delay", "Target ping", NULL, preset->mode);
    editor.policy = presetUIChoice("Inbound", "Outbound", "Both (split target compensation)", preset->policy);
    editor.loop = IupToggle("Wrap at sequence ends (Next returns to the first step)", NULL); IupSetInt(editor.loop, "VALUE", preset->loop);
    editor.protocol = presetUIChoice("Any", "TCP", "UDP", preset->target.traffic.protocol);
    editor.direction = presetUIChoice("Both", "Inbound", "Outbound", preset->target.traffic.direction);
    editor.address = presetUIText(preset->target.traffic.remote_address, TRAFFIC_ADDRESS_SIZE);
    editor.port = presetUIText("0", 6); IupSetInt(editor.port, "VALUE", preset->target.traffic.remote_port);
    editor.native = presetUIText(preset->target.native_filter, NATIVE_FILTER_SIZE);
    editor.list = IupList(NULL); IupSetAttribute(editor.list, "EXPAND", "HORIZONTAL"); IupSetAttribute(editor.list, "VISIBLELINES", "4");
    editor.step_name = presetUIText("", PRESET_NAME_SIZE); editor.note = presetUIText("", PRESET_NOTE_SIZE);
    editor.kind = presetUIChoice("Numeric target", "Lowest available", NULL, 0);
    editor.target = presetUIText("0", 6); editor.inbound = presetUIText("0", 6); editor.outbound = presetUIText("0", 6);
    editor.status = IupLabel("Values are saved when you change steps or choose Save. Baseline belongs to your local profile.");
    IupSetAttributes(editor.status, "WORDWRAP=YES, EXPAND=HORIZONTAL, SIZE=420x32");
    up = presetUIButton("Move up", moveStep); down = presetUIButton("Move down", moveStep);
    IupSetInt(up, "_DELTA", -1); IupSetInt(down, "_DELTA", 1);
    editor.traffic_fields = IupVbox(row("Delay direction", editor.policy),
        IupLabel("Traffic selection applies to every step:"),
        IupHbox(IupLabel("Protocol"), editor.protocol, IupLabel("Capture direction"), editor.direction, NULL),
        row("Remote IP (optional)", editor.address), row("Remote port (0 = any)", editor.port),
        row("Windows filter", editor.native),
        IupLabel("Optional Windows filter overrides the structured traffic fields above; verify its direction matches the delay policy."), NULL);
    editor.target_fields = IupHbox(editor.kind, IupLabel("Desired ping (ms)"), editor.target, NULL);
    editor.inbound_fields = IupHbox(IupLabel(!advanced && preset->policy == DELAY_INBOUND ? "Added delay (ms)" : "Inbound delay (ms)"), editor.inbound, NULL);
    editor.outbound_fields = IupHbox(IupLabel(!advanced && preset->policy == DELAY_OUTBOUND ? "Added delay (ms)" : "Outbound delay (ms)"), editor.outbound, NULL);
    editor.delay_fields = IupHbox(editor.inbound_fields, editor.outbound_fields, NULL);
    body = IupVbox(row("Sequence name", editor.name), row("Description", editor.description),
        row("Step values", editor.mode),
        editorHint("Target ping: the ping you want. Added delay: extra waiting time to add."),
        editor.traffic_fields,
        editorHint(advanced ? "Name each step and set its value below." :
            customTraffic ? "This sequence has custom network settings. They are kept; Advanced mode lets you inspect them." :
            "Set your steps below. No network setup needed."),
        editor.list, IupHbox(presetUIButton("Add step", addStep), presetUIButton("Remove step", removeStep), up, down, NULL),
        row("Step name", editor.step_name), row("Step note", editor.note),
        editor.target_fields, editor.delay_fields,
        editor.loop, editor.status, IupHbox(presetUIButton("Save sequence", savePreset), presetUIButton("Cancel", cancelPreset), NULL), NULL);
    presetUIShow(editor.traffic_fields, advanced);
    IupSetAttributes(body, "MARGIN=10x10, GAP=5");
    editor.dialog = IupDialog(body);
    IupSetAttributes(editor.dialog, "TITLE=Edit sequence, SIZE=500x");
    IupSetAttributeHandle(editor.dialog, "PARENTDIALOG", parent);
    IupSetCallback(editor.dialog, "CLOSE_CB", cancelPreset);
    IupSetCallback(editor.list, "VALUECHANGED_CB", selectStep);
    IupSetCallback(editor.mode, "VALUECHANGED_CB", modeChanged);
    IupSetCallback(editor.kind, "VALUECHANGED_CB", kindChanged);
    updateList(); showStep();
    hotkeysPause(); IupPopup(editor.dialog, IUP_CENTERPARENT, IUP_CENTERPARENT); hotkeysResume();
    IupDestroy(editor.dialog);
    if (editor.saved) *preset = editor.draft;
    return editor.saved;
}

static struct { Ihandle *dialog, *name, *baseline, *status; BaselineProfile draft; PresetStore *store; wchar_t *id; bool saved; } profileEditor;
static int saveProfile(Ihandle *ih) {
    char error[NETWORK_ERROR_SIZE];
    (void)ih;
    if (!presetUIReadText(profileEditor.name, profileEditor.draft.name, PRESET_NAME_SIZE) ||
        !presetUIReadNumber(profileEditor.baseline, PING_MAX_MS, &profileEditor.draft.baseline_ms))
        strcpy(error, "Enter a name and a whole baseline ping from 0 to 60000 ms.");
    else if (profileStoreSave(profileEditor.store, profileEditor.id, &profileEditor.draft, error)) {
        profileEditor.saved = true; IupHide(profileEditor.dialog); return IUP_DEFAULT;
    }
    IupStoreAttribute(profileEditor.status, "TITLE", error); return IUP_DEFAULT;
}
static int cancelProfile(Ihandle *ih) { (void)ih; IupHide(profileEditor.dialog); return IUP_DEFAULT; }
bool profileUIEdit(Ihandle *parent, PresetStore *store, wchar_t *id, BaselineProfile *profile) {
    Ihandle *body;
    memset(&profileEditor, 0, sizeof(profileEditor)); profileEditor.draft = *profile; profileEditor.store = store; profileEditor.id = id;
    profileEditor.name = presetUIText(profile->name, PRESET_NAME_SIZE);
    profileEditor.baseline = presetUIText("0", 6); IupSetInt(profileEditor.baseline, "VALUE", (int)profile->baseline_ms);
    profileEditor.status = IupLabel("Measure your stable ping with Clumsier stopped. This profile is not included in exported presets.");
    IupSetAttributes(profileEditor.status, "WORDWRAP=YES, EXPAND=HORIZONTAL, SIZE=320x40");
    body = IupVbox(row("Server / profile name", profileEditor.name), row("Baseline ping (ms)", profileEditor.baseline),
        profileEditor.status, IupHbox(presetUIButton("Save", saveProfile), presetUIButton("Cancel", cancelProfile), NULL), NULL);
    IupSetAttributes(body, "MARGIN=10x10, GAP=8"); profileEditor.dialog = IupDialog(body);
    IupSetAttributes(profileEditor.dialog, "TITLE=Baseline profile, SIZE=370x"); IupSetAttributeHandle(profileEditor.dialog, "PARENTDIALOG", parent);
    IupSetCallback(profileEditor.dialog, "CLOSE_CB", cancelProfile);
    hotkeysPause(); IupPopup(profileEditor.dialog, IUP_CENTERPARENT, IUP_CENTERPARENT); hotkeysResume();
    IupDestroy(profileEditor.dialog); if (profileEditor.saved) *profile = profileEditor.draft;
    return profileEditor.saved;
}
