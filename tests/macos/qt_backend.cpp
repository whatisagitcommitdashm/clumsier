// Contract checks for the macOS Qt backend that do not touch the provider:
// the synthesized preset must be parseable, resolve to exactly the requested
// lag, and reproduce the controller's traffic selection.
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstring>
#include <cstdio>
extern "C" {
#include "core/preset.h"
#include "core/network.h"
}
#include "platform/macos/qt_backend.h"

static int failures = 0;
static void check(bool ok, const char *what) {
    if (ok) return;
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    char error[NETWORK_ERROR_SIZE] = {0};

    CaptureTarget target{};
    // A port requires a protocol (shared validation), so use UDP explicitly.
    target.traffic.protocol = TRAFFIC_UDP;
    target.traffic.direction = TRAFFIC_BOTH;
    strcpy(target.traffic.remote_address, "203.0.113.7");
    target.traffic.remote_port = 25565;

    // Inbound-only delay: policy follows the delayed direction while the BOTH
    // capture target remains unchanged.
    LagSettings lag{};
    lag.enabled = true; lag.inbound = true; lag.outbound = false;
    lag.inbound_ms = 150; lag.outbound_ms = 0;
    const QString json = macQtPresetJson(&target, &lag, error);
    check(!json.isEmpty(), "Inbound preset serializes");
    Preset parsed{};
    if (!json.isEmpty()) {
        check(presetParse(json.toUtf8().constData(), json.size(), &parsed, error), "Inbound preset parses");
        if (parsed.mode == PRESET_ADDED_DELAY) {
            // Policy follows the lag flags; a BOTH target already includes
            // inbound, so no widening is needed for shared validation.
            check(parsed.policy == DELAY_INBOUND, "Inbound-only policy follows lag flags");
            check(parsed.steps[0].inbound_ms == 150 && parsed.steps[0].outbound_ms == 0, "Inbound-only delay values");
        }
    }
    StepResult resolved{};
    if (presetParse(json.toUtf8().constData(), json.size(), &parsed, error) &&
        presetResolve(&parsed, 0, true, 0, &resolved, error)) {
        check(resolved.lag.inbound_ms == 150 && resolved.lag.outbound_ms == 0, "Resolved lag matches request");
        check(resolved.lag.inbound && !resolved.lag.outbound, "Resolved lag flags match request");
        check(captureTargetsEqual(&parsed.target, &target), "Traffic selection is preserved");
    }

    // Outbound-only: policy must follow the lag flags, not the target alone.
    lag.inbound = false; lag.outbound = true; lag.inbound_ms = 0; lag.outbound_ms = 90;
    const QString outbound = macQtPresetJson(&target, &lag, error);
    Preset parsedOutbound{};
    if (presetParse(outbound.toUtf8().constData(), outbound.size(), &parsedOutbound, error)) {
        check(parsedOutbound.policy == DELAY_OUTBOUND, "Outbound-only policy follows lag flags");
        StepResult resolvedOutbound{};
        check(presetResolve(&parsedOutbound, 0, true, 0, &resolvedOutbound, error), "Outbound preset resolves");
        check(resolvedOutbound.lag.outbound_ms == 90 && resolvedOutbound.lag.outbound, "Outbound delay applied");
        check(!resolvedOutbound.lag.inbound && resolvedOutbound.lag.inbound_ms == 0, "No inbound delay leaks");
    }

    // Both directions at 0 ms must remain valid: zero delay releases instantly.
    lag.inbound = true; lag.outbound = true; lag.inbound_ms = 0; lag.outbound_ms = 0;
    const QString zero = macQtPresetJson(&target, &lag, error);
    check(!zero.isEmpty(), "Zero-delay preset serializes");
    Preset parsedZero{};
    check(presetParse(zero.toUtf8().constData(), zero.size(), &parsedZero, error), "Zero-delay preset parses");
    StepResult resolvedZero{};
    check(presetResolve(&parsedZero, 0, true, 0, &resolvedZero, error), "Zero-delay preset resolves");

    // Disabled settings may retain nonzero values for a later re-enable. The
    // synthesized preset must not turn those retained values into active lag.
    lag.enabled = false; lag.inbound = true; lag.outbound = true;
    lag.inbound_ms = 50; lag.outbound_ms = 50;
    const QString disabled = macQtPresetJson(&target, &lag, error);
    check(!disabled.isEmpty(), "Disabled preset serializes");
    Preset parsedDisabled{};
    StepResult resolvedDisabled{};
    check(presetParse(disabled.toUtf8().constData(), disabled.size(), &parsedDisabled, error), "Disabled preset parses");
    check(presetResolve(&parsedDisabled, 0, true, 0, &resolvedDisabled, error), "Disabled preset resolves");
    check(!resolvedDisabled.lag.enabled && resolvedDisabled.lag.inbound_ms == 0 && resolvedDisabled.lag.outbound_ms == 0,
          "Disabled settings cannot activate retained delay values");

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("macOS Qt backend contract checks passed\n");
    return 0;
}