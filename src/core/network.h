#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NETWORK_ERROR_SIZE 512
#define LAG_MAX_MS 15000
#define TRAFFIC_ADDRESS_SIZE 64
#define NATIVE_FILTER_SIZE 1024

typedef struct {
    bool enabled;
    bool inbound;
    bool outbound;
    uint32_t inbound_ms;
    uint32_t outbound_ms;
} LagSettings;

typedef enum { TRAFFIC_ANY, TRAFFIC_TCP, TRAFFIC_UDP } TrafficProtocol;
typedef enum { TRAFFIC_BOTH, TRAFFIC_INBOUND, TRAFFIC_OUTBOUND } TrafficDirection;
typedef struct {
    TrafficProtocol protocol;
    TrafficDirection direction;
    char remote_address[TRAFFIC_ADDRESS_SIZE]; // Empty means any; numeric IP only.
    uint16_t remote_port;                    // Zero means any port.
} TrafficSelector;

typedef struct {
    TrafficSelector traffic;
    // Optional escape hatch for the existing Windows filter editor. Backends
    // that don't understand this syntax must reject it, never silently ignore it.
    char native_backend[32]; // For example, "windivert". Empty for structured selection.
    char native_filter[NATIVE_FILTER_SIZE];
} CaptureTarget;

void lagSettingsDefault(LagSettings *settings);
bool lagSettingsValidate(const LagSettings *settings, char error[NETWORK_ERROR_SIZE]);
bool captureTargetValidate(const CaptureTarget *target, char error[NETWORK_ERROR_SIZE]);

// Calls are serialized by the controller's owner. Implementations own their
// workers and OS resources. A failed start must clean up; a failed apply must
// leave the previous settings intact. Stop must release queued traffic/resources.
// Accepted live Lag changes govern packets we're already holding, not just new
// arrivals. Keep their original enqueue times: lowering delay can release them
// sooner, raising it can hold them longer. Disabling Lag/a direction releases
// the affected queue. Workers observe changes on their next processing pass;
// "immediate" doesn't mean synchronous delivery inside apply_lag.
// Traffic selection changes still require stopping capture first.
// Scheduling precision and overload handling are backend-specific.
typedef struct {
    bool (*start)(void *context, const CaptureTarget *target, const LagSettings *lag, char *error);
    bool (*apply_lag)(void *context, const LagSettings *lag, char *error);
    void (*stop)(void *context);
    bool (*is_running)(void *context);
} NetworkBackendOps;
typedef struct { const NetworkBackendOps *ops; void *context; } NetworkBackend;
