#include <winsock2.h>
#include "app_bridge.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>
#include <algorithm>
extern "C" {
#include "core/controller.h"
#include "platform/windows/preset_store.h"
#include "platform/windows/hotkeys.h"
}

struct AppBridge::Data {
    AppController controller{};
    PresetStore store{};
    bool opened = false, listening = false, paused = false;
    QVariantList presets, profiles;
    QVariantMap draft, saved;
    QString id, profile, error, recording;
    HotkeySettings keys{};
    wchar_t keyPath[MAX_PATH]{};
    int recordAction = -1;
    QString activity = "quick controls";
    bool ready = true, sequenceStale = false;
    int quickMs = 0, quickDirection = 0;
    QString associationsPath;
};
AppBridge *AppBridge::listenerOwner = nullptr;

static QVariantMap toMap(const Preset &preset) {
    char error[NETWORK_ERROR_SIZE]{};
    char *json = presetSerialize(&preset, error);
    if (!json) return {};
    auto result = QJsonDocument::fromJson(json).object().toVariantMap();
    free(json);
    return result;
}
static bool fromMap(const QVariantMap &map, Preset *preset, char *error) {
    auto json = QJsonDocument::fromVariant(map).toJson(QJsonDocument::Compact);
    return presetParse(json.constData(), size_t(json.size()), preset, error);
}
static std::wstring wide(const QString &text) { return text.toStdWString(); }

AppBridge::AppBridge(NetworkBackend backend, const QString &root, bool enableHotkeys, QObject *parent)
    : QObject(parent), d(std::make_unique<Data>()) {
    controllerInit(&d->controller, backend);
    quickDelay(0, 0);
    char error[NETWORK_ERROR_SIZE]{};
    auto path = wide(root);
    d->opened = presetStoreOpen(&d->store, root.isEmpty() ? nullptr : path.c_str(), error);
    if (!d->opened) fail(QString::fromUtf8(error));
    else {
        d->associationsPath = QString::fromWCharArray(d->store.root) + "/sequence-servers.ini";
        refresh();
    }
    hotkeyDefaults(&d->keys);
    if (enableHotkeys) {
        bool pathReady;
        if (root.isEmpty()) pathReady = hotkeySettingsPath(d->keyPath, error);
        else {
            // A supplied library root also isolates hotkeys for process tests.
            const auto isolatedPath = wide(root + "/hotkeys.ini");
            pathReady = isolatedPath.size() < MAX_PATH;
            if (pathReady) wcscpy(d->keyPath, isolatedPath.c_str());
            else strcpy(error, "The hotkey settings path is too long.");
        }
        if (!pathReady || !hotkeyLoad(d->keyPath, &d->keys, error))
            fail(QString::fromUtf8(error));
        listenerOwner = this;
        d->listening = hotkeysOpen([](AppAction action) { if (listenerOwner) listenerOwner->execute(int(action)); }, error);
        if (!d->listening || !hotkeysApply(&d->keys, nullptr, error)) fail(QString::fromUtf8(error));
    }
    // Read backend state rather than assuming a successful button click means
    // capture is still running. This also surfaces changes from global hotkeys.
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &AppBridge::stateChanged);
    timer->start(200);
}
AppBridge::~AppBridge() {
    if (d->listening) hotkeysClose();
    if (listenerOwner == this) listenerOwner = nullptr;
    controllerShutdown(&d->controller);
}
bool AppBridge::fail(const QString &message) { d->error = message; emit errorChanged(); return false; }
void AppBridge::clearError() { d->error.clear(); emit errorChanged(); }
QVariantList AppBridge::presets() const { return d->presets; }
QVariantList AppBridge::profiles() const { return d->profiles; }
QVariantMap AppBridge::draft() const { return d->draft; }
QString AppBridge::selectedId() const { return d->id; }
bool AppBridge::dirty() const { return d->draft != d->saved; }
QString AppBridge::error() const { return d->error; }
QString AppBridge::profileId() const { return d->profile; }
QString AppBridge::recording() const { return d->recording; }
QVariantMap AppBridge::state() const {
    const auto &app = d->controller;
    QString description = app.preset_loaded ? QString::fromUtf8(app.preset.name)
        : d->activity == "sequences" ? (d->saved.isEmpty() ? "No sequence selected" : d->saved["name"].toString() + " · not ready") : "Delay";
    if (app.preset_loaded) description += " · " + QString::fromUtf8(app.preset.steps[app.active_step].name);
    return {{"running", controllerIsRunning(&app)}, {"sequence", app.preset_loaded},
        {"activity", d->activity}, {"canStart", d->ready && (d->activity != "sequences" || !dirty())},
        {"quickMs", d->quickMs}, {"quickDirection", d->quickDirection},
        {"selectionId", d->id},
        {"sequenceName", app.preset_loaded ? QString::fromUtf8(app.preset.name) : QString()},
        {"stepName", app.preset_loaded ? QString::fromUtf8(app.preset.steps[app.active_step].name) : QString()},
        {"stepCount", app.preset_loaded ? int(app.preset.step_count) : 0},
        {"description", description}, {"step", int(app.active_step)},
        {"expected", int(app.step_result.estimated_ping_ms)}, {"targetPing", app.preset_loaded && app.preset.mode == PRESET_TARGET_PING},
        {"belowBaseline", app.step_result.below_baseline}, {"inbound", int(app.lag.inbound_ms)}, {"outbound", int(app.lag.outbound_ms)}};
}
void AppBridge::refresh() {
    if (!d->opened) return;
    char error[NETWORK_ERROR_SIZE]{};
    for (bool profiles : {false, true}) {
        LibraryEntry entries[LIBRARY_MAX_ITEMS]{};
        size_t count = 0, skipped = 0;
        if (!presetStoreList(&d->store, profiles, entries, &count, &skipped, error)) { fail(QString::fromUtf8(error)); continue; }
        QVariantList list;
        for (size_t i = 0; i < count; ++i) {
            QVariantMap entry{{"id", QString::fromWCharArray(entries[i].id)}, {"name", QString::fromUtf8(entries[i].name)}};
            if (profiles) {
                BaselineProfile profile{};
                if (!profileStoreRead(&d->store, entries[i].id, &profile, error)) continue;
                entry["baseline"] = int(profile.baseline_ms);
            }
            list.append(entry);
        }
        std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
            return QString::localeAwareCompare(a.toMap()["name"].toString(), b.toMap()["name"].toString()) < 0;
        });
        (profiles ? d->profiles : d->presets) = list;
        if (skipped) fail(QString("Skipped %1 unreadable library files; they were left untouched.").arg(skipped));
    }
    emit libraryChanged();
}
bool AppBridge::selectPreset(const QString &id) {
    if (dirty()) return fail("Save or discard your edits before switching sequences.");
    Preset preset{}; char error[NETWORK_ERROR_SIZE]{};
    if (!d->opened || !presetStoreRead(&d->store, wide(id).c_str(), &preset, error)) return fail(QString::fromUtf8(error));
    if (id == d->id && d->activity == "sequences") return true;
    controllerShutdown(&d->controller);
    d->activity = "sequences";
    d->id = id; d->draft = d->saved = toMap(preset);
    restoreServerAssociation();
    prepareSequence(); emit draftChanged(); return true;
}
bool AppBridge::newPreset() {
    if (dirty()) return fail("Save or discard your edits before creating a sequence.");
    Preset preset; presetDefault(&preset);
    controllerShutdown(&d->controller); d->activity = "sequences";
    d->profile.clear();
    d->id.clear(); d->saved.clear(); d->draft = toMap(preset); prepareSequence(); emit draftChanged(); return true;
}
void AppBridge::updateDraft(const QVariantMap &value) { d->draft = value; emit draftChanged(); emit stateChanged(); }
bool AppBridge::save() {
    Preset preset{}; char error[NETWORK_ERROR_SIZE]{}; wchar_t id[LIBRARY_ID_SIZE]{};
    if (!d->opened) return fail("The library is unavailable.");
    if (!fromMap(d->draft, &preset, error)) return fail(QString::fromUtf8(error));
    if (!d->id.isEmpty()) {
        Preset current{};
        if (!presetStoreRead(&d->store, wide(d->id).c_str(), &current, error)) return fail(QString::fromUtf8(error));
        if (toMap(current) != d->saved) return fail("This file changed outside this window. Discard and reopen it before saving.");
        d->id.toWCharArray(id);
    }
    if (!presetStoreSave(&d->store, id, &preset, error)) return fail(QString::fromUtf8(error));
    d->id = QString::fromWCharArray(id); d->saved = d->draft = toMap(preset);
    const bool associated = storeServerAssociation();
    clearError(); refresh();
    if (d->activity == "sequences") {
        if (controllerIsRunning(&d->controller)) d->sequenceStale = true;
        else prepareSequence();
    }
    emit draftChanged();
    if (!associated) return fail("The sequence was saved, but its server association could not be saved.");
    return true;
}
void AppBridge::discard() {
    d->draft = d->saved; clearError();
    if (d->activity == "sequences" && !controllerIsRunning(&d->controller)) prepareSequence();
    emit draftChanged(); emit stateChanged();
}
bool AppBridge::duplicate() {
    if (dirty()) return fail("Save or discard your edits before duplicating.");
    if (d->draft.isEmpty()) return fail("Select a sequence first.");
    controllerShutdown(&d->controller); d->activity = "sequences";
    d->draft["name"] = d->draft["name"].toString() + " copy";
    d->id.clear(); d->saved.clear(); prepareSequence(); emit draftChanged(); return true;
}
bool AppBridge::deletePreset() {
    if (dirty()) return fail("Save or discard your edits before deleting.");
    char error[NETWORK_ERROR_SIZE]{};
    if (!d->opened || !presetStoreDelete(&d->store, false, wide(d->id).c_str(), error)) return fail(QString::fromUtf8(error));
    if (d->activity == "sequences") controllerShutdown(&d->controller);
    d->id.clear(); d->saved.clear(); d->draft.clear();
    if (d->activity == "sequences") prepareSequence();
    refresh(); emit draftChanged(); return true;
}
bool AppBridge::importPreset(const QUrl &file) {
    if (dirty()) return fail("Save or discard your edits before importing.");
    if (!file.isLocalFile()) return fail("Choose a local JSON file.");
    Preset preset{}; char error[NETWORK_ERROR_SIZE]{};
    if (!presetFileRead(wide(file.toLocalFile()).c_str(), &preset, error)) return fail(QString::fromUtf8(error));
    controllerShutdown(&d->controller); d->activity = "sequences";
    d->id.clear(); d->saved.clear(); d->draft = toMap(preset); emit draftChanged();
    d->profile.clear();
    prepareSequence();
    return save(); // Import always creates a new ID, never overwrites a same-name preset.
}
bool AppBridge::exportPreset(const QUrl &file) {
    if (dirty()) return fail("Save your edits before exporting.");
    if (!file.isLocalFile()) return fail("Choose a local JSON file.");
    Preset preset{}; char error[NETWORK_ERROR_SIZE]{};
    if (!fromMap(d->draft, &preset, error) || !presetFileWrite(wide(file.toLocalFile()).c_str(), &preset, error)) return fail(QString::fromUtf8(error));
    return true;
}
bool AppBridge::saveProfile(const QString &id, const QString &name, int baseline) {
    auto utf8 = name.trimmed().toUtf8();
    if (utf8.isEmpty() || utf8.size() >= PRESET_NAME_SIZE || utf8.contains('\0') || baseline < 0 || baseline > PING_MAX_MS)
        return fail("Enter a server name and a baseline between 0 and 60000 ms.");
    if (!d->opened) return fail("The library is unavailable.");
    wchar_t storedId[LIBRARY_ID_SIZE]{};
    if (id.size() >= LIBRARY_ID_SIZE) return fail("Invalid server ID.");
    id.toWCharArray(storedId);
    BaselineProfile profile{}; memcpy(profile.name, utf8.constData(), utf8.size()); profile.baseline_ms = uint32_t(baseline);
    char error[NETWORK_ERROR_SIZE]{};
    if (!profileStoreSave(&d->store, storedId, &profile, error)) return fail(QString::fromUtf8(error));
    const bool selected = selectProfile(QString::fromWCharArray(storedId));
    refresh(); return selected;
}
bool AppBridge::deleteProfile(const QString &id) {
    char error[NETWORK_ERROR_SIZE]{};
    if (!d->opened || !presetStoreDelete(&d->store, true, wide(id).c_str(), error)) return fail(QString::fromUtf8(error));
    if (d->profile == id) selectProfile(""); refresh(); emit stateChanged(); return true;
}
bool AppBridge::selectProfile(const QString &id) {
    const QString previous = d->profile;
    d->profile = id;
    if (!storeServerAssociation()) { d->profile = previous; emit stateChanged(); return false; }
    if (d->activity == "sequences") {
        if (controllerIsRunning(&d->controller)) d->sequenceStale = true;
        else prepareSequence();
    }
    emit stateChanged();
    return true;
}
bool AppBridge::storeServerAssociation() {
    // Baselines belong to this player's server profiles, not shared preset JSON.
    // A new/duplicated draft gets its association when it receives a saved ID.
    if (d->id.isEmpty()) return true;
    QSettings settings(d->associationsPath, QSettings::IniFormat);
    settings.setValue("servers/" + d->id, d->profile);
    settings.sync();
    if (settings.status() != QSettings::NoError) return fail("Could not save this sequence's server association.");
    return true;
}
void AppBridge::restoreServerAssociation() {
    QSettings settings(d->associationsPath, QSettings::IniFormat);
    d->profile = settings.value("servers/" + d->id).toString();
    // Missing/deleted servers require a new selection; never fall back to the
    // server from a different sequence.
    bool found = false;
    for (const auto &profile : d->profiles) if (profile.toMap()["id"] == d->profile) found = true;
    if (!found) d->profile.clear();
}
void AppBridge::prepareSequence() {
    // A selected but incomplete sequence must never leave the previous quick
    // delay armed. Both the button and global Start action use this readiness.
    controllerUnloadPreset(&d->controller);
    d->ready = false; d->sequenceStale = false;
    clearError();
    if (!d->saved.isEmpty()) loadSequence();
    emit stateChanged();
}
bool AppBridge::switchActivity(const QString &activity) {
    if (activity != "sequences" && activity != "quick controls") return fail("Unknown activity.");
    if (activity == d->activity) return true;
    controllerShutdown(&d->controller);
    d->activity = activity;
    if (activity == "sequences") prepareSequence();
    else return quickDelay(d->quickMs, d->quickDirection);
    return true;
}
bool AppBridge::loadSequence() {
    if (dirty()) return fail("Save or discard your edits before loading the sequence.");
    Preset preset{}; char error[NETWORK_ERROR_SIZE]{};
    if (!fromMap(d->saved, &preset, error)) return fail(QString::fromUtf8(error));
    bool hasBaseline = false; uint32_t baseline = 0;
    if (!d->profile.isEmpty()) {
        BaselineProfile profile{};
        if (!profileStoreRead(&d->store, wide(d->profile).c_str(), &profile, error)) return fail(QString::fromUtf8(error));
        hasBaseline = true; baseline = profile.baseline_ms;
    }
    if (!controllerLoadPreset(&d->controller, &preset, hasBaseline, baseline, error)) return fail(QString::fromUtf8(error));
    d->activity = "sequences"; d->ready = true;
    clearError(); emit stateChanged(); return true;
}
bool AppBridge::quickDelay(int milliseconds, int direction) {
    if (milliseconds < 0 || milliseconds > LAG_MAX_MS || direction < 0 || direction > 2) return fail("Delay must be between 0 and 15000 ms.");
    // Use the same all-step validation and transactional apply as sequences.
    Preset preset; presetDefault(&preset); preset.mode = PRESET_ADDED_DELAY;
    preset.policy = DelayPolicy(direction); preset.target.traffic.direction = direction == 0 ? TRAFFIC_INBOUND : direction == 1 ? TRAFFIC_OUTBOUND : TRAFFIC_BOTH;
    preset.steps[0].kind = STEP_DELAY; preset.steps[0].target_ms = 0;
    preset.steps[0].inbound_ms = direction != 1 ? milliseconds : 0;
    preset.steps[0].outbound_ms = direction != 0 ? milliseconds : 0;
    char error[NETWORK_ERROR_SIZE]{};
    if (!controllerLoadPreset(&d->controller, &preset, false, 0, error)) return fail(QString::fromUtf8(error));
    d->activity = "quick controls"; d->ready = true;
    d->quickMs = milliseconds; d->quickDirection = direction;
    controllerUnloadPreset(&d->controller); clearError(); emit stateChanged(); return true;
}
bool AppBridge::execute(int action) {
    char error[NETWORK_ERROR_SIZE]{};
    // Footer controls and global shortcuts must obey the same draft boundary.
    // Advancing the saved snapshot while editing a different list is misleading.
    if (action >= ACTION_NEXT_STEP && action <= ACTION_RESET_SEQUENCE && (dirty() || d->sequenceStale))
        return fail("Save or discard your edits before using playback controls.");
    const bool wasRunning = controllerIsRunning(&d->controller);
    const bool starting = !wasRunning && (action == ACTION_START_CAPTURE || action == ACTION_TOGGLE_CAPTURE);
    if (starting && !state()["canStart"].toBool())
        return fail(d->saved.isEmpty() ? "No sequence selected. Choose or save a sequence first." : "The selected sequence is not ready. Check your saved edits and server baseline.");
    if (!controllerExecute(&d->controller, AppAction(action), error)) return fail(QString::fromUtf8(error));
    if (wasRunning && !controllerIsRunning(&d->controller) && d->activity == "sequences" && d->sequenceStale) {
        prepareSequence(); return true;
    }
    clearError(); emit stateChanged(); return true;
}
bool AppBridge::selectStep(int index) {
    if (index < 0 || dirty() || d->sequenceStale)
        return fail("Save or discard edits, then stop playback before selecting an edited step.");
    char error[NETWORK_ERROR_SIZE]{};
    if (!controllerSelectStep(&d->controller, size_t(index), error)) return fail(QString::fromUtf8(error));
    clearError(); emit stateChanged(); return true;
}
QVariantList AppBridge::bindings() const {
    QVariantList result;
    for (int i = 0; i < ACTION_COUNT; ++i) {
        char text[HOTKEY_TEXT_SIZE]{}; hotkeyFormat(d->keys.bindings[i], text);
        result.append(QVariantMap{{"action", i}, {"name", QString::fromUtf8(actionName(AppAction(i)))}, {"text", QString::fromUtf8(text)}});
    }
    return result;
}
void AppBridge::setInputPaused(bool paused) {
    if (!d->listening || d->paused == paused) return;
    d->paused = paused;
    if (paused) hotkeysPause(); else hotkeysResume();
}
void AppBridge::recordBinding(int action) {
    if (!d->listening || action < 0 || action >= ACTION_COUNT) { fail("Global hotkeys are unavailable."); return; }
    d->recordAction = action; d->recording = "Press your keys or mouse buttons, then release them."; emit bindingsChanged();
    hotkeysRecordBegin([](const HotkeyBinding *binding, BOOL finished) {
        auto *owner = listenerOwner;
        if (!owner) return;
        char text[HOTKEY_TEXT_SIZE]{}; hotkeyFormat(*binding, text);
        owner->d->recording = QString::fromUtf8(text); emit owner->bindingsChanged();
        if (finished) {
            int action = owner->d->recordAction;
            owner->d->recordAction = -1; owner->d->recording.clear();
            owner->saveBinding(action, QString::fromUtf8(text));
            emit owner->bindingsChanged();
        }
    });
}
void AppBridge::cancelRecording() {
    if (d->listening) hotkeysRecordCancel();
    d->recordAction = -1; d->recording.clear(); emit bindingsChanged();
}
bool AppBridge::saveBinding(int action, const QString &text) {
    if (action < 0 || action >= ACTION_COUNT) return fail("Unknown action.");
    HotkeySettings next = d->keys; char error[HOTKEY_ERROR_SIZE]{};
    if (!hotkeyParse(text.toUtf8().constData(), &next.bindings[action], error) || !hotkeyValidate(&next, error)) return fail(QString::fromUtf8(error));
    if (d->listening && !hotkeysApply(&next, d->keyPath, error)) return fail(QString::fromUtf8(error));
    d->keys = next; clearError(); emit bindingsChanged(); return true;
}
