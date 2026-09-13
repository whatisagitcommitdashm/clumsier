#pragma once
#include "network.h"

#define PRESET_NAME_SIZE 96
#define PRESET_NOTE_SIZE 256
#define PRESET_MAX_STEPS 64
#define PRESET_JSON_MAX (256 * 1024)
#define PING_MAX_MS 60000

typedef enum { PRESET_ADDED_DELAY, PRESET_TARGET_PING } PresetMode;
typedef enum { DELAY_INBOUND, DELAY_OUTBOUND, DELAY_BOTH } DelayPolicy;
typedef enum { STEP_DELAY, STEP_TARGET, STEP_LOWEST } StepKind;
typedef struct {
    char name[PRESET_NAME_SIZE];
    char note[PRESET_NOTE_SIZE];
    // Only the fields belonging to this kind may be nonzero. Validation keeps
    // stale editor values out of the shared file (Lowest has no numeric value).
    StepKind kind;
    uint32_t target_ms;
    uint32_t inbound_ms, outbound_ms;
} PresetStep;
typedef struct {
    char name[PRESET_NAME_SIZE];
    char description[PRESET_NOTE_SIZE];
    // A portable server label, never another player's baseline or local ID.
    char server[PRESET_NAME_SIZE];
    CaptureTarget target;
    PresetMode mode;
    DelayPolicy policy;
    bool loop;
    size_t step_count;
    PresetStep steps[PRESET_MAX_STEPS];
} Preset;
typedef struct {
    LagSettings lag;
    bool below_baseline;
    // Meaningful only for target-ping mode; it is an estimate, not a measurement.
    uint32_t estimated_ping_ms;
} StepResult;

void presetDefault(Preset *preset);
bool presetValidate(const Preset *preset, char *error);
bool presetResolve(const Preset *preset, size_t step, bool has_baseline, uint32_t baseline, StepResult *result, char *error);
// length excludes any terminator; the input need not be NUL-terminated. Failed
// parsing leaves the output untouched. All error buffers use NETWORK_ERROR_SIZE.
bool presetParse(const char *json, size_t length, Preset *preset, char *error);
// Caller frees the returned text with free(). NULL means validation/allocation failure.
char *presetSerialize(const Preset *preset, char *error);
bool captureTargetsEqual(const CaptureTarget *a, const CaptureTarget *b);
