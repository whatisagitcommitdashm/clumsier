#include "actions.h"

const char *actionName(AppAction action) {
    switch (action) {
        case ACTION_START_CAPTURE: return "Start";
        case ACTION_STOP_CAPTURE: return "Stop";
        case ACTION_TOGGLE_CAPTURE: return "Toggle";
        default: return "Unknown action";
    }
}
