#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "core/controller.h"
#include "core/hotkey_matcher.h"

typedef struct { bool running, fail_start, fail_apply; int starts, stops, applies; LagSettings lag; } FakeNetwork;
static bool start(void *context, const CaptureTarget *target, const LagSettings *lag, char *error) {
    FakeNetwork *fake = context;
    (void)target;
    ++fake->starts;
    if (fake->fail_start) { strcpy(error, "Simulated start failure"); return false; }
    fake->lag = *lag; fake->running = true; return true;
}
static bool apply(void *context, const LagSettings *lag, char *error) {
    FakeNetwork *fake = context;
    ++fake->applies;
    if (fake->fail_apply) { strcpy(error, "Simulated apply failure"); return false; }
    fake->lag = *lag; return true;
}
static void stop(void *context) { FakeNetwork *fake = context; ++fake->stops; fake->running = false; }
static bool running(void *context) { return ((FakeNetwork*)context)->running; }

int main(void) {
    const NetworkBackendOps ops = {start, apply, stop, running};
    FakeNetwork fake = {0};
    NetworkBackend backend = {&ops, &fake};
    AppController app;
    CaptureTarget target = {0};
    LagSettings draft;
    char error[NETWORK_ERROR_SIZE];
    HotkeyMatcher matcher = {0};
    HotkeySettings bindings = {0};
    controllerInit(&app, backend);
    assert(!app.lag.enabled && app.lag.inbound_ms == 50);
    draft = app.lag; draft.enabled = true; draft.inbound_ms = 80; draft.outbound_ms = 20;
    assert(controllerSetLag(&app, &draft, error) && !fake.applies);
    target.traffic.protocol = TRAFFIC_UDP; target.traffic.remote_port = 1234;
    assert(controllerSetTarget(&app, &target, error));
    fake.fail_start = true;
    assert(!controllerExecute(&app, ACTION_START_CAPTURE, error) && !controllerIsRunning(&app));
    fake.fail_start = false;
    assert(controllerExecute(&app, ACTION_START_CAPTURE, error));
    assert(fake.lag.inbound_ms == 80 && fake.lag.outbound_ms == 20);
    assert(controllerExecute(&app, ACTION_START_CAPTURE, error) && fake.starts == 2);
    assert(!controllerSetTarget(&app, &target, error));
    fake.fail_apply = true; draft.inbound_ms = 100;
    assert(!controllerSetLag(&app, &draft, error));
    assert(app.lag.inbound_ms == 80 && fake.lag.inbound_ms == 80);
    fake.fail_apply = false; assert(controllerSetLag(&app, &draft, error));
    assert(app.lag.inbound_ms == 100 && fake.lag.inbound_ms == 100);
    draft.outbound_ms = LAG_MAX_MS + 1;
    assert(!controllerSetLag(&app, &draft, error) && fake.applies == 2);
    assert(controllerExecute(&app, ACTION_TOGGLE_CAPTURE, error) && !fake.running);
    assert(controllerExecute(&app, ACTION_STOP_CAPTURE, error) && fake.stops == 1);
    assert(controllerExecute(&app, ACTION_TOGGLE_CAPTURE, error) && fake.running);
    fake.running = false; // Backend state changed independently of the last command.
    assert(controllerExecute(&app, ACTION_TOGGLE_CAPTURE, error) && fake.running);
    assert(!controllerExecute(&app, ACTION_COUNT, error));
    controllerShutdown(&app); assert(!fake.running);
    strcpy(target.native_filter, "udp");
    assert(!controllerSetTarget(&app, &target, error)); // Native + structured is ambiguous.
    memset(&target, 0, sizeof(target)); target.traffic.direction = (TrafficDirection)99;
    assert(!controllerSetTarget(&app, &target, error));
    puts("PASS portable controller: accepted state, failed changes, idempotence, toggle, targets and shutdown");

    bindings.bindings[2].keys[10] = bindings.bindings[2].keys[11] = 1;
    hotkeyMatcherApply(&matcher, &bindings);
    assert(!hotkeyMatcherInput(&matcher, 10, true));
    assert(hotkeyMatcherInput(&matcher, 11, true) == 4);
    assert(!hotkeyMatcherInput(&matcher, 11, true));
    hotkeyMatcherInput(&matcher, 11, false);
    assert(hotkeyMatcherInput(&matcher, 11, true) == 4);
    puts("PASS matcher compiled and exercised with no Windows or UI headers");
    return 0;
}
