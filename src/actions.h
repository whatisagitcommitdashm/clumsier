#pragma once

typedef enum {
    ACTION_START_CAPTURE,
    ACTION_STOP_CAPTURE,
    ACTION_TOGGLE_CAPTURE,
    ACTION_COUNT
} AppAction;

// The caller supplies the capture operations, so dispatch also works without
// IUP (including in tests). context is passed through unchanged; the UI uses it
// for the buffer that receives startup errors.
// Call on the application's control thread so actions run in event order.
typedef struct {
    void *context;
    int (*isRunning)(void *context);
    int (*start)(void *context);
    void (*stop)(void *context);
} ActionTarget;

const char *actionName(AppAction action);
int actionExecute(AppAction action, const ActionTarget *target);
