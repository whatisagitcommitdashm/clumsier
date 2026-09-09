#pragma once
#include "actions.h"
#include "network.h"

typedef struct {
    NetworkBackend backend;
    LagSettings lag;
    CaptureTarget target;
} AppController;

// Single owner (currently the UI thread). UI controls hold drafts, while this
// object holds accepted settings. It does not cache a second running flag.
void controllerInit(AppController *app, NetworkBackend backend);
bool controllerSetTarget(AppController *app, const CaptureTarget *target, char *error);
bool controllerSetLag(AppController *app, const LagSettings *lag, char *error);
bool controllerExecute(AppController *app, AppAction action, char *error);
bool controllerIsRunning(const AppController *app);
void controllerShutdown(AppController *app);
