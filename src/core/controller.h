#pragma once
#include "actions.h"
#include "network.h"
#include "preset.h"

typedef struct {
    NetworkBackend backend;
    LagSettings lag;
    CaptureTarget target;
    // Keep a snapshot: editing a saved preset must not rewrite a running sequence.
    Preset preset;
    bool preset_loaded;
    size_t active_step;
    bool has_baseline;
    uint32_t baseline_ms;
    StepResult step_result;
} AppController;

// Single owner (currently the UI thread). UI controls hold drafts, while this
// object holds accepted settings. It does not cache a second running flag.
void controllerInit(AppController *app, NetworkBackend backend);
bool controllerSetTarget(AppController *app, const CaptureTarget *target, char *error);
bool controllerSetLag(AppController *app, const LagSettings *lag, char *error);
bool controllerExecute(AppController *app, AppAction action, char *error);
bool controllerIsRunning(const AppController *app);
void controllerShutdown(AppController *app);
bool controllerLoadPreset(AppController *app, const Preset *preset, bool has_baseline, uint32_t baseline_ms, char *error);
void controllerUnloadPreset(AppController *app);
