#include "qt_backend.h"
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <cstring>
extern "C" {
#include "core/preset.h"
}

static QJsonArray lagJson(const LagSettings *lag) {
    return {lag->enabled, lag->inbound, lag->outbound, int(lag->inbound_ms), int(lag->outbound_ms)};
}

// The helper's entry points are C++ (see qt_backend.h for the rationale), so
// this file needs no extern "C" wrapper around them.

QString macQtPresetJson(const CaptureTarget *target, const LagSettings *lag, char *error) {
    Preset preset;
    presetDefault(&preset);
    preset.mode = PRESET_ADDED_DELAY;
    // The capture direction must include every delayed direction (shared
    // validation), so the policy follows the lag flags, not the target alone.
    preset.policy = lag->outbound ? (lag->inbound ? DELAY_BOTH : DELAY_OUTBOUND) : DELAY_INBOUND;
    preset.target = *target;
    preset.steps[0].kind = STEP_DELAY;
    preset.steps[0].target_ms = 0; // presetDefault's target-ping value is invalid for delay steps.
    // The shared resolver derives enabled from the serialized delay values.
    // Preserve an explicitly disabled LagSettings even when it retains the
    // defaults that should be restored if the user enables lag again.
    preset.steps[0].inbound_ms = lag->enabled && lag->inbound ? lag->inbound_ms : 0;
    preset.steps[0].outbound_ms = lag->enabled && lag->outbound ? lag->outbound_ms : 0;
    char *serialized = presetSerialize(&preset, error);
    if (!serialized) return {};
    const QString json = QString::fromUtf8(serialized);
    free(serialized);
    return json;
}

MacQtBackend::MacQtBackend(QObject *parent) : QObject(parent), helper_(macControlCreate(&MacQtBackend::notify, this)) {
    // The provider stops delay after roughly ten seconds without contact. A
    // heartbeat also observes provider-side stops, which the AppKit host
    // detects the same way. Skipped while a command reply is pending.
    heartbeat_ = new QTimer(this);
    heartbeat_->setInterval(3000);
    connect(heartbeat_, &QTimer::timeout, this, [this] {
        if (busy_) return;
        macControlHeartbeat(helper_);
    });
    heartbeat_->start();
}

MacQtBackend::~MacQtBackend() { shutdown(); }

void MacQtBackend::shutdown() {
    if (heartbeat_) heartbeat_->stop();
    if (!helper_) return;
    macControlShutdown(helper_);
    macControlDestroy(helper_);
    helper_ = nullptr;
    running_ = false;
}

void MacQtBackend::notify(void *context, bool running, const QString &error) {
    auto *self = static_cast<MacQtBackend *>(context);
    const bool wasRunning = self->running_;
    self->running_ = running;
    if (wasRunning && !running && !self->busy_ && error.isEmpty())
        emit self->failed("The packet provider stopped the session. Start to resume.");
    if (!error.isEmpty()) emit self->failed(error);
}

bool MacQtBackend::request(const QJsonObject &object, char *error) {
    if (!helper_) {
        if (error) snprintf(error, NETWORK_ERROR_SIZE, "The macOS network backend is not available.");
        return false;
    }
    const bool launching = !running_ && !busy_;
    busy_ = true;
    // Do not disable focused editors for live updates or heartbeats. Only the
    // first activation needs a visible waiting state, like the Linux helper.
    if (launching) emit busyChanged(true);
    const QVariantList reply = macControlCommand(helper_, object);
    busy_ = false;
    if (launching) emit busyChanged(false);
    const bool ok = reply.value(0).toBool();
    running_ = reply.value(1).toBool();
    QString detail = reply.value(2).toString();
    if (!ok && detail.isEmpty()) detail = "The macOS network operation failed.";
    error_ = detail;
    if (!ok) {
        if (error) snprintf(error, NETWORK_ERROR_SIZE, "%s", detail.toUtf8().constData());
        emit failed(detail);
    }
    return ok;
}

bool MacQtBackend::start(void *context, const CaptureTarget *target, const LagSettings *lag, char *error) {
    auto *self = static_cast<MacQtBackend *>(context);
    const QString json = macQtPresetJson(target, lag, error);
    if (json.isEmpty()) return false;
    const bool ok = self->request({{"command", "start"}, {"preset", json}, {"lag", lagJson(lag)}}, error);
    if (ok) { self->lastTarget_ = *target; self->hasTarget_ = true; }
    return ok;
}

bool MacQtBackend::apply(void *context, const LagSettings *lag, char *error) {
    auto *self = static_cast<MacQtBackend *>(context);
    if (!self->hasTarget_) {
        strcpy(error, "Start capture before changing the delay.");
        return false;
    }
    const QString json = macQtPresetJson(&self->lastTarget_, lag, error);
    if (json.isEmpty()) return false;
    return self->request({{"command", "apply"}, {"preset", json}, {"lag", lagJson(lag)}}, error);
}

void MacQtBackend::stop(void *context) {
    auto *self = static_cast<MacQtBackend *>(context);
    self->request({{"command", "stop"}});
}

bool MacQtBackend::running(void *context) { return static_cast<MacQtBackend *>(context)->running_; }

NetworkBackend MacQtBackend::interface() {
    static const NetworkBackendOps ops{start, apply, stop, running};
    return {&ops, this};
}
