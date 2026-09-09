#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/controller.h"

static char error[NETWORK_ERROR_SIZE];
static Preset example(void) {
    Preset preset;
    size_t i;
    const uint32_t targets[] = {200, 0, 150, 100};
    presetDefault(&preset); strcpy(preset.name, "Four leaps"); preset.step_count = 4;
    for (i = 0; i < 4; ++i) {
        memset(&preset.steps[i], 0, sizeof(PresetStep));
        snprintf(preset.steps[i].name, PRESET_NAME_SIZE, "Leap %u", (unsigned)i + 1);
        preset.steps[i].kind = i == 1 ? STEP_LOWEST : STEP_TARGET;
        preset.steps[i].target_ms = targets[i];
    }
    return preset;
}
static void testCalculations(void) {
    Preset preset = example(), invalid;
    StepResult result, before;
    const uint32_t expected[] = {150, 0, 100, 50};
    size_t i;
    assert(presetValidate(&preset, error));
    for (i = 0; i < 4; ++i) {
        assert(presetResolve(&preset, i, true, 50, &result, error));
        assert(result.lag.inbound_ms == expected[i] && result.lag.outbound_ms == 0);
        assert(result.lag.enabled == (expected[i] > 0));
        assert(result.estimated_ping_ms == expected[i] + 50);
    }
    assert(presetResolve(&preset, 0, true, 250, &result, error));
    assert(result.below_baseline && !result.lag.enabled && result.estimated_ping_ms == 250);
    assert(preset.steps[0].target_ms == 200); // Never clamp the shared target itself.
    before = result;
    assert(!presetResolve(&preset, 0, false, 0, &result, error)); assert(!memcmp(&before, &result, sizeof(result)));
    assert(!presetResolve(&preset, 4, true, 50, &result, error));
    preset.policy = DELAY_BOTH; preset.target.traffic.direction = TRAFFIC_BOTH;
    assert(presetResolve(&preset, 0, true, 51, &result, error));
    assert(result.lag.inbound_ms == 74 && result.lag.outbound_ms == 75);
    // Each split preserves the total; changing the local baseline never edits the preset.
    for (i = 0; i <= 250; ++i) {
        assert(presetResolve(&preset, 0, true, (uint32_t)i, &result, error));
        assert(result.lag.inbound_ms + result.lag.outbound_ms == (i < 200 ? 200 - i : 0));
    }
    preset.steps[0].target_ms = 30000;
    assert(presetResolve(&preset, 0, true, 0, &result, error));
    preset.steps[0].target_ms = 30001;
    assert(!presetResolve(&preset, 0, true, 0, &result, error));
    invalid = example(); invalid.target.traffic.direction = TRAFFIC_OUTBOUND;
    assert(!presetValidate(&invalid, error));
    invalid = example(); invalid.step_count = PRESET_MAX_STEPS + 1;
    assert(!presetValidate(&invalid, error));
    invalid = example(); invalid.steps[1].target_ms = 30;
    assert(!presetValidate(&invalid, error));
    presetDefault(&preset); preset.mode = PRESET_ADDED_DELAY; preset.policy = DELAY_BOTH;
    preset.target.traffic.direction = TRAFFIC_BOTH; preset.steps[0].kind = STEP_DELAY;
    preset.steps[0].target_ms = 0; preset.steps[0].inbound_ms = 25; preset.steps[0].outbound_ms = 5;
    assert(presetResolve(&preset, 0, false, 0, &result, error));
    assert(result.lag.inbound_ms == 25 && result.lag.outbound_ms == 5);
    puts("PASS preset calculations: four leaps, lowest, unreachable targets, baseline independence, odd splits and limits");
}
static void badJson(const char *json) {
    Preset preset = example(), before = preset;
    assert(!presetParse(json, strlen(json), &preset, error)); assert(!memcmp(&preset, &before, sizeof(preset)));
}
static void replaceAndReject(const char *json, const char *find, const char *replacement) {
    const char *at = strstr(json, find);
    char *edited;
    size_t prefix;
    assert(at); prefix = (size_t)(at - json);
    edited = malloc(strlen(json) + strlen(replacement) + 1); assert(edited);
    memcpy(edited, json, prefix); strcpy(edited + prefix, replacement); strcat(edited, at + strlen(find));
    badJson(edited); free(edited);
}
static void testJson(void) {
    Preset preset = example(), loaded, before;
    char *json, *bounded, *oversize;
    size_t length, i;
    strcpy(preset.description, "Quotes \" and slashes \\ and newlines\n are preserved.");
    strcpy(preset.steps[1].note, "No delay, regardless of who imports this.");
    json = presetSerialize(&preset, error); assert(json); length = strlen(json);
    assert(!strstr(json, "baseline"));
    assert(presetParse(json, length, &loaded, error)); assert(!memcmp(&preset, &loaded, sizeof(preset)));
    bounded = malloc(length); assert(bounded); memcpy(bounded, json, length);
    assert(presetParse(bounded, length, &loaded, error)); free(bounded); // No terminator required from callers.
    replaceAndReject(json, "\"format_version\":\t1", "\"format_version\": 2");
    replaceAndReject(json, "\"target_ms\":\t200", "\"target_ms\": 200.5");
    replaceAndReject(json, "\"target_ms\":\t200", "\"target_ms\": -1");
    replaceAndReject(json, "\"target_ms\":\t200", "\"target_ms\": 1e999");
    replaceAndReject(json, "\"target_ms\":\t200", "\"target_ms\": \"200\"");
    replaceAndReject(json, "\"target_ms\":\t200", "\"target_ms\": 200, \"target_ms\": 100");
    replaceAndReject(json, "\"target_ms\":\t200", "\"target_ms\": 200, \"inbound_ms\": 50");
    replaceAndReject(json, "Four leaps", "Bad\\u0000name");
    replaceAndReject(json, "\"effect\":\t\"lag\"", "\"effect\": \"drop\"");
    replaceAndReject(json, "\"policy\":\t\"inbound\"", "\"policy\": \"sideways\"");
    replaceAndReject(json, "\"loop\":\tfalse", "\"loop\": false, \"unknown\": 1");
    before = loaded;
    for (i = 0; i < length - 1; i += 17) {
        assert(!presetParse(json, i, &loaded, error)); assert(!memcmp(&before, &loaded, sizeof(loaded)));
    }
    oversize = malloc(PRESET_JSON_MAX + 1); assert(oversize); memset(oversize, ' ', PRESET_JSON_MAX + 1);
    assert(!presetParse(oversize, PRESET_JSON_MAX + 1, &loaded, error)); free(oversize);
    badJson("[]"); badJson("null"); badJson("{} junk"); badJson("{\"name\":\"bad}");
    free(json);
    puts("PASS preset JSON: round trip, escapes, bounded buffers, malformed/duplicate/unknown fields and unchanged output on failure");
}
typedef struct { bool running, fail; LagSettings lag; } Fake;
static bool start(void *ctx, const CaptureTarget *target, const LagSettings *lag, char *message) {
    Fake *fake = ctx; (void)target; (void)message; fake->lag = *lag; fake->running = true; return true;
}
static bool apply(void *ctx, const LagSettings *lag, char *message) {
    Fake *fake = ctx;
    if (fake->fail) { strcpy(message, "Simulated backend failure"); return false; }
    fake->lag = *lag; return true;
}
static void stop(void *ctx) { ((Fake*)ctx)->running = false; }
static bool running(void *ctx) { return ((Fake*)ctx)->running; }
static void testSequence(void) {
    const NetworkBackendOps ops = {start, apply, stop, running};
    Fake fake = {0};
    NetworkBackend backend = {&ops, &fake};
    AppController app, before;
    Preset preset = example(), changed;
    controllerInit(&app, backend);
    assert(!controllerExecute(&app, ACTION_NEXT_STEP, error));
    assert(controllerLoadPreset(&app, &preset, true, 50, error));
    assert(!fake.running && app.active_step == 0 && app.lag.inbound_ms == 150);
    assert(controllerExecute(&app, ACTION_PREVIOUS_STEP, error) && app.active_step == 0);
    assert(controllerExecute(&app, ACTION_START_CAPTURE, error));
    fake.fail = true; before = app;
    assert(!controllerExecute(&app, ACTION_NEXT_STEP, error)); assert(!memcmp(&app, &before, sizeof(app)));
    assert(!controllerLoadPreset(&app, &preset, true, 80, error)); assert(!memcmp(&app, &before, sizeof(app)));
    fake.fail = false;
    assert(controllerExecute(&app, ACTION_NEXT_STEP, error)); assert(app.active_step == 1 && !fake.lag.enabled && fake.running);
    assert(controllerExecute(&app, ACTION_NEXT_STEP, error)); assert(app.active_step == 2 && fake.lag.inbound_ms == 100);
    assert(controllerExecute(&app, ACTION_NEXT_STEP, error)); assert(app.active_step == 3);
    assert(controllerExecute(&app, ACTION_NEXT_STEP, error)); assert(app.active_step == 3);
    assert(controllerExecute(&app, ACTION_RESET_SEQUENCE, error)); assert(app.active_step == 0 && fake.running);
    changed = preset; changed.target.traffic.protocol = TRAFFIC_TCP; before = app;
    assert(!controllerLoadPreset(&app, &changed, true, 50, error)); assert(!memcmp(&app, &before, sizeof(app)));
    changed = preset; changed.steps[3].target_ms = 60000;
    assert(!controllerLoadPreset(&app, &changed, true, 50, error)); assert(!memcmp(&app, &before, sizeof(app)));
    changed = preset; changed.loop = true;
    assert(controllerLoadPreset(&app, &changed, true, 80, error)); assert(fake.lag.inbound_ms == 120);
    assert(controllerExecute(&app, ACTION_PREVIOUS_STEP, error) && app.active_step == 3);
    assert(controllerExecute(&app, ACTION_NEXT_STEP, error) && app.active_step == 0);
    changed.steps[0].target_ms = 300; // A saved editor draft cannot mutate the running snapshot.
    assert(app.preset.steps[0].target_ms == 200);
    assert(controllerExecute(&app, ACTION_STOP_CAPTURE, error)); assert(app.preset_loaded && app.active_step == 0);
    assert(controllerExecute(&app, ACTION_NEXT_STEP, error)); assert(!fake.running && app.active_step == 1);
    assert(controllerExecute(&app, ACTION_START_CAPTURE, error)); assert(fake.running && !fake.lag.enabled);
    controllerUnloadPreset(&app); assert(fake.running && !app.preset_loaded);
    assert(controllerLoadPreset(&app, &preset, true, 50, error));
    assert(controllerSetLag(&app, &app.lag, error)); assert(!app.preset_loaded);
    controllerShutdown(&app);
    puts("PASS sequences: load without start, bounds/wrap/reset, stop/resume, frozen snapshots, traffic changes and failed-step rollback");
}
int main(void) { testCalculations(); testJson(); testSequence(); return 0; }
