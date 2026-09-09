#include <stdio.h>
#include <string.h>
#include "preset.h"

static bool textValid(const char *text, size_t capacity, bool required) {
    return memchr(text, 0, capacity) && (!required || text[0]);
}
void presetDefault(Preset *preset) {
    memset(preset, 0, sizeof(*preset));
    strcpy(preset->name, "New preset");
    preset->mode = PRESET_TARGET_PING;
    preset->policy = DELAY_INBOUND;
    preset->target.traffic.protocol = TRAFFIC_ANY;
    preset->target.traffic.direction = TRAFFIC_INBOUND;
    preset->step_count = 1;
    strcpy(preset->steps[0].name, "Step 1");
    preset->steps[0].kind = STEP_TARGET;
    preset->steps[0].target_ms = 100;
}
bool captureTargetsEqual(const CaptureTarget *a, const CaptureTarget *b) {
    return a->traffic.protocol == b->traffic.protocol && a->traffic.direction == b->traffic.direction &&
        a->traffic.remote_port == b->traffic.remote_port && !strcmp(a->traffic.remote_address, b->traffic.remote_address) &&
        !strcmp(a->native_backend, b->native_backend) && !strcmp(a->native_filter, b->native_filter);
}
bool presetValidate(const Preset *preset, char *error) {
    size_t i;
    if (!textValid(preset->name, sizeof(preset->name), true) ||
        !textValid(preset->description, sizeof(preset->description), false) ||
        preset->mode < PRESET_ADDED_DELAY || preset->mode > PRESET_TARGET_PING ||
        preset->policy < DELAY_INBOUND || preset->policy > DELAY_BOTH ||
        !preset->step_count || preset->step_count > PRESET_MAX_STEPS) {
        strcpy(error, "A preset needs a name, a valid mode/direction policy, and 1-64 steps."); return false;
    }
    if (!captureTargetValidate(&preset->target, error)) return false;
    if (!preset->target.native_filter[0] &&
        ((preset->target.traffic.direction == TRAFFIC_INBOUND && preset->policy != DELAY_INBOUND) ||
         (preset->target.traffic.direction == TRAFFIC_OUTBOUND && preset->policy != DELAY_OUTBOUND))) {
        strcpy(error, "The capture direction must include every direction being delayed."); return false;
    }
    for (i = 0; i < preset->step_count; ++i) {
        const PresetStep *step = &preset->steps[i];
        if (!textValid(step->name, sizeof(step->name), true) || !textValid(step->note, sizeof(step->note), false) ||
            (preset->mode == PRESET_ADDED_DELAY && step->kind != STEP_DELAY) ||
            (preset->mode == PRESET_TARGET_PING && step->kind != STEP_TARGET && step->kind != STEP_LOWEST) ||
            step->target_ms > PING_MAX_MS || step->inbound_ms > LAG_MAX_MS || step->outbound_ms > LAG_MAX_MS ||
            (step->kind != STEP_DELAY && (step->inbound_ms || step->outbound_ms)) ||
            (step->kind != STEP_TARGET && step->target_ms) ||
            (step->kind == STEP_DELAY && ((preset->policy == DELAY_INBOUND && step->outbound_ms) ||
             (preset->policy == DELAY_OUTBOUND && step->inbound_ms)))) {
            snprintf(error, NETWORK_ERROR_SIZE, "Step %u has an invalid name, type, or delay value.", (unsigned)i + 1); return false;
        }
    }
    return true;
}
bool presetResolve(const Preset *preset, size_t step_index, bool has_baseline, uint32_t baseline, StepResult *result, char *error) {
    StepResult resolved = {0};
    const PresetStep *step;
    uint32_t extra;
    if (!presetValidate(preset, error)) return false;
    if (step_index >= preset->step_count) { strcpy(error, "That step does not exist."); return false; }
    if (preset->mode == PRESET_TARGET_PING && (!has_baseline || baseline > PING_MAX_MS)) {
        strcpy(error, "Select a baseline profile (0-60000 ms) for this server first."); return false;
    }
    step = &preset->steps[step_index];
    resolved.lag.inbound = preset->policy != DELAY_OUTBOUND;
    resolved.lag.outbound = preset->policy != DELAY_INBOUND;
    if (preset->mode == PRESET_ADDED_DELAY) {
        resolved.lag.inbound_ms = step->inbound_ms;
        resolved.lag.outbound_ms = step->outbound_ms;
    } else {
        extra = step->kind == STEP_TARGET && step->target_ms > baseline ? step->target_ms - baseline : 0;
        resolved.below_baseline = step->kind == STEP_TARGET && step->target_ms < baseline;
        if (preset->policy == DELAY_INBOUND) resolved.lag.inbound_ms = extra;
        else if (preset->policy == DELAY_OUTBOUND) resolved.lag.outbound_ms = extra;
        else {
            // Keep the total exact when splitting an odd millisecond.
            resolved.lag.inbound_ms = extra / 2;
            resolved.lag.outbound_ms = extra - resolved.lag.inbound_ms;
        }
        resolved.estimated_ping_ms = baseline + extra;
    }
    resolved.lag.enabled = resolved.lag.inbound_ms || resolved.lag.outbound_ms;
    if (!lagSettingsValidate(&resolved.lag, error)) return false;
    *result = resolved;
    return true;
}
