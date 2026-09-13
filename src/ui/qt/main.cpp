#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>
#include "window_chrome.h"
#include "hud_stacking.h"
#include "text_focus.h"
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
#include "app_bridge.h"
#endif
#include <QQuickItem>
#ifdef Q_OS_LINUX
#include "platform/linux/qt_backend.h"
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
    // Keep the original storage identity so existing preferences and the single-
    // instance lock survive the executable rename. This is not a release label.
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
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
#ifdef Q_OS_LINUX
    LinuxQtBackend network(app.arguments().contains("--direct-helper"));
#endif
    std::unique_ptr<AppBridge> bridge;
#ifdef Q_OS_LINUX
    if (!checkLoad) bridge = std::make_unique<AppBridge>(network.interface());
#else
    if (!checkLoad) bridge = std::make_unique<AppBridge>(windowsNetworkBackend());
#endif
#endif

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    if (bridge) bridge->installStarterPresets();
#endif
    QQmlApplicationEngine engine;
    if (checkLoad) engine.setInitialProperties({{"visible", false}});
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    else engine.setInitialProperties({{"backend", QVariant::fromValue(bridge.get())}, {"page", "quick controls"}});
#endif
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Clumsier.Beta", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    WindowChrome chrome(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));
    TextFocus textFocus(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));
    auto *hud = engine.rootObjects().first()->findChild<QQuickWindow *>("clumsierHud");
    std::unique_ptr<HudStacking> hudStacking;
    if (hud) hudStacking = std::make_unique<HudStacking>(hud);
#ifdef Q_OS_LINUX
    if (bridge) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QObject::connect(&network, &LinuxQtBackend::busyChanged, window, [window, hud](bool busy) {
            window->contentItem()->setEnabled(!busy);
            if (hud) hud->contentItem()->setEnabled(!busy);
            window->setTitle(busy ? "Clumsier — waiting for networking" : "Clumsier");
        });
        QObject::connect(&network, &LinuxQtBackend::failed, bridge.get(), &AppBridge::reportBackendError, Qt::QueuedConnection);
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &network, &LinuxQtBackend::shutdown);
    }
#endif
    // Deployment check: exercise the embedded QML and shipped plugins without
    // opening another window or changing the user's settings.
    if (checkLoad) QTimer::singleShot(0, &app, &QCoreApplication::quit);
    return app.exec();
}
