#pragma once
#include <QObject>
#include <QLocalSocket>
#include <QJsonObject>
#include <QTimer>
extern "C" {
#include "core/network.h"
}
class DummynetBackend : public QObject {
    Q_OBJECT
public:
    explicit DummynetBackend(const QString &socketPath = {}, QObject *parent = nullptr);
    ~DummynetBackend() override;
    NetworkBackend interface();
    void shutdown();
    bool probe(char *error) { return request({{"command", "status"}}, error); }
signals:
    void busyChanged(bool busy);
    void failed(const QString &error);
private:
    QString socketPath_;
    QLocalSocket socket_;
    QTimer heartbeat_;
    bool running_ = false, busy_ = false;
    QByteArray pending_;
    bool request(QJsonObject object, char *error = nullptr);
    static bool start(void *, const CaptureTarget *, const LagSettings *, char *);
    static bool apply(void *, const LagSettings *, char *);
    static void stop(void *);
    static bool running(void *);
};
