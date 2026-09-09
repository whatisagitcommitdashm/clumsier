#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>
#include "backend.h"
#include "lag_engine.h"
#include "legacy/common.h"

bool windowsBuildFilter(const CaptureTarget *target, char filter[NATIVE_FILTER_SIZE], char *error) {
    char endpoint[256] = "true";
    const TrafficSelector *traffic = &target->traffic;
    if (!captureTargetValidate(target, error)) return false;
    if (target->native_filter[0]) {
        if (strcmp(target->native_backend, "windivert")) {
            strcpy(error, "This native filter belongs to a different backend."); return false;
        }
        strcpy(filter, target->native_filter);
        return true;
    }
    if (traffic->remote_port && traffic->protocol == TRAFFIC_ANY) {
        strcpy(error, "Choose TCP or UDP when selecting a port.");
        return false;
    }
    if (traffic->remote_address[0]) {
        IN_ADDR ipv4;
        IN6_ADDR ipv6;
        const char *family;
        if (InetPtonA(AF_INET, traffic->remote_address, &ipv4) == 1) family = "ip";
        else if (InetPtonA(AF_INET6, traffic->remote_address, &ipv6) == 1) family = "ipv6";
        else { strcpy(error, "Remote address must be a numeric IPv4 or IPv6 address."); return false; }
        snprintf(endpoint, sizeof(endpoint), "((outbound and %s.DstAddr == %s) or (inbound and %s.SrcAddr == %s))",
            family, traffic->remote_address, family, traffic->remote_address);
    }
    snprintf(filter, NATIVE_FILTER_SIZE, "(%s) and (%s) and (%s)",
        traffic->protocol == TRAFFIC_TCP ? "tcp" : traffic->protocol == TRAFFIC_UDP ? "udp" : "true",
        traffic->direction == TRAFFIC_INBOUND ? "inbound" : traffic->direction == TRAFFIC_OUTBOUND ? "outbound" : "true",
        endpoint);
    if (traffic->remote_port) {
        char ports[160];
        const char *protocol = traffic->protocol == TRAFFIC_TCP ? "tcp" : "udp";
        snprintf(ports, sizeof(ports), " and ((outbound and %s.DstPort == %u) or (inbound and %s.SrcPort == %u))",
            protocol, traffic->remote_port, protocol, traffic->remote_port);
        strcat(filter, ports);
    }
    return true;
}

static bool start(void *context, const CaptureTarget *target, const LagSettings *lag, char *error) {
    char filter[NATIVE_FILTER_SIZE];
    (void)context;
    if (!lagSettingsValidate(lag, error) || !windowsBuildFilter(target, filter, error)) return false;
    windowsLagConfigure(lag);
    return divertStart(filter, error) != 0;
}
static bool applyLag(void *context, const LagSettings *lag, char *error) {
    (void)context;
    if (!lagSettingsValidate(lag, error)) return false;
    windowsLagConfigure(lag);
    return true;
}
static void stop(void *context) { (void)context; divertStop(); }
static bool isRunning(void *context) { (void)context; return divertIsRunning() != 0; }
NetworkBackend windowsNetworkBackend(void) {
    static const NetworkBackendOps operations = {start, applyLag, stop, isRunning};
    NetworkBackend backend = {&operations, NULL};
    return backend;
}
