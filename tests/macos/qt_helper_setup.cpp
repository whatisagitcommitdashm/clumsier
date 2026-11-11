#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include "platform/macos/dummynet_backend.h"
#include "platform/macos/helper_setup.h"

class FakeSetup : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool packaged READ packaged CONSTANT)
    Q_PROPERTY(bool firstRun READ firstRun CONSTANT)
    Q_PROPERTY(bool busy READ busy CONSTANT)
    Q_PROPERTY(QString message READ message CONSTANT)
public:
    bool packaged() const { return true; }
    bool firstRun() const { return true; }
    bool busy() const { return false; }
    QString message() const { return "Test only: no administrator prompt"; }
    int authorized = 0, acknowledged = 0;
    Q_INVOKABLE void authorize() { ++authorized; }
    Q_INVOKABLE void acknowledge() { ++acknowledged; }
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir settings;
    if (!settings.isValid()) return 1;
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settings.path());
    app.setOrganizationName("ClumsierSetupTests");
    app.setApplicationName("NoPrivileges");
    QSettings().clear();
    DummynetBackend network(settings.filePath("not-running.sock"));
    MacHelperSetup setup(&network);
    if (setup.packaged() || setup.busy() || !setup.firstRun()) return 2;
    // Checkout instances cannot start administrator authorization.
    setup.authorize();
    if (setup.busy() || !setup.message().isEmpty()) return 3;
    setup.acknowledge();
    if (setup.firstRun()) return 4;

    FakeSetup fake;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"visible", false}, {"localHelper", QVariant::fromValue(&fake)}});
    engine.load(QUrl::fromLocalFile(QStringLiteral(BETA_QML_DIR "/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 5;
    QObject *window = engine.rootObjects().first();
    QObject *dialog = window->findChild<QObject *>("localSetupDialog");
    if (!dialog || !QTest::qWaitFor([&] { return dialog->property("visible").toBool(); })) return 6;
    if (!window->property("inputPaused").toBool()) return 7;
    auto *button = window->findChild<QObject *>("localAuthorizeButton");
    if (!button || !QMetaObject::invokeMethod(button, "clicked")) return 8;
    if (fake.authorized != 1) return 9;
    auto *next = window->findChild<QObject *>("localSetupContinueButton");
    if (!next || !QMetaObject::invokeMethod(next, "clicked")) return 10;
    if (fake.acknowledged != 1) return 11;
    if (!QTest::qWaitFor([&] { return !dialog->property("visible").toBool(); })) return 12;
    return 0;
}
#include "qt_helper_setup.moc"
