#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QTest>
#include <QQuickWindow>
#include <QQuickItem>
#include <QDir>
#include <QSettings>
#include <QProcess>
#include <QTimer>
#include <functional>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "app_bridge.h"
#include "hud_stacking.h"

struct FakeNetwork {
    bool running = false, reject = false;
    LagSettings accepted{};
    int starts = 0, stops = 0;
    static bool start(void *context, const CaptureTarget *, const LagSettings *lag, char *error) {
        auto &self = *static_cast<FakeNetwork *>(context);
        if (self.reject) { strcpy(error, "Test start rejected"); return false; }
        ++self.starts; self.accepted = *lag; self.running = true; return true;
    }
    static bool apply(void *context, const LagSettings *lag, char *error) {
        auto &self = *static_cast<FakeNetwork *>(context);
        if (self.reject) { strcpy(error, "Test update rejected"); return false; }
        self.accepted = *lag; return true;
    }
    static void stop(void *context) { auto &self = *static_cast<FakeNetwork *>(context); if (self.running) ++self.stops; self.running = false; }
    static bool isRunning(void *context) { return static_cast<FakeNetwork *>(context)->running; }
    NetworkBackend backend() { static const NetworkBackendOps ops{start, apply, stop, isRunning}; return {&ops, this}; }
};
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ClumsierTests"); app.setApplicationName("BridgeTests");
    QQuickStyle::setStyle("Basic");
    QTemporaryDir library;
    FakeNetwork network;
    if (app.arguments().contains("--shutdown-child")) {
        int result;
        {
            // Real listener threads and a real Qt event loop, but fake capture
            // and isolated files. The parent verifies actual process exit.
            AppBridge bridge(network.backend(), library.path(), true);
            QQmlApplicationEngine engine;
            engine.setInitialProperties({{"backend", QVariant::fromValue(&bridge)},
                {"settingsLocation", QUrl::fromLocalFile(library.filePath("ui.ini"))}, {"page", "quick controls"}});
            engine.load(QUrl::fromLocalFile(QStringLiteral(PREVIEW_QML_DIR "/Main.qml")));
            if (engine.rootObjects().isEmpty()) return 2;
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            auto *hud = window->findChild<QQuickWindow *>("clumsierHud");
            HudStacking stacking(hud);
            if (!bridge.error().isEmpty()) { qCritical() << bridge.error(); return 3; }
            bridge.execute(0);
            if (app.arguments().contains("--hud"))
                QMetaObject::invokeMethod(window, "saveHudPreference", Q_ARG(QVariant, "showHud"), Q_ARG(QVariant, true));
            QTimer::singleShot(400, window, [window] { window->close(); });
            QTimer::singleShot(4000, &app, [&] { app.exit(4); });
            result = app.exec();
            qInfo("Shutdown child: event loop returned");
        }
        qInfo("Shutdown child: objects destroyed");
        return network.running ? 5 : result;
    }
    bool ok = true;
    auto check = [&](bool success, const char *message) { if (!success) { qCritical("FAIL: %s", message); ok = false; } };
    QString savedId, savedServerId;
    {
        AppBridge switching(network.backend(), library.path(), false);
        check(switching.quickDelay(42, 0) && switching.execute(0), "Start quick delay before switching");
        check(!switching.selectPreset("missing") && network.running, "Invalid selection leaves current capture alone");
        check(switching.switchActivity("sequences") && !network.running, "Switch stops quick capture");
        check(!switching.state()["canStart"].toBool() && switching.state()["description"] == "No sequence selected", "Empty sequences page is unarmed");
        check(!switching.execute(0) && !switching.execute(2), "Start and Toggle cannot start missing sequence");
        check(switching.switchActivity("quick controls") && switching.state()["inbound"] == 42, "Return restores accepted quick delay");
    }
    network = {};
    {
        AppBridge bridge(network.backend(), library.path(), false);
        check(bridge.presets().isEmpty(), "No fake sample sequences in a fresh library");
        check(bridge.newPreset(), "Create draft");
        auto draft = bridge.draft(); draft["name"] = "Utopia test";
        auto steps = draft["steps"].toList(); auto first = steps[0].toMap(); first["target_ms"] = 200; steps[0] = first;
        steps.append(QVariantMap{{"name", "Recovery"}, {"note", ""}, {"type", "lowest"}});
        draft["steps"] = steps; bridge.updateDraft(draft);
        check(bridge.dirty(), "Draft reports edits");
        check(!bridge.newPreset(), "Unsaved draft cannot be replaced");
        check(bridge.save(), "Save sequence"); savedId = bridge.selectedId();
        check(!bridge.loadSequence(), "Missing baseline blocks target sequence");
        check(bridge.saveProfile("", "Test server", 40), "Save server baseline");
        savedServerId = bridge.profileId();
        check(bridge.loadSequence(), "Load accepted sequence");
        check(bridge.execute(0) && bridge.execute(0) && network.starts == 1, "Repeated Start is idempotent");
        check(network.accepted.inbound_ms == 160, "Target ping resolves through shared core");
        check(bridge.selectStep(1) && network.accepted.inbound_ms == 0, "Direct step selection changes delay");
        network.reject = true;
        check(!bridge.selectStep(0) && bridge.state()["step"] == 1, "Rejected direct step leaves cursor unchanged");
        network.reject = false;
        check(!bridge.selectStep(99) && bridge.selectStep(0), "Step index is validated");
        draft["name"] = "Unsaved edit"; bridge.updateDraft(draft);
        check(bridge.state()["description"].toString().startsWith("Utopia test"), "Draft does not alter active snapshot");
        bridge.discard();
        network.reject = true;
        check(!bridge.execute(3) && bridge.state()["step"].toInt() == 0 && network.accepted.inbound_ms == 160, "Failed apply preserves step and accepted lag");
        network.reject = false;
        check(bridge.execute(3) && network.accepted.inbound_ms == 0, "Lowest step clears added delay");
        check(bridge.execute(1) && bridge.execute(1) && network.stops == 1, "Repeated Stop is idempotent");
        check(bridge.quickDelay(75, 0), "Quick controls apply delay");
        check(!bridge.state()["sequence"].toBool(), "Quick controls unload sequence");
        network.reject = true;
        check(!bridge.execute(0) && !bridge.state()["running"].toBool(), "Failed start is never shown as running");
        network.reject = false;
        const QUrl exported = QUrl::fromLocalFile(library.filePath("shared.json"));
        check(bridge.exportPreset(exported), "Export sequence");
        check(bridge.importPreset(exported) && bridge.selectedId() != savedId && bridge.presets().size() == 2, "Import generates a fresh ID");
        check(bridge.deletePreset() && bridge.presets().size() == 1, "Delete imported copy");
        check(bridge.selectPreset(savedId), "Reload saved sequence");
        check(bridge.profileId() == savedServerId, "Selecting sequence restores its own server");
        check(bridge.duplicate() && bridge.save(), "Save duplicate for server association test");
        const QString duplicateId = bridge.selectedId();
        check(bridge.saveProfile("", "Other server", 70), "Associate a different server with duplicate");
        const QString otherServerId = bridge.profileId();
        check(bridge.selectPreset(savedId) && bridge.profileId() == savedServerId, "Original keeps first server");
        check(bridge.selectPreset(duplicateId) && bridge.profileId() == otherServerId, "Duplicate keeps second server");
        check(bridge.deletePreset() && bridge.deleteProfile(otherServerId) && bridge.selectPreset(savedId), "Remove association-test copy");
        draft = bridge.draft(); draft["name"] = ""; bridge.updateDraft(draft);
        check(!bridge.save() && bridge.dirty(), "Invalid draft stays editable and cannot overwrite saved file"); bridge.discard();
        check(bridge.saveBinding(2, "W+Mouse4"), "Expanded binding parser available");
        check(!bridge.saveBinding(0, "W+Mouse4"), "Conflicting binding rejected");
        check(bridge.bindings()[2].toMap()["text"].toString().contains("Mouse4"), "Rejected edit preserves existing binding");

        // Load the actual production workspace against isolated data and no hooks.
        // The existing shell test keeps exercising the visual demo independently.
        QQmlApplicationEngine engine;
        bool warnings = false;
        QObject::connect(&engine, &QQmlApplicationEngine::warnings, [&](const QList<QQmlError> &) { warnings = true; });
        engine.setInitialProperties({{"backend", QVariant::fromValue(&bridge)}, {"settingsLocation", QUrl::fromLocalFile(library.filePath("ui.ini"))}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(PREVIEW_QML_DIR "/Main.qml")));
        check(!engine.rootObjects().isEmpty(), "Production UI loads");
        if (!engine.rootObjects().isEmpty()) {
            auto *window = engine.rootObjects().first();
            auto *quickWindow = qobject_cast<QQuickWindow *>(window);
            std::function<QQuickItem *(QQuickItem *, const QString &)> find;
            find = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
                if (item->objectName() == name) return item;
                for (auto *child : item->childItems()) if (auto *result = find(child, name)) return result;
                return nullptr;
            };
            auto click = [&](const char *name) {
                auto *item = find(quickWindow->contentItem(), name);
                check(item != nullptr, name);
                if (!item) return;
                check(QRectF(0, 0, quickWindow->width(), quickWindow->height()).contains(item->mapRectToScene(item->boundingRect())), "Switch control stays inside window");
                QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier,
                    item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
                QTest::qWait(200);
            };
            QTest::qWait(300);
            check(bridge.execute(0) && bridge.execute(3), "Advance active step before highlight check");
            QTest::qWait(100);
            auto *secondStep = find(quickWindow->contentItem(), "liveStep1");
            check(secondStep && secondStep->property("selected").toBool(), "Highlight follows controller step");
            auto *firstStep = find(quickWindow->contentItem(), "liveStep0");
            if (firstStep) {
                QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier,
                    firstStep->mapToScene(QPointF(30, firstStep->height()/2)).toPoint());
                check(bridge.state()["step"] == 0 && network.accepted.inbound_ms == 160, "Clicking row changes live step");
            }
            auto *targetField = find(quickWindow->contentItem(), "liveTargetField");
            check(targetField && targetField->property("readOnly").toBool(), "Playback fields are read-only");
            click("editSequenceButton");
            check(network.running && window->property("switchingActivity").toBool(), "Edit asks before stopping playback");
            click("cancelActivitySwitch");
            check(network.running && targetField->property("readOnly").toBool(), "Cancel keeps playback and locked fields");
            click("editSequenceButton"); click("confirmActivitySwitch");
            check(!network.running && !targetField->property("readOnly").toBool(), "Confirmed Edit stops playback before unlocking fields");
            auto *trafficButton = find(quickWindow->contentItem(), "trafficSettingsButton");
            check(trafficButton && !trafficButton->isVisible(), "Traffic settings hidden by default");
            auto *nameField = find(quickWindow->contentItem(), "sequenceNameField");
            check(nameField != nullptr, "Live sequence editor exists");
            if (nameField) {
                nameField->forceActiveFocus();
                QTest::keyClick(quickWindow, Qt::Key_A, Qt::ControlModifier);
                QTest::keyClick(quickWindow, Qt::Key_X);
                check(bridge.dirty() && bridge.draft()["name"] == "x", "Typing edits the real draft");
                check(!bridge.execute(3), "Playback shortcuts cannot advance a dirty draft");
                click("tab-quick controls");
                click("cancelUnsavedSwitch");
                check(window->property("page") == "sequences" && bridge.dirty(), "Cancel navigation retains draft and page");
                auto invalidDraft = bridge.draft(); invalidDraft["name"] = ""; bridge.updateDraft(invalidDraft);
                click("tab-quick controls"); click("saveUnsavedSwitch");
                check(window->property("page") == "sequences" && bridge.dirty(), "Invalid Save blocks navigation and preserves editor");
                click("cancelUnsavedSwitch");
                bridge.discard();
                auto changedDraft = bridge.draft(); changedDraft["description"] = "Saved during navigation"; bridge.updateDraft(changedDraft);
                click("tab-hotkeys"); click("saveUnsavedSwitch");
                check(window->property("page") == "hotkeys" && !bridge.dirty(), "Save completes navigation");
                click("tab-sequences");
                changedDraft = bridge.draft(); changedDraft["name"] = "Discard this name"; bridge.updateDraft(changedDraft);
                click("tab-quick controls"); click("discardUnsavedSwitch");
                check(window->property("page") == "quick controls" && !bridge.dirty() && bridge.draft()["name"] == "Utopia test", "Discard completes navigation without saving draft");
                click("tab-sequences");
            }
            click("manageServersButton");
            for (const auto &name : {"serverSelector", "addServerButton"}) {
                auto *control = find(quickWindow->contentItem(), name);
                check(control != nullptr, "Server control exists");
                if (!control) continue;
                QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier,
                    control->mapToScene(QPointF(control->width()/2, control->height()/2)).toPoint());
                QTest::qWait(200);
                if (qEnvironmentVariableIsSet("CLUMSIER_SCREENSHOTS")) {
                    const QString directory = qEnvironmentVariable("CLUMSIER_SCREENSHOTS");
                    QDir().mkpath(directory);
                    quickWindow->grabWindow().save(directory + "/" + name + ".png");
                }
                QTest::keyClick(quickWindow, Qt::Key_Escape);
                QTest::qWait(200);
            }
            QTest::keyClick(quickWindow, Qt::Key_Escape); QTest::qWait(100);
            click("tab-quick controls");
            check(bridge.state()["activity"] == "quick controls" && !window->property("switchingActivity").toBool(), "Stopped page switch needs no warning");
            auto *quickField = find(quickWindow->contentItem(), "quickDelayField");
            check(quickField != nullptr, "Quick delay editor exists");
            if (quickField) {
                quickField->forceActiveFocus(); QTest::keyClick(quickWindow, Qt::Key_A, Qt::ControlModifier);
                QTest::keyClick(quickWindow, Qt::Key_2); QTest::qWait(450);
                check(quickField->property("text") == "2", "Status polling preserves partially typed number");
                QTest::keyClick(quickWindow, Qt::Key_5); QTest::qWait(450);
                QTest::keyClick(quickWindow, Qt::Key_0); QTest::qWait(450);
                check(quickField->property("text") == "250", "Slow typing survives multiple status refreshes");
                check(bridge.state()["quickMs"] == 250, "Typing applies quick delay without a button");
                auto enterDelay = [&](const char *value) {
                    quickField->forceActiveFocus();
                    QTest::keyClick(quickWindow, Qt::Key_A, Qt::ControlModifier);
                    QTest::keyClick(quickWindow, Qt::Key_Backspace);
                    for (const char *key = value; *key; ++key) QTest::keyClick(quickWindow, *key);
                    QTest::qWait(250);
                };
                auto *error = find(quickWindow->contentItem(), "quickDelayError");
                enterDelay("");
                check(bridge.state()["quickMs"] == 250 && error && error->isVisible(), "Empty input reports error and keeps accepted delay");
                enterDelay("abc");
                check(bridge.state()["quickMs"] == 250 && quickField->property("text") == "abc" && error->isVisible(), "Invalid text remains editable without changing delay");
                enterDelay("15001");
                check(bridge.state()["quickMs"] == 1500 && error->isVisible(), "Out-of-range input keeps last valid typed value");
                enterDelay("250");
                check(bridge.state()["quickMs"] == 250 && !error->isVisible(), "Valid input clears inline error");
                check(bridge.execute(0), "Start capture for live delay editing");
                enterDelay("0");
                check(bridge.state()["running"].toBool() && bridge.state()["inbound"] == 0, "Zero applies immediately during capture");
                auto *direction = find(quickWindow->contentItem(), "quickDelayDirection");
                check(direction && !direction->isEnabled(), "Traffic direction stays locked during capture");
                bridge.execute(1);
                enterDelay("250");
            }
            click("showHudButton");
            auto *hud = window->findChild<QQuickWindow *>("clumsierHud");
            check(hud && hud->isVisible() && hud->transientParent() == nullptr, "HUD is an independent visible window");
            if (hud) {
                HudStacking stacking(hud);
                check(hud->flags().testFlag(Qt::WindowStaysOnTopHint) && hud->flags().testFlag(Qt::WindowDoesNotAcceptFocus), "HUD requests topmost non-focus window");
#ifdef Q_OS_WIN
                const auto nativeStyle = GetWindowLongPtr(reinterpret_cast<HWND>(hud->winId()), GWL_EXSTYLE);
                check((nativeStyle & WS_EX_TOPMOST) && (nativeStyle & WS_EX_NOACTIVATE), "Windows applies topmost and no-activate styles");
                {
                    QQuickWindow fullscreen;
                    fullscreen.setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
                    fullscreen.showFullScreen(); fullscreen.requestActivate();
                    QTest::qWait(150);
                    const HWND other = reinterpret_cast<HWND>(fullscreen.winId());
                    const HWND overlay = reinterpret_cast<HWND>(hud->winId());
                    auto aboveFullscreen = [&] {
                        for (HWND above = GetWindow(other, GW_HWNDPREV); above; above = GetWindow(above, GW_HWNDPREV))
                            if (above == overlay) return true;
                        return false;
                    };
                    SetForegroundWindow(other);
                    QTest::qWait(100);
                    if (GetForegroundWindow() == other) {
                    // Simulate an already-focused app promoting itself during
                    // an F11 transition; the OS delivers the location event.
                    RECT area{}; GetWindowRect(other, &area);
                    SetWindowPos(other, HWND_TOPMOST, area.left, area.top, area.right - area.left - 1, area.bottom - area.top,
                                 SWP_NOACTIVATE);
                    check(!aboveFullscreen(), "Fullscreen window initially covers HUD");
                    for (int attempt = 0; attempt < 15 && !aboveFullscreen(); ++attempt) QTest::qWait(100);
                    check(aboveFullscreen(), "HUD repairs ordering after fullscreen resize");
                    check(GetForegroundWindow() == other, "Repair leaves fullscreen app focused");
                    hud->hide();
                    SetWindowPos(other, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                    QTest::qWait(150);
                    check(!hud->isVisible(), "Foreground changes do not reshow hidden HUD");
                    hud->show(); QTest::qWait(150);
                    check(aboveFullscreen(), "Showing HUD restores ordering");
                    } else {
                        // Windows denies foreground requests when another app
                        // is being used. Do not mistake that for a HUD failure.
                        qInfo("SKIP fullscreen foreground test: Windows declined focus; run again on an idle desktop.");
                    }
                    fullscreen.close();
                }

#endif
                quickWindow->showMinimized(); QTest::qWait(150);
                check(hud->isVisible(), "HUD survives minimizing editor");
                quickWindow->showNormal(); QTest::qWait(150);
                QMetaObject::invokeMethod(window, "saveHudPreference", Q_ARG(QVariant, "hudScale"), Q_ARG(QVariant, 1.25));
                QTest::qWait(100); check(hud->width() == 450, "HUD size changes independently of editor");
                auto *toggle = find(hud->contentItem(), "hudToggle");
                check(toggle && toggle->isEnabled(), "HUD playback control is available");
                if (toggle) {
#ifdef Q_OS_WIN
                    const HWND foreground = GetForegroundWindow();
#endif
                    QTest::mouseClick(hud, Qt::LeftButton, Qt::NoModifier, toggle->mapToScene(QPointF(30, toggle->height()/2)).toPoint());
                    check(network.running, "HUD starts accepted quick delay");
#ifdef Q_OS_WIN
                    check(GetForegroundWindow() == foreground, "HUD click preserves foreground window");
#endif
                    QTest::mouseClick(hud, Qt::LeftButton, Qt::NoModifier, toggle->mapToScene(QPointF(30, toggle->height()/2)).toPoint());
                    check(!network.running, "HUD stops capture");
                }
                check(bridge.switchActivity("sequences") && bridge.execute(0) && bridge.execute(3), "Advance sequence for HUD check");
                auto *hudStep = find(hud->contentItem(), "hudStepName");
                check(hudStep && hudStep->property("text").toString().startsWith("Recovery"), "HUD follows accepted sequence step");
                QTest::qWait(150);
                if (qEnvironmentVariableIsSet("CLUMSIER_SCREENSHOTS"))
                    hud->grabWindow().save(qEnvironmentVariable("CLUMSIER_SCREENSHOTS") + "/hud.png");
                hud->close(); QTest::qWait(100);
                check(!hud->isVisible() && network.running, "Hiding HUD does not stop capture");
                bridge.execute(1); bridge.switchActivity("quick controls");
            }
            check(bridge.execute(0), "Start before confirmation test");
            click("tab-sequences");
            check(window->property("switchingActivity").toBool() && network.running && window->property("page") == "quick controls", "Confirmation precedes stop and navigation");
            if (qEnvironmentVariableIsSet("CLUMSIER_SCREENSHOTS")) {
                const QString directory = qEnvironmentVariable("CLUMSIER_SCREENSHOTS");
                QDir().mkpath(directory);
                quickWindow->grabWindow().save(directory + "/switch-confirmation.png");
            }
            click("cancelActivitySwitch");
            check(network.running && window->property("page") == "quick controls", "Cancel preserves capture and page");
            click("tab-sequences");
            auto *warningCheck = find(quickWindow->contentItem(), "skipSwitchWarning");
            auto *indicator = warningCheck ? warningCheck->property("indicator").value<QObject *>() : nullptr;
            check(indicator != nullptr, "Checkbox has themed indicator");
            if (indicator) {
                const QVariant before = indicator->property("color");
                const QPoint point = warningCheck->mapToScene(QPointF(8, warningCheck->height()/2)).toPoint();
                QTest::mousePress(quickWindow, Qt::LeftButton, Qt::NoModifier, point);
                QTest::qWait(100);
                check(indicator->property("color") == before, "Checkbox press preserves themed fill");
                QTest::mouseRelease(quickWindow, Qt::LeftButton, Qt::NoModifier, point);
            }
            click("confirmActivitySwitch");
            check(!network.running && window->property("page") == "sequences" && bridge.state()["sequence"].toBool(), "Continue stops capture and arms selected sequence");
            QSettings savedUi(library.filePath("ui.ini"), QSettings::IniFormat);
            check(savedUi.value("shell/skipSwitchWarning").toBool(), "Warning opt-out written to persistent preferences");
            check(bridge.execute(0) && network.accepted.inbound_ms == 160, "Start now uses selected sequence");
            click("tab-quick controls");
            check(!network.running && !window->property("switchingActivity").toBool(), "Opt-out skips warning, not Stop");
            // The selection itself also stops capture, even if the UI warning
            // is suppressed. It cannot keep playing an old sequence snapshot.
            check(bridge.switchActivity("sequences") && bridge.execute(0), "Restart selected sequence");
            check(bridge.importPreset(exported) && !network.running, "Selecting imported sequence stops capture");
            check(bridge.profileId().isEmpty() && !bridge.state()["canStart"].toBool(), "Imported sequence needs its own local server");
            bridge.selectProfile(savedServerId);
            check(bridge.execute(0) && bridge.selectPreset(savedId) && !network.running, "Switch between saved sequences stops capture");
            const QString copyId = bridge.presets()[0].toMap()["id"].toString() == savedId ? bridge.presets()[1].toMap()["id"].toString() : bridge.presets()[0].toMap()["id"].toString();
            check(bridge.selectPreset(copyId) && bridge.deletePreset() && bridge.selectPreset(savedId), "Remove switching-test copy");
            window->setProperty("page", "settings"); QTest::qWait(200);
            click("advancedModeSetting");
            QSettings advancedSettings(library.filePath("ui.ini"), QSettings::IniFormat);
            check(advancedSettings.value("shell/advancedMode").toBool(), "Advanced preference saved");
            window->setProperty("page", "sequences"); QTest::qWait(100);
            check(trafficButton && trafficButton->isVisible(), "Advanced mode reveals traffic settings");
            quickWindow->resize(760, 560); QTest::qWait(250);
            auto *startControl = find(quickWindow->contentItem(), "startButton");
            auto *previousControl = find(quickWindow->contentItem(), "previousPlaybackButton");
            const QRectF viewport(0, 0, quickWindow->width(), quickWindow->height());
            check(startControl && previousControl && viewport.contains(startControl->mapRectToScene(startControl->boundingRect()))
                && viewport.contains(previousControl->mapRectToScene(previousControl->boundingRect())), "Playback controls fit narrow window");
            if (qEnvironmentVariableIsSet("CLUMSIER_SCREENSHOTS"))
                quickWindow->grabWindow().save(qEnvironmentVariable("CLUMSIER_SCREENSHOTS") + "/live-narrow.png");
            quickWindow->resize(1180, 780); QTest::qWait(100);
            for (const auto &page : {"sequences", "quick controls", "hotkeys", "settings"}) {
                window->setProperty("page", page); QTest::qWait(250);
                if (qEnvironmentVariableIsSet("CLUMSIER_SCREENSHOTS")) {
                    const QString directory = qEnvironmentVariable("CLUMSIER_SCREENSHOTS");
                    QDir().mkpath(directory);
                    quickWindow->grabWindow().save(directory + "/live-" + page + ".png");
                }
            }
        }
        check(!warnings, "Production UI has no QML warnings");
    }
    {
        AppBridge reopened(network.backend(), library.path(), false);
        check(reopened.presets().size() == 1 && reopened.profiles().size() == 1, "Library survives a new bridge instance");
        check(reopened.selectPreset(savedId) && reopened.draft()["name"] == "Utopia test", "Saved sequence survives restart and rejected edits");
        check(reopened.profileId() == savedServerId, "Sequence server survives restart");
        check(reopened.switchActivity("quick controls") && reopened.execute(0), "Run quick controls after restart");
        QQmlApplicationEngine restartedUi;
        restartedUi.setInitialProperties({{"backend", QVariant::fromValue(&reopened)},
            {"settingsLocation", QUrl::fromLocalFile(library.filePath("ui.ini"))}, {"page", "quick controls"}});
        restartedUi.load(QUrl::fromLocalFile(QStringLiteral(PREVIEW_QML_DIR "/Main.qml")));
        check(!restartedUi.rootObjects().isEmpty(), "Restarted UI loads");
        if (!restartedUi.rootObjects().isEmpty()) {
            auto *window = restartedUi.rootObjects().first();
            check(QMetaObject::invokeMethod(window, "requestSwitch", Q_ARG(QVariant, "page"), Q_ARG(QVariant, "sequences")), "Request switch after restart");
            QTest::qWait(100);
            check(!network.running && window->property("page") == "sequences" && !window->property("switchingActivity").toBool(), "Opt-out survives restart and still stops capture");
            check(reopened.state()["canStart"].toBool() && reopened.state()["expected"] == 200, "Restored server prepares correct sequence after restart");
            auto *quickWindow = qobject_cast<QQuickWindow *>(window);
            std::function<QQuickItem *(QQuickItem *, const QString &)> find;
            find = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
                if (item->objectName() == name) return item;
                for (auto *child : item->childItems()) if (auto *result = find(child, name)) return result;
                return nullptr;
            };
            auto *traffic = find(quickWindow->contentItem(), "trafficSettingsButton");
            check(traffic && traffic->isVisible(), "Advanced mode survives restart");
            window->setProperty("page", "settings"); QTest::qWait(100);
            auto *warning = find(quickWindow->contentItem(), "switchWarningSetting");
            check(warning != nullptr, "Switch confirmation setting exists");
            if (warning) {
                QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier,
                    warning->mapToScene(QPointF(8, warning->height()/2)).toPoint());
                check(reopened.execute(0), "Start before restored warning check");
                QMetaObject::invokeMethod(window, "requestSwitch", Q_ARG(QVariant, "page"), Q_ARG(QVariant, "quick controls"));
                QTest::qWait(100);
                check(window->property("switchingActivity").toBool() && network.running, "Settings restores switch confirmation");
                QTest::keyClick(quickWindow, Qt::Key_Escape);
                reopened.execute(1);
                auto closingDraft = reopened.draft(); closingDraft["name"] = "Unsaved on close"; reopened.updateDraft(closingDraft);
                quickWindow->close(); QTest::qWait(100);
                auto *cancel = find(quickWindow->contentItem(), "cancelUnsavedSwitch");
                check(quickWindow->isVisible() && cancel && cancel->isVisible(), "Close offers unsaved choices");
                if (cancel) {
                    QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier, cancel->mapToScene(QPointF(30, cancel->height()/2)).toPoint());
                    check(quickWindow->isVisible() && reopened.dirty(), "Cancel close retains draft");
                }
                reopened.discard();
            }
        }
    }
    for (bool withHud : {false, true}) {
        QProcess child;
        QStringList arguments{"--shutdown-child"};
        if (withHud) arguments.append("--hud");
        child.start(QCoreApplication::applicationFilePath(), arguments);
        const bool finished = child.waitForStarted(3000) && child.waitForFinished(7000);
        if (!finished) { child.kill(); child.waitForFinished(1000); }
        check(finished && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
              withHud ? "Closing editor with HUD exits process and stops capture" : "Closing editor exits process and stops capture");
        if (!finished || child.exitCode() != 0) qCritical().noquote() << child.readAllStandardError();
    }
    qInfo() << (ok ? "Bridge checks passed" : "Bridge checks failed");
    return ok ? 0 : 1;
}
