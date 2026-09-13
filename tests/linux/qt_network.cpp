#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QProcess>
#include <QTest>
#include <QRegularExpression>
#include <QFile>
#include "app_bridge.h"
#include "platform/linux/qt_backend.h"

static void check(bool value, const char *message) {
    if (!value) qFatal("FAIL: %s", message);
}
static QQuickItem *find(QQuickItem *item, const QString &name) {
    if (item->objectName() == name) return item;
    for (auto *child : item->childItems()) if (auto *found = find(child, name)) return found;
    return nullptr;
}
static double ping() {
    QProcess process;
    process.start("ping", {"-U", "-n", "-c", "1", "-W", "3", "192.0.2.2"});
    check(process.waitForFinished(5000) && process.exitCode() == 0, "Ping reply delivered");
    const auto match = QRegularExpression("time[=<]([0-9.]+) ms").match(QString::fromUtf8(process.readAllStandardOutput()));
    check(match.hasMatch(), "Ping timing parsed");
    return match.captured(1).toDouble();
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    check(qEnvironmentVariableIsSet("CLUMSIER_QT_NETWORK_LAB"), "Run through test-linux-network.sh --qt");
    app.setOrganizationName("ClumsierTests"); app.setApplicationName("NativeQtNetwork");
    QQuickStyle::setStyle("Basic");
    QTemporaryDir library;
    LinuxQtBackend network(true);
    {
        AppBridge bridge(network.interface(), library.path(), false);
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"backend", QVariant::fromValue(&bridge)},
            {"settingsLocation", QUrl::fromLocalFile(library.filePath("ui.ini"))}, {"page", "quick controls"}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(BETA_QML_DIR "/Main.qml")));
        check(!engine.rootObjects().isEmpty(), "Live QML loads");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QTest::qWait(100);
        auto *start = find(window->contentItem(), "startButton");
        check(start && start->isVisible(), "Start button visible");
        check(bridge.quickDelay(120, 0), "Quick delay accepted");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, start->mapToScene(QPointF(start->width()/2, start->height()/2)).toPoint());
        check(bridge.state()["running"].toBool(), qPrintable(bridge.error()));
        double rtt = ping();
        check(rtt >= 90 && rtt < 270, "GUI Start applies real inbound delay");
        qInfo("PASS Qt Start -> helper -> NFQUEUE: %.2f ms", rtt);
        check(bridge.quickDelay(0, 0), "Live zero accepted");
        check(ping() < 100, "GUI zero restores latency");
        check(!bridge.quickDelay(-1, 0), "Invalid UI delay rejected");
        check(bridge.execute(1), "GUI Stop succeeds");
        check(bridge.importPreset(QUrl::fromLocalFile(QStringLiteral(PROJECT_ROOT "/examples/four-leaps.json"))), "Shared preset imports");
        check(bridge.saveProfile({}, "Lab server", 50), "Local baseline saves");
        check(bridge.execute(0), qPrintable(bridge.error()));
        check(ping() >= 120, "Sequence first step applies");
        check(bridge.execute(3), "Next step applies");
        check(ping() < 100, "Lowest step removes delay");
        check(bridge.execute(1), "Sequence stops");
        check(bridge.switchActivity("quick controls") && bridge.quickDelay(5000, 0) && bridge.execute(0), "Start held-traffic shutdown regression");
        QProcess held;
        held.start("ping", {"-U", "-n", "-c", "1", "-W", "7", "192.0.2.2"});
        check(held.waitForStarted(), "Held ping starts");
        QTest::qWait(300);
        check(held.state() == QProcess::Running, "Reply held before Stop");
        check(bridge.execute(1), "Stop with held traffic");
        check((held.state() == QProcess::NotRunning || held.waitForFinished(2000)) && held.exitCode() == 0, "Qt Stop releases held traffic");
        check(bridge.quickDelay(100, 0) && bridge.execute(0), "Start before bridge destruction");
    }
    check(ping() < 100, "Bridge destruction stops real capture");
    qInfo("PASS native Qt controls, sequences, persistence, live edits, Stop and destruction");
    return 0;
}
