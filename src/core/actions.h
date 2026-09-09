#pragma once

typedef enum {
    ACTION_START_CAPTURE,
    ACTION_STOP_CAPTURE,
    ACTION_TOGGLE_CAPTURE,
    ACTION_NEXT_STEP,
    ACTION_PREVIOUS_STEP,
    ACTION_RESET_SEQUENCE,
    ACTION_COUNT
} AppAction;

const char *actionName(AppAction action);
