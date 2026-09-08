#pragma once

typedef enum {
    ACTION_START_CAPTURE,
    ACTION_STOP_CAPTURE,
    ACTION_TOGGLE_CAPTURE,
    ACTION_COUNT
} AppAction;

// The application supplies these operations. No UI or Windows types are needed.
// Dispatch actions on the application's control thread, in event order.
typedef struct {
    void *context;
    int (*isRunning)(void *context);
    int (*start)(void *context);
    void (*stop)(void *context);
} ActionTarget;

const char *actionName(AppAction action);
int actionExecute(AppAction action, const ActionTarget *target);
