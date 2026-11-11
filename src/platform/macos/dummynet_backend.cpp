#include "dummynet_backend.h"
#include <QEventLoop>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <unistd.h>
#include <cstdio>

DummynetBackend::DummynetBackend(const QString &socketPath, QObject *parent) : QObject(parent),
    socketPath_(socketPath.isEmpty() ? QString("/var/run/clumsier-lag-%1.sock").arg(getuid()) : socketPath) {
    connect(&socket_, &QLocalSocket::disconnected, this, [this] {
        bool active = running_; running_ = false;
        if (active && !busy_) emit failed("The PF/dummynet helper disconnected. Restart it with the local launcher; see docs/MACOS-LOCAL-LAG.md for recovery logs.");
    });
    heartbeat_.setInterval(2000);
    connect(&heartbeat_, &QTimer::timeout, this, [this] {
        if (!busy_ && socket_.state() == QLocalSocket::ConnectedState) request({{"command", "status"}});
    });
    heartbeat_.start();
}
DummynetBackend::~DummynetBackend() { shutdown(); }
void DummynetBackend::shutdown() {
    heartbeat_.stop();
    if (!busy_ && socket_.state() == QLocalSocket::ConnectedState) request({{"command", "stop"}});
    socket_.abort(); running_ = false;
}
bool DummynetBackend::request(QJsonObject object, char *error) {
    if (busy_) {
        if (error) snprintf(error, NETWORK_ERROR_SIZE, "Wait for the pending network operation.");
        return false;
    }
    busy_ = true;
    QString detail;
    bool ok = false;
    if (socket_.state() != QLocalSocket::ConnectedState) {
        pending_.clear();
        socket_.connectToServer(socketPath_);
        if (!socket_.waitForConnected(1000))
            detail = "Authorize networking using Set up networking, then retry Start. Development checkout: scripts/start-macos-local.sh (no server required).";
    }
    if (detail.isEmpty()) {
        QEventLoop loop;
        QTimer timeout; timeout.setSingleShot(true);
        const auto ready = connect(&socket_, &QLocalSocket::readyRead, &loop, [&] {
            pending_ += socket_.readAll();
            if (pending_.contains('\n') || pending_.size() > 4096) loop.quit();
        });
        const auto gone = connect(&socket_, &QLocalSocket::disconnected, &loop, &QEventLoop::quit);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, &loop, &QEventLoop::quit);
        socket_.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
        timeout.start(5000);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
        disconnect(ready); disconnect(gone);
        const int newline = pending_.indexOf('\n');
        QJsonParseError parse;
        const auto response = QJsonDocument::fromJson(pending_.left(newline), &parse).object();
        if (newline >= 0 && pending_.size() <= 4096 && parse.error == QJsonParseError::NoError
            && response["ok"].isBool() && response["running"].isBool() && response["error"].isString()) {
            pending_.remove(0, newline + 1);
            ok = response["ok"].toBool(); running_ = response["running"].toBool();
            detail = response["error"].toString();
        } else {
            detail = "PF/dummynet helper timed out or returned an invalid reply. Check the recovery log described in docs/MACOS-LOCAL-LAG.md.";
            socket_.abort(); running_ = false;
        }
    }
    busy_ = false;
    if (!ok) {
        if (detail.isEmpty()) detail = "PF/dummynet command failed.";
        if (error) snprintf(error, NETWORK_ERROR_SIZE, "%s", detail.toUtf8().constData());
        emit failed(detail);
    }
    return ok;
}
static QJsonArray delays(const LagSettings *lag) {
    return {int(lag->enabled && lag->inbound ? lag->inbound_ms : 0),
            int(lag->enabled && lag->outbound ? lag->outbound_ms : 0)};
}
bool DummynetBackend::start(void *context, const CaptureTarget *target, const LagSettings *lag, char *error) {
    if (!captureTargetValidate(target, error) || !lagSettingsValidate(lag, error)) return false;
    return static_cast<DummynetBackend *>(context)->request({{"command", "start"}, {"delay", delays(lag)},
        {"address", QString::fromUtf8(target->traffic.remote_address)}, {"port", target->traffic.remote_port},
        {"protocol", int(target->traffic.protocol)}, {"native", bool(target->native_backend[0] || target->native_filter[0])}}, error);
}
bool DummynetBackend::apply(void *context, const LagSettings *lag, char *error) {
    if (!lagSettingsValidate(lag, error)) return false;
    return static_cast<DummynetBackend *>(context)->request({{"command", "apply"}, {"delay", delays(lag)}}, error);
}
void DummynetBackend::stop(void *context) {
    auto *self = static_cast<DummynetBackend *>(context);
    if (self->socket_.state() == QLocalSocket::ConnectedState) self->request({{"command", "stop"}});
}
bool DummynetBackend::running(void *context) { return static_cast<DummynetBackend *>(context)->running_; }
NetworkBackend DummynetBackend::interface() {
    static const NetworkBackendOps ops{start, apply, stop, running};
    return {&ops, this};
}
