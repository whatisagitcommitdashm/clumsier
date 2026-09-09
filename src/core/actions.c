#include "actions.h"

const char *actionName(AppAction action) {
    switch (action) {
        case ACTION_START_CAPTURE: return "Start";
        case ACTION_STOP_CAPTURE: return "Stop";
        case ACTION_TOGGLE_CAPTURE: return "Toggle";
        case ACTION_NEXT_STEP: return "Next step";
        case ACTION_PREVIOUS_STEP: return "Previous step";
        case ACTION_RESET_SEQUENCE: return "Reset sequence";
        default: return "Unknown action";
    }
}
