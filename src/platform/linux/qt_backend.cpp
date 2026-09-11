#include "qt_backend.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <cstring>
extern "C" {
#include "core/preset.h"
}
LinuxQtBackend::LinuxQtBackend(bool direct, QObject *parent) : QObject(parent), direct_(direct) {
    connect(&process_, &QProcess::finished, this, [this] {
        const bool wasRunning = running_;
        running_ = false;
        if (wasRunning && !busy_) {
            error_ = "The Linux network helper exited. Capture has stopped.";
            emit failed(error_);
        }
    });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (!busy_ && process_.state() == QProcess::Running) request({{"command", "status"}});
    });
    timer->start(500);
}
LinuxQtBackend::~LinuxQtBackend() { disconnectHelper(); }
void LinuxQtBackend::disconnectHelper() {
    // Closing stdin works even when the child has elevated credentials and
    // cannot be signalled by the desktop user. EOF requests backend cleanup.
    process_.closeWriteChannel();
    if (process_.state() != QProcess::NotRunning) process_.waitForFinished(15000);
    running_ = false;
    pending_.clear();
}
bool LinuxQtBackend::request(QJsonObject object, char *error) {
    if (busy_) {
        if (error) snprintf(error, NETWORK_ERROR_SIZE, "Wait for the pending network operation.");
        return false;
    }
    const bool launching = process_.state() == QProcess::NotRunning;
    busy_ = true;
    // Do not disable focused editors for live updates or status polling.
    // Only initial authorization needs a visible waiting state.
    if (launching) emit busyChanged(true);
    bool ok = false;
    QString detail;
    if (launching) {
        pending_.clear();
        const QString helper = QCoreApplication::applicationDirPath() + "/clumsier-linux-helper";
        if (!QFileInfo(helper).isExecutable()) detail = "The Linux network helper is missing. Rebuild the Qt application.";
        else {
            if (direct_) process_.start(helper, {});
            else process_.start("/usr/bin/pkexec", {"--disable-internal-agent", helper});
            if (!process_.waitForStarted(3000)) detail = "Could not launch the network helper. Install polkit and use a desktop authentication agent.";
        }
    }
    if (detail.isEmpty()) {
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        const auto ready = connect(&process_, &QProcess::readyReadStandardOutput, &loop, [&] {
            pending_ += process_.readAllStandardOutput();
            if (pending_.contains('\n') || pending_.size() > 16384) loop.quit();
        });
        const auto finished = connect(&process_, &QProcess::finished, &loop, &QEventLoop::quit);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, &loop, &QEventLoop::quit);
        process_.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
        timeout.start(launching ? 120000 : 15000);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
        disconnect(ready); disconnect(finished);
        const auto newline = pending_.indexOf('\n');
        if (newline >= 0 && pending_.size() <= 16384) {
            QJsonParseError parse;
            const auto response = QJsonDocument::fromJson(pending_.left(newline), &parse).object();
            pending_.remove(0, newline + 1);
            if (parse.error == QJsonParseError::NoError && response["ok"].isBool() && response["running"].isBool() && response["error"].isString()) {
                running_ = response["running"].toBool();
                ok = response["ok"].toBool();
                detail = response["error"].toString();
            } else {
                detail = "Invalid response from the Linux network helper.";
                disconnectHelper();
            }
        } else {
            detail = launching ? "Network authorization was cancelled, unavailable, or timed out. Try Start again and approve the desktop authentication prompt."
                               : "The Linux network helper stopped responding. Capture is being stopped.";
            const auto diagnostic = QString::fromUtf8(process_.readAllStandardError()).trimmed();
            if (!diagnostic.isEmpty()) detail += "\n" + diagnostic.left(1000);
            disconnectHelper();
        }
    }
    if (!ok && detail.isEmpty()) detail = "The Linux network operation failed.";
    error_ = detail;
    busy_ = false;
    if (launching) emit busyChanged(false);
    if (!ok) {
        if (error) snprintf(error, NETWORK_ERROR_SIZE, "%s", detail.toUtf8().constData());
        emit failed(detail);
    }
    return ok;
}
static QJsonArray lagJson(const LagSettings *lag) {
    return {lag->enabled, lag->inbound, lag->outbound, int(lag->inbound_ms), int(lag->outbound_ms)};
}
bool LinuxQtBackend::start(void *context, const CaptureTarget *target, const LagSettings *lag, char *error) {
    auto *self = static_cast<LinuxQtBackend *>(context);
    Preset preset; presetDefault(&preset);
    preset.mode = PRESET_ADDED_DELAY;
    preset.steps[0].kind = STEP_DELAY;
    preset.steps[0].inbound_ms = preset.steps[0].outbound_ms = preset.steps[0].target_ms = 0;
    preset.target = *target;
    preset.policy = target->traffic.direction == TRAFFIC_OUTBOUND ? DELAY_OUTBOUND : DELAY_INBOUND;
    char *serialized = presetSerialize(&preset, error);
    if (!serialized) return false;
    const QString json = QString::fromUtf8(serialized); free(serialized);
    return self->request({{"command", "start"}, {"preset", json}, {"lag", lagJson(lag)}}, error);
}
bool LinuxQtBackend::apply(void *context, const LagSettings *lag, char *error) {
    return static_cast<LinuxQtBackend *>(context)->request({{"command", "apply"}, {"lag", lagJson(lag)}}, error);
}
void LinuxQtBackend::stop(void *context) {
    auto *self = static_cast<LinuxQtBackend *>(context);
    if (self->process_.state() != QProcess::NotRunning) self->request({{"command", "stop"}});
    self->running_ = false;
}
bool LinuxQtBackend::running(void *context) { return static_cast<LinuxQtBackend *>(context)->running_; }
NetworkBackend LinuxQtBackend::interface() {
    static const NetworkBackendOps ops{start, apply, stop, running};
    return {&ops, this};
}
