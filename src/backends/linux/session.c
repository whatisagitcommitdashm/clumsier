#include <stdio.h>
#include <string.h>
#include "session.h"

bool linuxSessionStop(LinuxSession *session, char *error) {
    bool okay = true;
    char detail[NETWORK_ERROR_SIZE] = "";
    if (session->owned) {
        if (session->ops->remove(session->context, detail)) session->owned = false;
        else { snprintf(error, NETWORK_ERROR_SIZE, "%s", detail); okay = false; }
    }
    if (session->open) {
        // Even if removing rules fails, release what we can before closing.
        // Queue bypass then protects new packets; report the stale owned rule.
        if (!session->ops->drain(session->context, detail)) {
            if (okay) snprintf(error, NETWORK_ERROR_SIZE, "%s", detail);
            okay = false;
        }
        session->ops->close(session->context);
        session->open = false;
    }
    return okay;
}

bool linuxSessionStart(LinuxSession *session, char *error) {
    char cleanup[NETWORK_ERROR_SIZE] = "";
    if (session->open || session->owned) {
        strcpy(error, "Linux session still owns resources; stop it before starting again.");
        return false;
    }
    if (!session->ops->open(session->context, error)) return false;
    session->open = true;
    if (session->ops->install(session->context, &session->owned, error)) return true;
    if (!linuxSessionStop(session, cleanup)) {
        char original[NETWORK_ERROR_SIZE];
        snprintf(original, sizeof(original), "%s", error);
        snprintf(error, NETWORK_ERROR_SIZE, "%.240s; cleanup: %.250s", original, cleanup);
    }
    return false;
}
