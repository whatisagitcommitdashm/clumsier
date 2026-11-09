#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>
#include <memory>
#include "platform/macos/dummynet_backend.h"
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const QString path = dir.filePath("broker.sock");
    QLocalServer server;
    if (!server.listen(path)) return 1;
    bool active = false, sawStart = false, sawApply = false, reject = false;
    QLocalSocket *peer = nullptr;
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        auto *socket = server.nextPendingConnection();
        peer = socket;
        auto pending = std::make_shared<QByteArray>();
        QObject::connect(socket, &QLocalSocket::readyRead, &app, [&, socket, pending] {
            *pending += socket->readAll();
            while (pending->contains('\n')) {
                int n = pending->indexOf('\n');
                auto request = QJsonDocument::fromJson(pending->left(n)).object(); pending->remove(0,n+1);
                QString command = request["command"].toString();
                if (command == "start") { active = true; sawStart = request["delay"].toArray() == QJsonArray{0, 100}; }
                if (command == "apply" && !reject) sawApply = request["delay"].toArray() == QJsonArray{0, 300};
                if (command == "stop") active = false;
                socket->write(QJsonDocument(QJsonObject{{"ok", !reject}, {"running", active}, {"error", reject ? "rejected" : ""}}).toJson(QJsonDocument::Compact)+'\n');
            }
        });
    });
    DummynetBackend backend(path);
    auto api = backend.interface();
    CaptureTarget target{}; target.traffic.direction = TRAFFIC_OUTBOUND;
    LagSettings lag{}; lag.enabled = true; lag.outbound = true; lag.outbound_ms = 100;
    char error[NETWORK_ERROR_SIZE]{};
    if (!api.ops->start(api.context, &target, &lag, error) || !sawStart || !api.ops->is_running(api.context)) return 2;
    lag.outbound_ms = 300;
    if (!api.ops->apply_lag(api.context, &lag, error) || !sawApply) return 3;
    reject = true;
    if (api.ops->apply_lag(api.context, &lag, error) || !api.ops->is_running(api.context)) return 5;
    reject = false;
    api.ops->stop(api.context);
    if (api.ops->is_running(api.context)) return 4;
    if (!api.ops->start(api.context, &target, &lag, error)) return 6;
    peer->abort();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    if (api.ops->is_running(api.context)) return 7;
    backend.shutdown();
    std::puts("PASS dummynet Qt command/acknowledgement lifecycle");
    return 0;
}
