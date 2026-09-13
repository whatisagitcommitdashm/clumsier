#ifdef _WIN32
#include <winsock2.h>
#endif
#include "app_bridge.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>
#include <QDir>
#include <QUuid>
#include <QRegularExpression>
#include <QKeySequence>
#include <QFile>
#include <algorithm>
extern "C" {
#include "core/controller.h"
#ifdef _WIN32
#include "platform/windows/preset_store.h"
#include "platform/windows/hotkeys.h"
#else
#include "platform/linux/preset_store.h"
#endif
}

struct AppBridge::Data {
    AppController controller{};
    PresetStore store{};
    bool opened = false, listening = false, paused = false;
    QVariantList presets, profiles;
    QVariantMap draft, saved;
    QString id, profile, error, recording, lastProfile;
    bool bindingEnabled[ACTION_COUNT] = {true, true, true, true, true, true};
#ifdef Q_OS_WIN
    HotkeySettings keys{};
    wchar_t keyPath[MAX_PATH]{};
#endif
    int recordAction = -1;
    QString activity = "quick controls";
    bool ready = true, sequenceStale = false;
    int quickMs = 0, quickDirection = 0;
    QString associationsPath;
    QString preferencesPath;
    bool hotkeysEnabled = true, autoSave = false, inputSuspended = false;
    QTimer *saveTimer = nullptr;
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
        d->preferencesPath = QString::fromWCharArray(d->store.root) + "/preferences.ini";
        QSettings preferences(d->preferencesPath, QSettings::IniFormat);
        d->hotkeysEnabled = preferences.value("hotkeysEnabled", true).toBool();
        d->autoSave = preferences.value("autoSave", false).toBool();
        d->lastProfile = preferences.value("lastServer").toString();
        for (int i = 0; i < ACTION_COUNT; ++i) d->bindingEnabled[i] = preferences.value(QString("bindingEnabled/%1").arg(i), true).toBool();
        refresh();
    }
#ifdef Q_OS_WIN
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
        d->listening = hotkeysOpen([](AppAction action) { if (listenerOwner) listenerOwner->executeHotkey(int(action)); }, error);
        if (!d->listening || !hotkeysApply(&d->keys, nullptr, error)) fail(QString::fromUtf8(error));
        updateHotkeyPause();
    }
#else
    Q_UNUSED(enableHotkeys);
#endif
    d->saveTimer = new QTimer(this);
    d->saveTimer->setSingleShot(true);
    d->saveTimer->setInterval(500);
    connect(d->saveTimer, &QTimer::timeout, this, [this] { if (d->autoSave && dirty()) save(); });
    // Read backend state rather than assuming a successful button click means
    // capture is still running. This also surfaces changes from global hotkeys.
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &AppBridge::stateChanged);
    timer->start(200);
}
AppBridge::~AppBridge() {
#ifdef Q_OS_WIN
    if (d->listening) hotkeysClose();
#endif
    if (listenerOwner == this) listenerOwner = nullptr;
    controllerShutdown(&d->controller);
}
bool AppBridge::fail(const QString &message) { d->error = message; emit errorChanged(); return false; }
void AppBridge::clearError() { d->error.clear(); emit errorChanged(); }
QVariantList AppBridge::presets() const { return d->presets; }
QVariantList AppBridge::profiles() const { return d->profiles; }
QString AppBridge::matchingProfile(const QString &name) const {
    for (const auto &entry : d->profiles) {
        const auto profile = entry.toMap();
        if (profile["name"].toString().trimmed().compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return profile["id"].toString();
    }
    return {};
}
QString AppBridge::serverName(const QString &profileId) const {
    for (const auto &entry : d->profiles)
        if (entry.toMap()["id"].toString() == profileId) return entry.toMap()["name"].toString();
    return {};
}
QStringList AppBridge::missingServers() const {
    if (!d->opened) return {};
    QStringList names;
    QSettings associations(d->associationsPath, QSettings::IniFormat);
    for (const auto &entry : d->presets) {
        const auto preset = entry.toMap();
        const QString name = preset["server"].toString().trimmed();
        const QString linked = associations.value("servers/" + preset["id"].toString()).toString();
        if (!name.isEmpty() && serverName(linked).isEmpty() && matchingProfile(name).isEmpty()
            && !names.contains(name, Qt::CaseInsensitive)) names.append(name);
    }
    return names;
}
bool AppBridge::resolveServer(const QString &name, int baseline) {
    if (!missingServers().contains(name, Qt::CaseInsensitive)) return fail("This server no longer needs a baseline.");
    if (!saveProfile("", name, baseline, false)) return false;
    QSettings associations(d->associationsPath, QSettings::IniFormat);
    const QString profile = matchingProfile(name);
    for (const auto &entry : d->presets) {
        const auto preset = entry.toMap();
        const QString key = "servers/" + preset["id"].toString();
        if (preset["server"].toString().trimmed().compare(name.trimmed(), Qt::CaseInsensitive) == 0
            && serverName(associations.value(key).toString()).isEmpty()) associations.setValue(key, profile);
    }
    associations.sync();
    if (associations.status() != QSettings::NoError) return fail("Could not save the server associations.");
    d->lastProfile = profile;
    QSettings prefs(d->preferencesPath, QSettings::IniFormat); prefs.setValue("lastServer", profile);
    if (!d->id.isEmpty()) {
        const QString previous = d->profile;
        restoreServerAssociation();
        if (previous != d->profile && d->activity == "sequences") prepareSequence();
    }
    clearError(); emit libraryChanged(); emit stateChanged(); return true;
}

bool AppBridge::installStarterPresets() {
    Q_INIT_RESOURCE(starter_presets);
    if (!d->opened) return false;
    QSettings preferences(d->preferencesPath, QSettings::IniFormat);
    if (preferences.value("starterLibraryInstalled", false).toBool()) return true;
    // Existing libraries belong to their users. Only seed an empty library;
    // record progress so an interrupted first launch can resume safely.
    if (!d->presets.isEmpty() && !preferences.value("starterLibraryStarted", false).toBool()) {
        preferences.setValue("starterLibraryInstalled", true); preferences.sync();
        return preferences.status() == QSettings::NoError;
    }
    preferences.setValue("starterLibraryStarted", true); preferences.sync();
    if (preferences.status() != QSettings::NoError) return fail("Could not initialize the starter library.");
    const QStringList files{"good-basic", "pirate-bay", "skylands"};
    for (int i = 0; i < files.size(); ++i) {
        const QString id = QString("B37A0000-0000-4000-8000-%1").arg(i + 1, 12, 10, QChar('0'));
        Preset preset{}; char error[NETWORK_ERROR_SIZE]{};
        if (presetStoreRead(&d->store, wide(id).c_str(), &preset, error)) continue;
        QFile file(":/starter-presets/" + files[i] + ".json");
        if (!file.open(QIODevice::ReadOnly)) return fail("A bundled sequence could not be opened.");
        const auto json = file.readAll();
        if (!presetParse(json.constData(), size_t(json.size()), &preset, error)) return fail(QString::fromUtf8(error));
        wchar_t storedId[LIBRARY_ID_SIZE]{}; id.toWCharArray(storedId);
        if (!presetStoreSave(&d->store, storedId, &preset, error)) return fail(QString::fromUtf8(error));
    }
    preferences.setValue("starterLibraryInstalled", true); preferences.sync();
    refresh();
    if (preferences.status() != QSettings::NoError) return fail("Could not finish initializing the starter library.");
    return true;
}
QVariantMap AppBridge::draft() const { return d->draft; }
QString AppBridge::selectedId() const { return d->id; }
bool AppBridge::dirty() const { return d->draft != d->saved; }
QString AppBridge::error() const { return d->error; }
QString AppBridge::profileId() const { return d->profile; }
QString AppBridge::recording() const { return d->recording; }
bool AppBridge::globalHotkeysAvailable() const {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}
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
            } else {
                Preset preset{};
                if (!presetStoreRead(&d->store, entries[i].id, &preset, error)) continue;
                entry["server"] = QString::fromUtf8(preset.server);
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
    const QString previousProfile = d->profile, previousId = d->id;
    d->id = id; restoreServerAssociation();
    const QString profile = d->profile;
    d->id = previousId; d->profile = previousProfile;
    const bool running = controllerIsRunning(&d->controller);
    if (running && !applySequence(toMap(preset), profile)) return false;
    d->activity = "sequences"; d->ready = true;
    d->id = id; d->profile = profile; d->draft = d->saved = toMap(preset);
    if (!profile.isEmpty()) { d->lastProfile = profile; QSettings prefs(d->preferencesPath, QSettings::IniFormat); prefs.setValue("lastServer", profile); }
    if (!running) prepareSequence();
    emit draftChanged(); emit stateChanged(); return true;
}
bool AppBridge::newPreset() {
    if (dirty()) return fail("Save or discard your edits before creating a sequence.");
    Preset preset; presetDefault(&preset);
    controllerShutdown(&d->controller); d->activity = "sequences";
    d->profile.clear();
    for (const auto &server : d->profiles)
        if (server.toMap()["id"].toString() == d->lastProfile) d->profile = d->lastProfile;
    d->id.clear(); d->saved.clear(); d->draft = toMap(preset); prepareSequence(); emit draftChanged(); return true;
}
bool AppBridge::applySequence(const QVariantMap &value, const QString &profileId, size_t step) {
    Preset preset{}; char error[NETWORK_ERROR_SIZE]{};
    if (!fromMap(value, &preset, error)) return fail(QString::fromUtf8(error));
    BaselineProfile profile{};
    if (!profileId.isEmpty() && !profileStoreRead(&d->store, wide(profileId).c_str(), &profile, error)) return fail(QString::fromUtf8(error));
    step = std::min(step, preset.step_count - 1);
    if (!controllerReplacePreset(&d->controller, &preset, !profileId.isEmpty(), profile.baseline_ms, step, error)) return fail(QString::fromUtf8(error));
    d->ready = true; d->sequenceStale = false; clearError(); emit stateChanged(); return true;
}
bool AppBridge::commitDraft(const QVariantMap &value, int activeStep) {
    if (value == d->draft && activeStep < 0) return true;
    Preset checked{}; char error[NETWORK_ERROR_SIZE]{};
    if (!fromMap(value, &checked, error)) return fail(QString::fromUtf8(error));
    if (d->activity == "sequences" && controllerIsRunning(&d->controller)
        && !applySequence(value, d->profile, activeStep < 0 ? d->controller.active_step : size_t(activeStep))) return false;
    clearError(); updateDraft(value); return true;
}
void AppBridge::updateDraft(const QVariantMap &value) {
    d->draft = value; emit draftChanged(); emit stateChanged();
    // Wait for a pause in typing. Invalid drafts remain visible and never
    // replace the last saved version; Save reports the validation error.
    if (d->autoSave) d->saveTimer->start();
}
bool AppBridge::save() {
    if (d->saveTimer) d->saveTimer->stop();
    Preset preset{}; char error[NETWORK_ERROR_SIZE]{}; wchar_t id[LIBRARY_ID_SIZE]{};
    if (!d->opened) return fail("The library is unavailable.");
    if (!fromMap(d->draft, &preset, error)) return fail(QString::fromUtf8(error));
    if (!d->id.isEmpty()) {
        Preset current{};
        if (!presetStoreRead(&d->store, wide(d->id).c_str(), &current, error)) return fail(QString::fromUtf8(error));
        if (toMap(current) != d->saved) return fail("This file changed outside this window. Discard and reopen it before saving.");
        d->id.toWCharArray(id);
    }
    if (d->activity == "sequences" && controllerIsRunning(&d->controller)
        && !applySequence(d->draft, d->profile, d->controller.active_step)) return false;
    if (!presetStoreSave(&d->store, id, &preset, error)) return fail(QString::fromUtf8(error));
    d->id = QString::fromWCharArray(id); d->saved = d->draft = toMap(preset);
    const bool associated = storeServerAssociation();
    clearError(); refresh();
    if (d->activity == "sequences") {
        if (!controllerIsRunning(&d->controller)) prepareSequence();
    }
    emit draftChanged();
    if (!associated) return fail("The sequence was saved, but its server association could not be saved.");
    return true;
}
void AppBridge::discard() {
    if (controllerIsRunning(&d->controller) && d->activity == "sequences" && !applySequence(d->saved, d->profile, d->controller.active_step)) return;
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
    d->profile = matchingProfile(QString::fromUtf8(preset.server));
    prepareSequence();
    return save(); // Import always creates a new ID, never overwrites a same-name preset.
}
bool AppBridge::exportPreset(const QUrl &file) {
    if (dirty()) return fail("Save your edits before exporting.");
    if (!file.isLocalFile()) return fail("Choose a local JSON file.");
    Preset preset{}; char error[NETWORK_ERROR_SIZE]{};
    auto shared = d->draft;
    if (!serverName(d->profile).isEmpty()) shared["server"] = serverName(d->profile);
    if (!fromMap(shared, &preset, error) || !presetFileWrite(wide(file.toLocalFile()).c_str(), &preset, error)) return fail(QString::fromUtf8(error));
    return true;
}
bool AppBridge::saveProfile(const QString &id, const QString &name, int baseline, bool select) {
    auto utf8 = name.trimmed().toUtf8();
    if (utf8.isEmpty() || utf8.size() >= PRESET_NAME_SIZE || utf8.contains('\0') || baseline < 0 || baseline > PING_MAX_MS)
        return fail("Enter a server name and a baseline between 0 and 60000 ms.");
    if (!d->opened) return fail("The library is unavailable.");
    wchar_t storedId[LIBRARY_ID_SIZE]{};
    if (id.size() >= LIBRARY_ID_SIZE) return fail("Invalid server ID.");
    id.toWCharArray(storedId);
    BaselineProfile profile{}; memcpy(profile.name, utf8.constData(), utf8.size()); profile.baseline_ms = uint32_t(baseline);
    char error[NETWORK_ERROR_SIZE]{};
    const bool liveEdit = !id.isEmpty() && id == d->profile && d->activity == "sequences" && controllerIsRunning(&d->controller);
    const AppController previous = d->controller;
    if (liveEdit && !controllerReplacePreset(&d->controller, &previous.preset, true,
        profile.baseline_ms, previous.active_step, error)) return fail(QString::fromUtf8(error));
    if (!profileStoreSave(&d->store, storedId, &profile, error)) {
        if (liveEdit) {
            char rollback[NETWORK_ERROR_SIZE]{};
            if (!controllerReplacePreset(&d->controller, &previous.preset, previous.has_baseline,
                previous.baseline_ms, previous.active_step, rollback)) {
                controllerShutdown(&d->controller);
                return fail("The server could not be saved or restored. Capture is stopped.");
            }
        }
        return fail(QString::fromUtf8(error));
    }
    const bool selected = liveEdit || !select || selectProfile(QString::fromWCharArray(storedId));
    if (liveEdit) { clearError(); emit stateChanged(); }
    refresh(); return selected;
}
bool AppBridge::batchSequences(const QString &operation, const QStringList &requested, const QUrl &folder) {
    if (dirty()) return fail("Save or discard your edits before changing the selection.");
    if (!d->opened) return fail("The library is unavailable.");
    if (operation != "delete" && operation != "duplicate" && operation != "export") return fail("Unknown sequence action.");
    QStringList ids = requested; ids.removeDuplicates();
    if (ids.isEmpty()) return fail("Select at least one sequence.");
    if (operation == "export" && (!folder.isLocalFile() || !QDir(folder.toLocalFile()).exists()))
        return fail("Choose an existing export folder.");
    // Validate the entire selection before changing files. Store reads validate
    // IDs as well, so a context-menu argument cannot escape the library.
    QList<Preset> presets;
    char error[NETWORK_ERROR_SIZE]{};
    for (const auto &id : ids) {
        Preset preset{};
        if (!presetStoreRead(&d->store, wide(id).c_str(), &preset, error)) return fail(QString::fromUtf8(error));
        presets.append(preset);
    }
    if (operation == "duplicate" && d->presets.size() + ids.size() > LIBRARY_MAX_ITEMS)
        return fail("There is not enough room in the sequence library for these copies.");
    QSettings associations(d->associationsPath, QSettings::IniFormat);
    int completed = 0;
    for (int i = 0; i < ids.size(); ++i) {
        bool success = false;
        if (operation == "delete") {
            success = presetStoreDelete(&d->store, false, wide(ids[i]).c_str(), error);
            if (success) {
                associations.remove("servers/" + ids[i]);
                if (ids[i] == d->id) {
                    if (d->activity == "sequences") controllerShutdown(&d->controller);
                    d->id.clear(); d->saved.clear(); d->draft.clear(); d->profile.clear();
                    if (d->activity == "sequences") prepareSequence();
                }
            }
        } else if (operation == "duplicate") {
            auto copy = presets[i];
            QString name = QString::fromUtf8(copy.name);
            while ((name + " copy").toUtf8().size() >= PRESET_NAME_SIZE) name.chop(1);
            const auto utf8 = (name + " copy").toUtf8();
            memset(copy.name, 0, sizeof(copy.name)); memcpy(copy.name, utf8.constData(), utf8.size());
            wchar_t newId[LIBRARY_ID_SIZE]{};
            success = presetStoreSave(&d->store, newId, &copy, error);
            if (success) associations.setValue("servers/" + QString::fromWCharArray(newId), associations.value("servers/" + ids[i]));
        } else {
            const QString linkedName = serverName(associations.value("servers/" + ids[i]).toString());
            if (!linkedName.isEmpty()) {
                const auto bytes = linkedName.toUtf8();
                memset(presets[i].server, 0, sizeof(presets[i].server));
                memcpy(presets[i].server, bytes.constData(), size_t(bytes.size()));
            }
            QString name = QString::fromUtf8(presets[i].name);
            name.replace(QRegularExpression("[^a-zA-Z0-9_-]+"), "-");
            // Unique suffixes keep repeated exports from overwriting files.
            name = name.left(60) + "-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json";
            success = presetFileWrite(wide(QDir(folder.toLocalFile()).filePath(name)).c_str(), &presets[i], error);
        }
        if (!success) {
            associations.sync(); refresh(); emit draftChanged(); emit stateChanged();
            return fail(QString("Completed %1 of %2 sequences. %3").arg(completed).arg(ids.size()).arg(QString::fromUtf8(error)));
        }
        ++completed;
    }
    associations.sync(); refresh(); emit draftChanged(); emit stateChanged();
    if (associations.status() != QSettings::NoError) return fail("Sequence files were updated, but server associations could not be saved.");
    clearError(); return true;
}
bool AppBridge::deleteProfile(const QString &id) {
    char error[NETWORK_ERROR_SIZE]{};
    if (!d->opened || !presetStoreDelete(&d->store, true, wide(id).c_str(), error)) return fail(QString::fromUtf8(error));
    if (d->profile == id) {
        // A deleted baseline cannot supply a target-ping sequence. Stop that
        // activity rather than displaying a server that no longer exists.
        if (d->activity == "sequences") controllerShutdown(&d->controller);
        selectProfile("");
    }
    refresh(); emit stateChanged(); return true;
}
bool AppBridge::selectProfile(const QString &id) {
    if (!id.isEmpty()) {
        BaselineProfile profile{}; char error[NETWORK_ERROR_SIZE]{};
        if (!profileStoreRead(&d->store, wide(id).c_str(), &profile, error)) return fail(QString::fromUtf8(error));
    }
    const QString previous = d->profile;
    d->profile = id;
    if (!storeServerAssociation()) { d->profile = previous; emit stateChanged(); return false; }
    if (d->activity == "sequences" && controllerIsRunning(&d->controller)
        && !applySequence(d->draft, id, d->controller.active_step)) {
        const QString rejection = d->error;
        d->profile = previous;
        if (!storeServerAssociation()) return false;
        emit stateChanged(); return fail(rejection);
    }
    if (!id.isEmpty()) { d->lastProfile = id; QSettings prefs(d->preferencesPath, QSettings::IniFormat); prefs.setValue("lastServer", id); }
    if (d->activity == "sequences" && !controllerIsRunning(&d->controller)) prepareSequence();
    emit stateChanged(); return true;
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
    if (!found) {
        d->profile.clear();
        for (const auto &entry : d->presets)
            if (entry.toMap()["id"].toString() == d->id) d->profile = matchingProfile(entry.toMap()["server"].toString());
    }
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
    if (activity == "quick controls") return quickDelay(d->quickMs, d->quickDirection);
    if (d->saved.isEmpty()) {
        controllerShutdown(&d->controller); d->activity = activity; prepareSequence();
    } else {
        if (!controllerIsRunning(&d->controller)) { d->activity = activity; prepareSequence(); return true; }
        if (!applySequence(d->saved, d->profile)) return false;
        d->activity = activity; emit stateChanged();
    }
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
    if (!controllerReplacePreset(&d->controller, &preset, false, 0, 0, error)) return fail(QString::fromUtf8(error));
    d->activity = "quick controls"; d->ready = true;
    d->quickMs = milliseconds; d->quickDirection = direction;
    controllerUnloadPreset(&d->controller); clearError(); emit stateChanged(); return true;
}
bool AppBridge::execute(int action) {
    char error[NETWORK_ERROR_SIZE]{};
    // Footer controls and global shortcuts must obey the same draft boundary.
    // Advancing the saved snapshot while editing a different list is misleading.
    if (action >= ACTION_NEXT_STEP && action <= ACTION_RESET_SEQUENCE && d->sequenceStale)
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
    if (index < 0 || d->sequenceStale)
        return fail("Save or discard edits, then stop playback before selecting an edited step.");
    char error[NETWORK_ERROR_SIZE]{};
    if (!controllerSelectStep(&d->controller, size_t(index), error)) return fail(QString::fromUtf8(error));
    clearError(); emit stateChanged(); return true;
}
QVariantList AppBridge::bindings() const {
    QVariantList result;
#ifdef Q_OS_WIN
    for (int i = 0; i < ACTION_COUNT; ++i) {
        char text[HOTKEY_TEXT_SIZE]{}; hotkeyFormat(d->keys.bindings[i], text);
        result.append(QVariantMap{{"action", i}, {"name", QString::fromUtf8(actionName(AppAction(i)))}, {"text", QString::fromUtf8(text)}, {"enabled", d->bindingEnabled[i]}});
    }
#endif
    return result;
}
void AppBridge::setInputPaused(bool paused) {
    if (d->paused == paused) return;
    d->paused = paused;
    updateHotkeyPause();
}
void AppBridge::updateHotkeyPause() {
    const bool suspend = d->paused || !d->hotkeysEnabled;
    if (!d->listening || suspend == d->inputSuspended) return;
    // The native listener counts nested pauses. Own exactly one pause here,
    // regardless of how many UI reasons currently need shortcuts suspended.
#ifdef Q_OS_WIN
    if (suspend) hotkeysPause(); else hotkeysResume();
#endif
    d->inputSuspended = suspend;
}
bool AppBridge::hotkeysEnabled() const { return d->hotkeysEnabled; }
bool AppBridge::autoSave() const { return d->autoSave; }
bool AppBridge::savePreference(const QString &key, bool value) {
    if (!d->opened) return fail("The settings folder is unavailable.");
    QSettings preferences(d->preferencesPath, QSettings::IniFormat);
    preferences.setValue(key, value); preferences.sync();
    return preferences.status() == QSettings::NoError || fail("Could not save your preference.");
}
void AppBridge::setHotkeysEnabled(bool enabled) {
    if (d->hotkeysEnabled == enabled || !savePreference("hotkeysEnabled", enabled)) return;
    d->hotkeysEnabled = enabled;
    updateHotkeyPause();
    emit preferencesChanged();
}
void AppBridge::setAutoSave(bool enabled) {
    if (d->autoSave == enabled || !savePreference("autoSave", enabled)) return;
    d->autoSave = enabled;
    if (enabled && dirty()) d->saveTimer->start(); else d->saveTimer->stop();
    emit preferencesChanged();
}
bool AppBridge::executeHotkey(int action) {
    // Recheck queued callbacks too. Disabling shortcuts must never disable
    // the mouse controls or allow a previously queued key event through.
    return action >= 0 && action < ACTION_COUNT && d->bindingEnabled[action] && d->hotkeysEnabled && !d->paused && execute(action);
}
void AppBridge::recordBinding(int action) {
    if (!d->listening || action < 0 || action >= ACTION_COUNT) { fail("Global hotkeys are unavailable."); return; }
    d->recordAction = action; d->recording = "Press your keys or mouse buttons, then release them."; emit bindingsChanged();
#ifdef Q_OS_WIN
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
#endif
}
void AppBridge::cancelRecording() {
#ifdef Q_OS_WIN
    if (d->listening) hotkeysRecordCancel();
#endif
    d->recordAction = -1; d->recording.clear(); emit bindingsChanged();
}
bool AppBridge::saveBinding(int action, const QString &text) {
    if (action < 0 || action >= ACTION_COUNT) return fail("Unknown action.");
#ifdef Q_OS_WIN
    HotkeySettings next = d->keys; char error[HOTKEY_ERROR_SIZE]{};
    if (!hotkeyParse(text.toUtf8().constData(), &next.bindings[action], error) || !hotkeyValidate(&next, error)) return fail(QString::fromUtf8(error));
    if (d->listening && !hotkeysApply(&next, d->keyPath, error)) return fail(QString::fromUtf8(error));
    d->keys = next; clearError(); emit bindingsChanged(); return true;
#else
    Q_UNUSED(text);
    return fail("Global hotkeys are not yet available on Linux. Use the playback buttons.");
#endif
}

bool AppBridge::setBindingEnabled(int action, bool enabled) {
    if (action < 0 || action >= ACTION_COUNT) return fail("Unknown action.");
    if (!savePreference(QString("bindingEnabled/%1").arg(action), enabled)) return false;
    d->bindingEnabled[action] = enabled; emit bindingsChanged(); return true;
}

QString AppBridge::shortcutForKey(int key, int modifiers) const {
    if (key == Qt::Key_unknown || key == Qt::Key_Shift || key == Qt::Key_Control
        || key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_AltGr) return {};
    const auto supported = Qt::KeyboardModifiers(modifiers) & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    return QKeySequence(QKeyCombination(supported, Qt::Key(key))).toString(QKeySequence::PortableText);
}
