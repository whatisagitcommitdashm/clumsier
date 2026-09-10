#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>
#include "window_chrome.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    // Separate identity keeps prototype preferences out of the working app.
    app.setOrganizationName("Clumsier");
    app.setApplicationName("ClumsierUiPreview");
    const bool checkLoad = app.arguments().contains("--check-load");
    if (checkLoad) app.setApplicationName("ClumsierUiPreviewCheck");
    QQuickStyle::setStyle("Basic");

    QQmlApplicationEngine engine;
    if (checkLoad) engine.setInitialProperties({{"visible", false}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Clumsier.Preview", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    WindowChrome chrome(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));
    // Deployment check: exercise the embedded QML and shipped plugins without
    // opening another window or changing the user's preview settings.
    if (checkLoad) QTimer::singleShot(0, &app, &QCoreApplication::quit);
    return app.exec();
}
