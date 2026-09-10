#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>
#include "window_chrome.h"
#include "hud_stacking.h"
#ifdef Q_OS_WIN
#include "app_bridge.h"
#endif
#include <QDir>
#include <QLockFile>
#include <QStandardPaths>
#ifdef Q_OS_WIN
extern "C" {
#include "backends/windows/backend.h"
}
#endif

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    // Separate identity keeps prototype preferences out of the working app.
    app.setOrganizationName("Clumsier");
    app.setApplicationName("ClumsierUiPreview");
    const bool checkLoad = app.arguments().contains("--check-load");
    if (checkLoad) app.setApplicationName("ClumsierUiPreviewCheck");
    QQuickStyle::setStyle("Basic");

    // Only one Qt controller may own capture and the global hotkey listener.
    const QString lockDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(lockDirectory);
    QLockFile lock(lockDirectory + "/runtime.lock");
    if (!checkLoad && !lock.tryLock(0)) return 2;
#ifdef Q_OS_WIN
    std::unique_ptr<AppBridge> bridge;
    if (!checkLoad) bridge = std::make_unique<AppBridge>(windowsNetworkBackend());
#endif

    QQmlApplicationEngine engine;
    if (checkLoad) engine.setInitialProperties({{"visible", false}});
#ifdef Q_OS_WIN
    else engine.setInitialProperties({{"backend", QVariant::fromValue(bridge.get())}, {"page", "quick controls"}});
#endif
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Clumsier.Preview", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    WindowChrome chrome(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));
    auto *hud = engine.rootObjects().first()->findChild<QQuickWindow *>("clumsierHud");
    std::unique_ptr<HudStacking> hudStacking;
    if (hud) hudStacking = std::make_unique<HudStacking>(hud);
    // Deployment check: exercise the embedded QML and shipped plugins without
    // opening another window or changing the user's preview settings.
    if (checkLoad) QTimer::singleShot(0, &app, &QCoreApplication::quit);
    return app.exec();
}
