#pragma once
#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QVariantList>
#include <QTimer>
extern "C" {
#include "core/network.h"
}

// Called on the Qt main thread. An empty error means a pure running-state
// change (for example, the provider's watchdog stopping delay); the adapter
// reports command failures through its own reply values instead.
using MacNotify = void (*)(void *context, bool running, const QString &error);

// The Objective-C++ helper (mac_control.mm) owns system extension activation,
// filter registration and the provider XPC connection. All functions run on
// the Qt main thread; commands are serialized by the controller's owner, and
// the heartbeat skips itself while a command reply is pending. These are C++
// functions, not extern "C": the helper is compiled by the same compiler, so
// C++ return types cross the boundary safely.
void *macControlCreate(MacNotify notify, void *context);
// Returns {bool ok, bool running, QString error}.
QVariantList macControlCommand(void *control, const QJsonObject &request);
void macControlHeartbeat(void *control);
void macControlShutdown(void *control);
void macControlDestroy(void *control);

// Serialize the added-delay preset the provider re-validates, so both peers
// share one schema and the shared target-ping arithmetic. Exposed for the
// backend contract test; start/apply build their requests from it.
QString macQtPresetJson(const CaptureTarget *target, const LagSettings *lag, char *error);

// Qt controller for the macOS packet-filter prototype. Mirrors the Linux
// adapter's NetworkBackendOps contract: serialized ops calls, a failed start
// must clean up, and stop releases held traffic. Running state is refreshed
// by command replies and heartbeats; AppBridge polls it like on Linux.
class MacQtBackend : public QObject {
    Q_OBJECT
public:
    explicit MacQtBackend(QObject *parent = nullptr);
    ~MacQtBackend() override;
    NetworkBackend interface();
    QString error() const { return error_; }
    bool busy() const { return busy_; }
    void shutdown();
signals:
    void busyChanged(bool busy);
    void failed(const QString &error);
private:
    void *helper_ = nullptr;
    QTimer *heartbeat_ = nullptr;
    CaptureTarget lastTarget_{};
    bool hasTarget_ = false, running_ = false, busy_ = false;
    QString error_;
    bool request(const QJsonObject &request, char *error = nullptr);
    static void notify(void *context, bool running, const QString &error);
    static bool start(void *, const CaptureTarget *, const LagSettings *, char *);
    static bool apply(void *, const LagSettings *, char *);
    static void stop(void *);
    static bool running(void *);
};
