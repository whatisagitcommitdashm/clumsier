#pragma once
#include <QObject>
#include <QProcess>
#include <QJsonObject>
extern "C" {
#include "core/network.h"
}
class LinuxQtBackend : public QObject {
    Q_OBJECT
public:
    explicit LinuxQtBackend(bool direct = false, QObject *parent = nullptr);
    ~LinuxQtBackend() override;
    NetworkBackend interface();
    QString error() const { return error_; }
    bool busy() const { return busy_; }
    void shutdown() { disconnectHelper(); }
signals:
    void busyChanged(bool busy);
    void failed(const QString &error);
private:
    QProcess process_;
    bool direct_, running_ = false, busy_ = false;
    QString error_;
    QByteArray pending_;
    bool request(QJsonObject request, char *error = nullptr);
    void disconnectHelper();
    static bool start(void *, const CaptureTarget *, const LagSettings *, char *);
    static bool apply(void *, const LagSettings *, char *);
    static void stop(void *);
    static bool running(void *);
};
