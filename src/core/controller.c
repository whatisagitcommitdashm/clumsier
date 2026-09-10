#include <string.h>
#include "controller.h"

void controllerInit(AppController *app, NetworkBackend backend) {
    memset(app, 0, sizeof(*app));
    app->backend = backend;
    lagSettingsDefault(&app->lag);
}

bool controllerIsRunning(const AppController *app) {
    return app->backend.ops->is_running(app->backend.context);
}

bool controllerSetTarget(AppController *app, const CaptureTarget *target, char *error) {
    if (controllerIsRunning(app)) {
        strcpy(error, "Stop capture before changing the traffic selection.");
        return false;
    }
    if (!captureTargetValidate(target, error)) return false;
    app->target = *target;
    app->preset_loaded = false;
    return true;
}

bool controllerSetLag(AppController *app, const LagSettings *lag, char *error) {
    if (!lagSettingsValidate(lag, error)) return false;
    // While stopped this is just a configuration edit. During capture, don't
    // tell the UI it succeeded until the backend has accepted the whole change.
    if (controllerIsRunning(app) && !app->backend.ops->apply_lag(app->backend.context, lag, error)) return false;
    app->lag = *lag;
    app->preset_loaded = false;
    return true;
}

bool controllerLoadPreset(AppController *app, const Preset *preset, bool has_baseline, uint32_t baseline_ms, char *error) {
    StepResult first, checked;
    size_t i;
    if (!presetValidate(preset, error)) return false;
    // Check the whole sequence before loading it. A high target must not become
    // a surprise failure several hotkey presses into a run.
    for (i = 0; i < preset->step_count; ++i)
        if (!presetResolve(preset, i, has_baseline, baseline_ms, &checked, error)) return false;
    if (!presetResolve(preset, 0, has_baseline, baseline_ms, &first, error)) return false;
    if (controllerIsRunning(app)) {
        if (!captureTargetsEqual(&app->target, &preset->target)) {
            strcpy(error, "Stop capture before loading a preset with different traffic selections.");
            return false;
        }
        if (!app->backend.ops->apply_lag(app->backend.context, &first.lag, error)) return false;
    }
    app->preset = *preset;
    app->target = preset->target;
    app->lag = first.lag;
    app->step_result = first;
    app->active_step = 0;
    app->has_baseline = has_baseline;
    app->baseline_ms = baseline_ms;
    app->preset_loaded = true;
    return true;
}

void controllerUnloadPreset(AppController *app) {
    // Unloading returns control to the manual panel; it doesn't stop capture or
    // change the currently applied delay behind the user's back.
    app->preset_loaded = false;
}

bool controllerSelectStep(AppController *app, size_t next, char *error) {
    StepResult resolved;
    if (!app->preset_loaded) { strcpy(error, "Load a preset before using sequence controls."); return false; }
    if (!presetResolve(&app->preset, next, app->has_baseline, app->baseline_ms, &resolved, error)) return false;
    if (controllerIsRunning(app) && !app->backend.ops->apply_lag(app->backend.context, &resolved.lag, error)) return false;
    // Commit the cursor and settings together, only after the backend accepts.
    app->lag = resolved.lag;
    app->step_result = resolved;
    app->active_step = next;
    return true;
}

static bool moveStep(AppController *app, AppAction action, char *error) {
    size_t next = app->active_step;
    if (!app->preset_loaded) { strcpy(error, "Load a preset before using sequence controls."); return false; }
    if (action == ACTION_RESET_SEQUENCE) next = 0;
    else if (action == ACTION_NEXT_STEP) {
        if (next + 1 < app->preset.step_count) ++next;
        else if (app->preset.loop) next = 0;
    } else if (next) --next;
    else if (app->preset.loop) next = app->preset.step_count - 1;
    return controllerSelectStep(app, next, error);
}

bool controllerExecute(AppController *app, AppAction action, char *error) {
    bool running = controllerIsRunning(app);
    if (action == ACTION_TOGGLE_CAPTURE) action = running ? ACTION_STOP_CAPTURE : ACTION_START_CAPTURE;
    switch (action) {
        case ACTION_NEXT_STEP: case ACTION_PREVIOUS_STEP: case ACTION_RESET_SEQUENCE:
            return moveStep(app, action, error);
        case ACTION_START_CAPTURE:
            if (running) return true;
            return app->backend.ops->start(app->backend.context, &app->target, &app->lag, error);
        case ACTION_STOP_CAPTURE:
            if (running) app->backend.ops->stop(app->backend.context);
            return true;
        default:
            strcpy(error, "Unknown application action.");
            return false;
    }
}

void controllerShutdown(AppController *app) {
    app->backend.ops->stop(app->backend.context);
}
