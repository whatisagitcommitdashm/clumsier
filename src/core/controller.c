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
    return true;
}

bool controllerSetLag(AppController *app, const LagSettings *lag, char *error) {
    if (!lagSettingsValidate(lag, error)) return false;
    // While stopped this is just a configuration edit. During capture, don't
    // tell the UI it succeeded until the backend has accepted the whole change.
    if (controllerIsRunning(app) && !app->backend.ops->apply_lag(app->backend.context, lag, error)) return false;
    app->lag = *lag;
    return true;
}

bool controllerExecute(AppController *app, AppAction action, char *error) {
    bool running = controllerIsRunning(app);
    if (action == ACTION_TOGGLE_CAPTURE) action = running ? ACTION_STOP_CAPTURE : ACTION_START_CAPTURE;
    switch (action) {
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
