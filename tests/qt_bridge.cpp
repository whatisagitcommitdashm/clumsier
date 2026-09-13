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
#include <QJSValue>
#include <functional>
#include <algorithm>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "app_bridge.h"
#include "hud_stacking.h"
#include "text_focus.h"

struct FakeNetwork {
    bool running = false, reject = false;
    LagSettings accepted{};
    int starts = 0, stops = 0, rejectStarts = 0;
    static bool start(void *context, const CaptureTarget *, const LagSettings *lag, char *error) {
        auto &self = *static_cast<FakeNetwork *>(context);
        if (self.reject || self.rejectStarts > 0) { if (self.rejectStarts > 0) --self.rejectStarts; strcpy(error, "Test start rejected"); return false; }
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
            engine.load(QUrl::fromLocalFile(QStringLiteral(BETA_QML_DIR "/Main.qml")));
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
    {
        QTemporaryDir edits;
        FakeNetwork capture;
        AppBridge bridge(capture.backend(), edits.path(), false);
        check(bridge.quickDelay(50, 0) && bridge.execute(0), "Run before replacement test");
        capture.rejectStarts = 1;
        check(!bridge.quickDelay(200, 1) && capture.running && capture.accepted.inbound_ms == 50,
            "Failed traffic restart restores previous running configuration");
        check(bridge.state()["quickMs"] == 50 && bridge.state()["quickDirection"] == 0,
            "Rejected restart keeps accepted UI values");
        check(bridge.quickDelay(200, 1) && capture.running && capture.accepted.outbound_ms == 200,
            "Changed traffic restarts into the new configuration");
        capture.rejectStarts = 2;
        check(!bridge.quickDelay(70, 0) && !capture.running && !bridge.error().isEmpty(),
            "Failed replacement and rollback report stopped capture");
        check(bridge.saveProfile("", "Recent server", 35), "Save recent server");
        const QString server = bridge.profileId();
        check(bridge.newPreset() && bridge.profileId() == server && bridge.save(), "New sequence defaults to recent server");
        check(bridge.execute(0), "Start sequence for atomic edit test");
        capture.reject = true;
        check(!bridge.saveProfile(server, "Recent server", 100, false), "Rejected live baseline is not saved");
        capture.reject = false; bridge.refresh();
        check(bridge.profiles()[0].toMap()["baseline"] == 35, "Rejected live baseline retains stored value");
        const QVariantMap original = bridge.draft();
        auto invalid = original; invalid["name"] = "";
        check(!bridge.commitDraft(invalid) && bridge.draft() == original && capture.running, "Invalid commit preserves draft and running state");
        check(bridge.setBindingEnabled(0, false) && bridge.execute(1) && !bridge.executeHotkey(0), "Individual disabled hotkey cannot start");
        check(bridge.execute(0), "Individual hotkey toggle does not disable mouse Start");
        bridge.execute(1);
        AppBridge reopened(capture.backend(), edits.path(), false);
        check(!reopened.executeHotkey(0), "Individual hotkey preference survives reopen");
        check(reopened.newPreset() && reopened.profileId() == server, "Recent server survives reopen");
    }
    QString savedId, savedServerId;
    {
        QTemporaryDir starters, recipient;
        FakeNetwork capture;
        AppBridge bridge(capture.backend(), starters.path(), false);
        check(bridge.installStarterPresets() && bridge.presets().size() == 3, "Fresh library receives three bundled sequences");
        check(bridge.missingServers() == QStringList{"Mineplex"} && bridge.profiles().isEmpty(), "Starters request one personal baseline, never bundle the author's ping");
        QStringList names;
        for (const auto &entry : bridge.presets()) names.append(entry.toMap()["name"].toString());
        check(names == QStringList{"Good Basic", "Pirate Bay", "Skylands"}, "Requested starter names are preserved");
        const QString first = bridge.presets()[0].toMap()["id"].toString();
        bridge.selectPreset(first);
        check(!bridge.execute(0) && !capture.running, "Unconfigured starter cannot start");
        check(!bridge.resolveServer("Mineplex", -1) && bridge.profiles().isEmpty(), "Invalid baseline creates no server");
        bridge.clearError();
        {
            QQmlApplicationEngine engine;
            engine.setInitialProperties({{"backend", QVariant::fromValue(&bridge)}, {"settingsLocation", QUrl::fromLocalFile(starters.filePath("ui.ini"))}});
            engine.load(QUrl::fromLocalFile(QStringLiteral(BETA_QML_DIR "/Main.qml")));
            QTest::qWait(600);
            check(!engine.rootObjects().isEmpty(), "First-run shell loads");
            if (!engine.rootObjects().isEmpty()) {
                auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
                auto *dialog = window->findChild<QObject *>("baselineSetupDialog");
                check(dialog && dialog->property("visible").toBool(), "First launch opens the server-baseline prompt");
                if (qEnvironmentVariableIsSet("CLUMSIER_SCREENSHOTS")) {
                    QDir().mkpath(qEnvironmentVariable("CLUMSIER_SCREENSHOTS"));
                    window->grabWindow().save(qEnvironmentVariable("CLUMSIER_SCREENSHOTS") + "/starter-baseline.png");
                }
                check(bridge.resolveServer("Mineplex", 47), "Resolve shared server baseline");
            }
        }
        check(bridge.missingServers().isEmpty() && bridge.profiles().size() == 1, "One baseline resolves all starter sequences");
        for (const auto &entry : bridge.presets()) {
            check(bridge.selectPreset(entry.toMap()["id"].toString()) && !bridge.profileId().isEmpty(), "Every starter uses the saved server");
        }
        check(bridge.installStarterPresets() && bridge.presets().size() == 3, "Starter installation is idempotent");
        const QUrl exported = QUrl::fromLocalFile(starters.filePath("shared.json"));
        check(bridge.exportPreset(exported), "Export portable server hint");
        QFile shared(exported.toLocalFile()); shared.open(QIODevice::ReadOnly);
        const QByteArray bytes = shared.readAll(); shared.close();
        check(bytes.contains("Mineplex") && !bytes.contains("baseline"), "Shared JSON includes server name but not personal baseline");
        AppBridge imported(capture.backend(), recipient.path(), false);
        check(imported.importPreset(exported) && imported.missingServers() == QStringList{"Mineplex"}, "Unknown imported server requests baseline");
        check(imported.resolveServer("Mineplex", 80) && imported.profiles().size() == 1, "Recipient supplies their own baseline");
        check(imported.importPreset(exported) && imported.missingServers().isEmpty() && imported.profiles().size() == 1, "Known imported server reuses local baseline");
        AppBridge reopened(capture.backend(), starters.path(), false);
        check(reopened.missingServers().isEmpty() && reopened.selectPreset(first) && !reopened.profileId().isEmpty(), "Baseline and sequence association survive restart");
        check(reopened.deletePreset() && reopened.installStarterPresets() && reopened.presets().size() == 2, "Deleted starters are not restored");
        QTemporaryDir existing;
        AppBridge established(capture.backend(), existing.path(), false);
        check(established.newPreset() && established.save() && established.installStarterPresets() && established.presets().size() == 1, "Existing user libraries are left intact");
    }
    {
        QTemporaryDir features;
        QString original;
        {
            FakeNetwork capture;
            AppBridge bridge(capture.backend(), features.path(), false);
            bridge.setHotkeysEnabled(false);
            check(!bridge.executeHotkey(0) && !capture.running, "Disabled hotkeys cannot start capture");
            check(bridge.execute(0) && capture.running && bridge.execute(1), "Mouse actions still work with hotkeys off");
            bridge.setInputPaused(true); bridge.setHotkeysEnabled(true);
            check(!bridge.executeHotkey(0), "Enabling hotkeys does not override text-input pause");
            bridge.setInputPaused(false);
            check(bridge.executeHotkey(0) && bridge.execute(1), "Hotkeys resume when enabled and not editing");
            bridge.setHotkeysEnabled(false);
            bridge.setAutoSave(true);
            bridge.newPreset();
            auto draft = bridge.draft(); draft["name"] = "Auto saved"; bridge.updateDraft(draft);
            check(QTest::qWaitFor([&] { return !bridge.dirty(); }, 1500), "Autosave persists a valid draft");
            original = bridge.selectedId();
            draft = bridge.draft(); draft["name"] = ""; bridge.updateDraft(draft); QTest::qWait(650);
            check(bridge.dirty() && !bridge.error().isEmpty(), "Invalid autosave preserves draft with validation error");
            bridge.discard();
            check(bridge.draft()["name"] == "Auto saved", "Invalid autosave does not replace saved data");
            check(bridge.saveProfile("", "Batch server", 45), "Associate server before batch copy");
            const auto server = bridge.profileId();
            check(!bridge.batchSequences("delete", {original, "invalid"}) && bridge.presets().size() == 1, "Batch validates all IDs before deleting anything");
            check(bridge.batchSequences("duplicate", {original}) && bridge.presets().size() == 2, "Batch duplicate writes a saved copy");
            QStringList ids;
            for (const auto &entry : bridge.presets()) ids.append(entry.toMap()["id"].toString());
            for (const auto &id : ids) check(bridge.selectPreset(id) && bridge.profileId() == server, "Batch copies retain server association");
            QTemporaryDir exports;
            check(bridge.batchSequences("export", ids, QUrl::fromLocalFile(exports.path())), "Batch export writes each selection");
            check(bridge.batchSequences("export", ids, QUrl::fromLocalFile(exports.path())) && QDir(exports.path()).entryList({"*.json"}, QDir::Files).size() == 4, "Repeated batch exports never overwrite files");
            check(bridge.execute(0), "Start selected sequence before batch deletion");
            check(bridge.batchSequences("delete", ids) && bridge.presets().isEmpty() && !capture.running && !bridge.state()["canStart"].toBool(), "Deleting active selection stops capture and clears readiness");
        }
        FakeNetwork capture;
        AppBridge reopened(capture.backend(), features.path(), false);
        check(!reopened.hotkeysEnabled() && reopened.autoSave(), "Hotkey and autosave preferences survive restart");
    }
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
#ifdef Q_OS_WIN
        check(bridge.saveBinding(2, "W+Mouse4"), "Expanded binding parser available");
        check(!bridge.saveBinding(0, "W+Mouse4"), "Conflicting binding rejected");
        check(bridge.bindings()[2].toMap()["text"].toString().contains("Mouse4"), "Rejected edit preserves existing binding");
#else
        check(!bridge.globalHotkeysAvailable() && bridge.bindings().isEmpty(), "Linux does not advertise unsupported global bindings");
#endif

        // Load the actual production workspace against isolated data and no hooks.
        // The existing shell test keeps exercising the visual demo independently.
        QQmlApplicationEngine engine;
        bool warnings = false;
        QObject::connect(&engine, &QQmlApplicationEngine::warnings, [&](const QList<QQmlError> &) { warnings = true; });
        engine.setInitialProperties({{"backend", QVariant::fromValue(&bridge)}, {"settingsLocation", QUrl::fromLocalFile(library.filePath("ui.ini"))}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(BETA_QML_DIR "/Main.qml")));
        check(!engine.rootObjects().isEmpty(), "Production UI loads");
        if (!engine.rootObjects().isEmpty()) {
            auto *window = engine.rootObjects().first();
            auto *quickWindow = qobject_cast<QQuickWindow *>(window);
            TextFocus textFocus(quickWindow);
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
                // Reach controls below the fold through the real scroll view.
                for (auto *parent = item->parentItem(); parent; parent = parent->parentItem()) {
                    if (!parent->property("contentY").isValid()) continue;
                    const auto bounds = item->mapRectToItem(parent, item->boundingRect());
                    if (bounds.top() < 0 || bounds.bottom() > parent->height()) {
                        const double maximum = (std::max)(0.0, parent->property("contentHeight").toDouble() - parent->height());
                        const double desired = parent->property("contentY").toDouble() + bounds.top() - 12;
                        parent->setProperty("contentY", std::clamp(desired, 0.0, maximum));
                        QTest::qWait(50);
                    }
                }
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
            check(targetField && !targetField->property("readOnly").toBool(), "Playback fields remain editable");
            if (targetField) {
                targetField->forceActiveFocus(); QTest::keyClick(quickWindow, Qt::Key_A, Qt::ControlModifier);
                QTest::keyClick(quickWindow, Qt::Key_3); QTest::qWait(250);
                check(network.accepted.inbound_ms == 160, "Intermediate target does not affect capture");
                QTest::keyClick(quickWindow, Qt::Key_0); QTest::keyClick(quickWindow, Qt::Key_0);
                QTest::keyClick(quickWindow, Qt::Key_Return); QTest::qWait(50);
                check(network.running && network.accepted.inbound_ms == 260, "Leaving target field applies live delay");
                bridge.discard();
                check(network.accepted.inbound_ms == 160, "Discard restores the saved live configuration");
            }
            bridge.execute(1);
            auto *trafficButton = find(quickWindow->contentItem(), "trafficSettingsButton");
            check(trafficButton && !trafficButton->isVisible(), "Traffic settings hidden by default");
            auto *nameField = find(quickWindow->contentItem(), "sequenceNameField");
            check(nameField != nullptr, "Live sequence editor exists");
            if (nameField) {
                nameField->forceActiveFocus();
                QTest::keyClick(quickWindow, Qt::Key_A, Qt::ControlModifier);
                QTest::keyClick(quickWindow, Qt::Key_X);
                QTest::keyClick(quickWindow, Qt::Key_Return);
                check(bridge.dirty() && bridge.draft()["name"] == "x", "Finishing name edits the real draft");
                check(bridge.execute(3), "Playback can advance with valid unsaved edits");
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
                click("quickDelayField");
                check(quickField->hasActiveFocus(), "Click enters quick delay editing");
                QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier, QPoint(700, 500));
                check(!quickField->hasActiveFocus(), "Blank-space click exits unchanged text field");
                click("quickDelayField");
                check(quickField->hasActiveFocus(), "Click re-enters unchanged text field");
                QTest::keyClick(quickWindow, Qt::Key_Escape);
                check(!quickField->hasActiveFocus(), "Escape exits text editing");
                quickField->forceActiveFocus(); QTest::keyClick(quickWindow, Qt::Key_A, Qt::ControlModifier);
                QTest::keyClick(quickWindow, Qt::Key_2); QTest::qWait(450);
                check(quickField->property("text") == "2", "Status polling preserves partially typed number");
                QTest::keyClick(quickWindow, Qt::Key_5); QTest::qWait(450);
                QTest::keyClick(quickWindow, Qt::Key_0); QTest::qWait(450);
                check(quickField->property("text") == "250", "Slow typing survives multiple status refreshes");
                QTest::keyClick(quickWindow, Qt::Key_Return);
                check(bridge.state()["quickMs"] == 250, "Leaving field applies quick delay without a button");
                auto enterDelay = [&](const char *value) {
                    quickField->forceActiveFocus();
                    QTest::keyClick(quickWindow, Qt::Key_A, Qt::ControlModifier);
                    QTest::keyClick(quickWindow, Qt::Key_Backspace);
                    for (const char *key = value; *key; ++key) QTest::keyClick(quickWindow, *key);
                    QTest::qWait(250);
                };
                enterDelay("");
                check(bridge.state()["quickMs"] == 250 && quickField->property("text") == "", "Empty draft stays empty while focused");
                QTest::keyClick(quickWindow, Qt::Key_Return);
                check(bridge.state()["quickMs"] == 0 && quickField->property("text") == "0", "Empty field commits zero on blur");
                click("quickDelayField"); QTest::qWait(30);
                check(quickField->property("selectedText") == "0", "Entering zero selects it for replacement");
                enterDelay("abc"); QTest::keyClick(quickWindow, Qt::Key_Return);
                check(bridge.state()["quickMs"] == 0 && quickField->property("text") == "0" && !bridge.error().isEmpty(), "Invalid input restores accepted value and reports error");
                enterDelay("15001"); QTest::keyClick(quickWindow, Qt::Key_Return);
                check(bridge.state()["quickMs"] == 0, "Out-of-range input never becomes live");
                enterDelay("250"); QTest::keyClick(quickWindow, Qt::Key_Return);
                check(bridge.state()["quickMs"] == 250, "Valid field commits once");
                check(bridge.execute(0), "Start capture for live delay editing");
                enterDelay("0");
                check(network.accepted.inbound_ms == 250, "Live delay stays unchanged while typing");
                QTest::keyClick(quickWindow, Qt::Key_Return);
                check(network.running && network.accepted.inbound_ms == 0, "Zero applies when editing finishes");
                auto *direction = find(quickWindow->contentItem(), "quickDelayDirection");
                check(direction && direction->isEnabled(), "Direction can be changed during capture");
                bridge.execute(1); enterDelay("250"); QTest::keyClick(quickWindow, Qt::Key_Return);
            }
            click("showHudButton");
            auto *hud = window->findChild<QQuickWindow *>("clumsierHud");
            check(hud && hud->isVisible() && hud->transientParent() == nullptr, "HUD is an independent visible window");
            if (hud) {
                if (qEnvironmentVariableIsSet("CLUMSIER_SCREENSHOTS")) hud->grabWindow().save(qEnvironmentVariable("CLUMSIER_SCREENSHOTS") + "/hud-quick.png");
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
            check(bridge.execute(0), "Start before live switching test");
            const int startsBeforeSwitch = network.starts;
            click("tab-sequences");
            check(network.running && window->property("page") == "sequences" && !window->property("switchingActivity").toBool(), "Switch remains running without a warning");
            check(network.accepted.inbound_ms == 160 && network.starts == startsBeforeSwitch, "Same-traffic switch updates delay without restart");
            click("tab-quick controls");
            check(network.running && network.accepted.inbound_ms == 250, "Quick controls switch stays running with remembered delay");
            check(bridge.switchActivity("sequences") && bridge.execute(0), "Restart selected sequence");
            check(bridge.importPreset(exported) && !network.running, "Selecting imported sequence stops capture");
            check(bridge.profileId() == savedServerId && bridge.state()["canStart"].toBool(), "Imported sequence reuses a matching local server");
            bridge.selectProfile(savedServerId);
            check(bridge.execute(0) && bridge.selectPreset(savedId) && network.running, "Switch between saved sequences keeps capture running");
            const QString copyId = bridge.presets()[0].toMap()["id"].toString() == savedId ? bridge.presets()[1].toMap()["id"].toString() : bridge.presets()[0].toMap()["id"].toString();
            check(bridge.selectPreset(copyId) && bridge.deletePreset() && bridge.selectPreset(savedId), "Remove switching-test copy");
            window->setProperty("page", "settings"); QTest::qWait(200);
            click("advancedModeSetting");
            auto *advanced = find(quickWindow->contentItem(), "advancedModeSetting");
            if (advanced) {
                quickWindow->requestActivate();
                QTest::mouseMove(quickWindow, advanced->mapToScene(QPointF(30, advanced->height()/2)).toPoint());
                QTest::qWait(550);
                auto *hint = window->findChild<QObject *>("hint-advancedModeSetting");
                if (quickWindow->isActive()) check(hint && hint->property("visible").toBool(), "Settings hover shows a themed explanation");
                if (qEnvironmentVariableIsSet("CLUMSIER_SCREENSHOTS")) quickWindow->grabWindow().save(qEnvironmentVariable("CLUMSIER_SCREENSHOTS") + "/setting-hint.png");
                QTest::mouseMove(quickWindow, QPoint(1000, 200)); QTest::qWait(200);
                check(hint && !hint->property("visible").toBool(), "Settings explanation disappears on pointer leave");
            }
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
            window->setProperty("page", "sequences"); QTest::qWait(100);
            click("renameSequenceButton");
            nameField = find(quickWindow->contentItem(), "sequenceNameField");
            check(nameField && nameField->hasActiveFocus(), "Rename button focuses the sequence title");
            QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier, QPoint(1100, 580));
            check(nameField && !nameField->hasActiveFocus() && !bridge.dirty(), "Clicking away from unchanged name ends editing without dirtying draft");
            for (int i = 0; i < 4; ++i) {
                QStringList ids;
                for (const auto &entry : bridge.presets()) ids.append(entry.toMap()["id"].toString());
                check(bridge.batchSequences("duplicate", ids), "Populate a scrollable test library");
            }
            QTest::qWait(150);
            auto *list = find(quickWindow->contentItem(), "sequenceList");
            check(list && list->property("contentHeight").toReal() > list->height(), "Long sequence list has scrollable content");
            if (list) {
                list->setProperty("contentY", 150); QTest::qWait(50);
                check(list->property("contentY").toReal() > 0, "Sequence list scrolls independently");
                list->setProperty("contentY", 0); QTest::qWait(100);
            }
            check(find(quickWindow->contentItem(), "betaBadge") != nullptr, "Beta badge is visible in the main header");
            QTest::keyClick(quickWindow, Qt::Key_F, Qt::ControlModifier); QTest::qWait(100);
            auto *searchField = find(quickWindow->contentItem(), "sequenceSearchField");
            check(searchField && searchField->hasActiveFocus(), "Ctrl+F focuses sequence search");
            if (searchField) {
                for (char key : QByteArray("no-such-sequence")) QTest::keyClick(quickWindow, key); QTest::qWait(100);
                check(list && list->property("count").toInt() == 0, "Sequence search filters unmatched names");
                QTest::keyClick(quickWindow, Qt::Key_A, Qt::ControlModifier); QTest::keyClick(quickWindow, Qt::Key_Backspace); QTest::qWait(100);
                check(list && list->property("count").toInt() == bridge.presets().size(), "Clearing search restores the list");
                QTest::keyClick(quickWindow, Qt::Key_Return);
            }
            click("sequenceSearchButton"); QTest::qWait(100);
            const auto secondSearchId = bridge.presets()[1].toMap()["id"].toString();
            QTest::keyClick(quickWindow, Qt::Key_Tab); QTest::qWait(50);
            auto *secondSearchRow = find(quickWindow->contentItem(), "sequence-" + secondSearchId);
            check(secondSearchRow && secondSearchRow->property("selected").toBool(), "Tab highlights the second search result");
            QTest::keyClick(quickWindow, Qt::Key_Return); QTest::qWait(100);
            check(bridge.selectedId() == secondSearchId && !window->property("searchOpen").toBool()
                && list->property("count").toInt() == bridge.presets().size(), "Enter selects highlighted result and restores full list");
            const auto firstCopy = bridge.presets()[1].toMap()["id"].toString();
            const auto thirdCopy = bridge.presets()[3].toMap()["id"].toString();
            auto rowClick = [&](const QString &id, Qt::MouseButton button, Qt::KeyboardModifiers modifiers) {
                auto *row = find(quickWindow->contentItem(), "sequence-" + id);
                check(row != nullptr, "Sequence row exists for mouse selection");
                if (row) QTest::mouseClick(quickWindow, button, modifiers, row->mapToScene(QPointF(45, row->height()/2)).toPoint());
                QTest::qWait(100);
            };
            rowClick(firstCopy, Qt::LeftButton, Qt::NoModifier);
            rowClick(thirdCopy, Qt::LeftButton, Qt::ShiftModifier);
            const auto selected = window->property("selectedSequenceIds").value<QJSValue>().toVariant().toList();
            check(selected.size() == 3 && bridge.selectedId() == firstCopy, "Shift click selects range without changing active sequence");
            rowClick(thirdCopy, Qt::RightButton, Qt::NoModifier);
            click("context-delete");
            auto *deleteButton = find(quickWindow->contentItem(), "confirmDeleteSequences");
            check(deleteButton && deleteButton->isVisible(), "Context delete asks for batch confirmation");
            if (qEnvironmentVariableIsSet("CLUMSIER_SCREENSHOTS")) quickWindow->grabWindow().save(qEnvironmentVariable("CLUMSIER_SCREENSHOTS") + "/batch-delete.png");
            click("skipDeleteConfirmation"); click("confirmDeleteSequences");
            check(bridge.presets().size() == 13 && !bridge.state()["canStart"].toBool(), "Batch delete removes selected range and clears active sequence");
            QSettings deletionSettings(library.filePath("ui.ini"), QSettings::IniFormat);
            check(!deletionSettings.value("shell/confirmDeletion", true).toBool(), "Deletion opt-out persists");
            QStringList copies;
            for (const auto &entry : bridge.presets()) { const auto id = entry.toMap()["id"].toString(); if (id != savedId) copies.append(id); }
            check(bridge.batchSequences("delete", copies) && bridge.selectPreset(savedId), "Clean up batch test copies");
            QMetaObject::invokeMethod(window, "saveHudPreference", Q_ARG(QVariant, "newSequenceShortcutEnabled"), Q_ARG(QVariant, false));
            QTest::keyClick(quickWindow, Qt::Key_T, Qt::ControlModifier); QTest::qWait(50);
            check(bridge.selectedId() == savedId && !bridge.dirty(), "Disabled Ctrl+T does not create a sequence");
            QMetaObject::invokeMethod(window, "saveHudPreference", Q_ARG(QVariant, "newSequenceShortcutEnabled"), Q_ARG(QVariant, true));
            QTest::keyClick(quickWindow, Qt::Key_T, Qt::ControlModifier); QTest::qWait(50);
            check(bridge.selectedId().isEmpty() && bridge.dirty() && bridge.profileId() == savedServerId, "Ctrl+T creates a sequence with recent server");
            bridge.discard(); bridge.selectPreset(savedId);
            check(find(quickWindow->contentItem(), "feedbackButton") == nullptr, "Discord feedback handoff is removed");
            window->setProperty("page", "hotkeys"); QTest::qWait(100);
            click("hotkeysEnabledToggle");
            auto *masterToggle = find(quickWindow->contentItem(), "hotkeysEnabledToggle");
            check(masterToggle && masterToggle->property("text") == "Disabled" && !masterToggle->property("selected").toBool(), "Master toggle shows Disabled without highlight");
            click("hotkeysEnabledToggle");
            check(masterToggle && masterToggle->property("text") == "Enabled" && masterToggle->property("selected").toBool(), "Master toggle shows Enabled with highlight");
            click("searchShortcutRecorder");
            QTest::keyClick(quickWindow, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier); QTest::qWait(50);
            click("newSequenceShortcutRecorder");
            QTest::keyClick(quickWindow, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier); QTest::qWait(50);
            QSettings localBindings(library.filePath("ui.ini"), QSettings::IniFormat);
            check(localBindings.value("shell/searchShortcut").toString() == "Ctrl+Shift+F" && localBindings.value("shell/newSequenceShortcut").toString() == "Ctrl+Shift+T", "Recorded window shortcuts persist");
            window->setProperty("page", "sequences"); QTest::qWait(100);
            QTest::keyClick(quickWindow, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier); QTest::qWait(100);
            check(searchField && searchField->isVisible() && searchField->hasActiveFocus(), "Recorded search shortcut opens search");
            QTest::keyClick(quickWindow, Qt::Key_Escape); QTest::qWait(100);
            check(searchField && !searchField->isVisible(), "Escape tucks search away");
            QTest::keyClick(quickWindow, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier); QTest::qWait(100);
            check(bridge.selectedId().isEmpty() && bridge.dirty(), "Recorded new-sequence shortcut works");
            bridge.discard(); bridge.selectPreset(savedId);



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
        restartedUi.load(QUrl::fromLocalFile(QStringLiteral(BETA_QML_DIR "/Main.qml")));
        check(!restartedUi.rootObjects().isEmpty(), "Restarted UI loads");
        if (!restartedUi.rootObjects().isEmpty()) {
            auto *window = restartedUi.rootObjects().first();
            check(QMetaObject::invokeMethod(window, "requestSwitch", Q_ARG(QVariant, "page"), Q_ARG(QVariant, "sequences")), "Request switch after restart");
            QTest::qWait(100);
            check(network.running && window->property("page") == "sequences" && !window->property("switchingActivity").toBool(), "Live switching works after restart");
            check(reopened.state()["canStart"].toBool() && reopened.state()["expected"] == 200, "Restored server prepares correct sequence after restart");
            auto *quickWindow = qobject_cast<QQuickWindow *>(window);
            TextFocus textFocus(quickWindow);
            std::function<QQuickItem *(QQuickItem *, const QString &)> find;
            find = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
                if (item->objectName() == name) return item;
                for (auto *child : item->childItems()) if (auto *result = find(child, name)) return result;
                return nullptr;
            };
            auto *traffic = find(quickWindow->contentItem(), "trafficSettingsButton");
            check(traffic && traffic->isVisible(), "Advanced mode survives restart");
            window->setProperty("page", "settings"); QTest::qWait(100);
            {
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
