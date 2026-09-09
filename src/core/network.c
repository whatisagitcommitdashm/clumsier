#include <string.h>
#include "network.h"

void lagSettingsDefault(LagSettings *settings) {
    LagSettings defaults = {false, true, true, 50, 50};
    *settings = defaults;
}

bool lagSettingsValidate(const LagSettings *settings, char error[NETWORK_ERROR_SIZE]) {
    if (settings->inbound_ms > LAG_MAX_MS || settings->outbound_ms > LAG_MAX_MS) {
        strcpy(error, "Lag delay must be between 0 and 15000 ms.");
        return false;
    }
    return true;
}

bool captureTargetValidate(const CaptureTarget *target, char error[NETWORK_ERROR_SIZE]) {
    if (target->traffic.protocol < TRAFFIC_ANY || target->traffic.protocol > TRAFFIC_UDP ||
        target->traffic.direction < TRAFFIC_BOTH || target->traffic.direction > TRAFFIC_OUTBOUND ||
        !memchr(target->traffic.remote_address, 0, sizeof(target->traffic.remote_address)) ||
        !memchr(target->native_backend, 0, sizeof(target->native_backend)) ||
        !memchr(target->native_filter, 0, sizeof(target->native_filter))) {
        strcpy(error, "Invalid traffic selection.");
        return false;
    }
    if ((target->native_filter[0] != 0) != (target->native_backend[0] != 0)) {
        strcpy(error, "A native filter must name its backend.");
        return false;
    }
    if (target->traffic.remote_port && target->traffic.protocol == TRAFFIC_ANY) {
        strcpy(error, "Choose TCP or UDP when selecting a port.");
        return false;
    }
    if (target->native_filter[0] && (target->traffic.protocol != TRAFFIC_ANY ||
        target->traffic.direction != TRAFFIC_BOTH || target->traffic.remote_address[0] || target->traffic.remote_port)) {
        strcpy(error, "Choose a structured traffic selection or a native filter, not both.");
        return false;
    }
    return true;
}
