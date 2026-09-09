#pragma once

typedef enum {
    ACTION_START_CAPTURE,
    ACTION_STOP_CAPTURE,
    ACTION_TOGGLE_CAPTURE,
    ACTION_COUNT
} AppAction;

const char *actionName(AppAction action);
