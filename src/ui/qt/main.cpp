#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>
#include <type_traits>
#include "window_chrome.h"
#include "hud_stacking.h"
#include "text_focus.h"
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX) || defined(Q_OS_MAC)
#include "app_bridge.h"
#endif
#include <QQuickItem>
#ifdef Q_OS_LINUX
#include "platform/linux/qt_backend.h"
#endif
#ifdef Q_OS_MAC
#include "platform/macos/qt_backend.h"
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
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX) || defined(Q_OS_MAC)
#ifdef Q_OS_LINUX
    LinuxQtBackend network(app.arguments().contains("--direct-helper"));
#endif
#ifdef Q_OS_MAC
    MacQtBackend network;
#endif
    std::unique_ptr<AppBridge> bridge;
#if defined(Q_OS_WIN)
    if (!checkLoad) bridge = std::make_unique<AppBridge>(windowsNetworkBackend());
#else
    if (!checkLoad) bridge = std::make_unique<AppBridge>(network.interface());
#endif
#endif

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX) || defined(Q_OS_MAC)
    if (bridge) bridge->installStarterPresets();
#endif
    QQmlApplicationEngine engine;
    if (checkLoad) engine.setInitialProperties({{"visible", false}});
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX) || defined(Q_OS_MAC)
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
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
    if (bridge) {
        // Linux and macOS adapters expose the same signal surface; derive the
        // type from the instance so this block compiles on both.
        using BackendType = std::remove_reference_t<decltype(network)>;
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QObject::connect(&network, &BackendType::busyChanged, window, [window, hud](bool busy) {
            window->contentItem()->setEnabled(!busy);
            if (hud) hud->contentItem()->setEnabled(!busy);
            window->setTitle(busy ? "Clumsier — waiting for networking" : "Clumsier");
        });
        QObject::connect(&network, &BackendType::failed, bridge.get(), &AppBridge::reportBackendError, Qt::QueuedConnection);
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &network, &BackendType::shutdown);
    }
#endif
    // Deployment check: exercise the embedded QML and shipped plugins without
    // opening another window or changing the user's settings.
    if (checkLoad) QTimer::singleShot(0, &app, &QCoreApplication::quit);
    return app.exec();
}
