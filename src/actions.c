#include "actions.h"

const char *actionName(AppAction action) {
    switch (action) {
        case ACTION_START_CAPTURE: return "Start";
        case ACTION_STOP_CAPTURE: return "Stop";
        case ACTION_TOGGLE_CAPTURE: return "Toggle";
        default: return "Unknown action";
    }
}

int actionExecute(AppAction action, const ActionTarget *target) {
    if (action == ACTION_TOGGLE_CAPTURE) {
        action = target->isRunning(target->context)
            ? ACTION_STOP_CAPTURE : ACTION_START_CAPTURE;
    }
    switch (action) {
        case ACTION_START_CAPTURE: return target->start(target->context);
        case ACTION_STOP_CAPTURE:
            target->stop(target->context);
            return 1;
        default: return 0;
    }
}
