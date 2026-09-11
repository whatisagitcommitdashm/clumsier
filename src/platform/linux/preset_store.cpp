#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QUuid>
#include <cstring>
extern "C" {
#include "preset_store.h"
#include "../../../external/cjson/cJSON.h"
}
static bool fail(char *error, const char *message) { snprintf(error, NETWORK_ERROR_SIZE, "%s", message); return false; }
static QString text(const wchar_t *s) { return QString::fromWCharArray(s); }
static bool validId(const QString &id) {
    static const QRegularExpression pattern("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    return pattern.match(id).hasMatch();
}
static QString path(const PresetStore *store, bool profiles, const wchar_t *id, char *error) {
    if (!id || !validId(text(id))) { fail(error, "Invalid library ID."); return {}; }
    return text(store->root) + (profiles ? "/profiles/" : "/presets/") + text(id) + ".json";
}
static bool read(const QString &path, QByteArray &data, char *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, "Cannot read the selected file.");
    data = file.read(PRESET_JSON_MAX + 1);
    if (file.error() != QFileDevice::NoError || data.isEmpty() || data.size() > PRESET_JSON_MAX || data.contains('\0'))
        return fail(error, "Cannot read file: empty, too large, or incomplete.");
    return true;
}
static bool write(const QString &path, const QByteArray &data, char *error) {
    QSaveFile file(path);
    // Do not fall back to overwriting the original if atomic replacement fails.
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return fail(error, "Could not save the file. The previous file has been kept.");
    return true;
}
bool presetStoreOpen(PresetStore *store, const wchar_t *root, char *error) {
    const QString directory = root ? text(root) : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/Clumsier";
    auto wide = directory.toStdWString();
    if (wide.size() >= 4096) return fail(error, "Library path is too long.");
    if (!QDir().mkpath(directory + "/presets") || !QDir().mkpath(directory + "/profiles"))
        return fail(error, "Cannot create the preset library folder.");
    wcscpy(store->root, wide.c_str());
    return true;
}
bool presetFileRead(const wchar_t *file, Preset *preset, char *error) {
    QByteArray data;
    return read(text(file), data, error) && presetParse(data.constData(), size_t(data.size()), preset, error);
}
bool presetFileWrite(const wchar_t *file, const Preset *preset, char *error) {
    char *data = presetSerialize(preset, error);
    if (!data) return false;
    bool ok = write(text(file), data, error); free(data); return ok;
}
bool presetStoreRead(const PresetStore *store, const wchar_t *id, Preset *preset, char *error) {
    auto file = path(store, false, id, error);
    if (file.isEmpty()) return false;
    return presetFileRead(file.toStdWString().c_str(), preset, error);
}
static bool save(const PresetStore *store, bool profiles, wchar_t *id, const QByteArray &data, char *error) {
    const QString chosen = *id ? text(id) : QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto file = path(store, profiles, chosen.toStdWString().c_str(), error);
    if (file.isEmpty()) return false;
    if (!*id && QFileInfo::exists(file)) return fail(error, "Library ID collision. Try saving again.");
    if (!write(file, data, error)) return false;
    wcscpy(id, chosen.toStdWString().c_str()); return true;
}
bool presetStoreSave(const PresetStore *store, wchar_t *id, const Preset *preset, char *error) {
    char *data = presetSerialize(preset, error);
    if (!data) return false;
    bool ok = save(store, false, id, data, error); free(data); return ok;
}
bool profileStoreRead(const PresetStore *store, const wchar_t *id, BaselineProfile *profile, char *error) {
    QByteArray data;
    auto file = path(store, true, id, error);
    if (file.isEmpty() || !read(file, data, error)) return false;
    cJSON *root = data.contains("\\u0000") ? nullptr : cJSON_ParseWithLengthOpts(data.constData(), size_t(data.size()) + 1, nullptr, 1);
    BaselineProfile parsed{};
    unsigned seen = 0;
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return fail(error, "Invalid baseline profile object."); }
    bool ok = true;
    cJSON *item;
    cJSON_ArrayForEach(item, root) {
        unsigned bit = 0;
        if (!strcmp(item->string, "format_version")) { bit = 1; if (!cJSON_IsNumber(item) || item->valuedouble != 1) ok = false; }
        else if (!strcmp(item->string, "name")) {
            bit = 2;
            if (!cJSON_IsString(item) || !*item->valuestring || strlen(item->valuestring) >= PRESET_NAME_SIZE) ok = false;
            else strcpy(parsed.name, item->valuestring);
        } else if (!strcmp(item->string, "baseline_ms")) {
            bit = 4;
            if (!cJSON_IsNumber(item) || !(item->valuedouble >= 0 && item->valuedouble <= PING_MAX_MS)) ok = false;
            else { parsed.baseline_ms = uint32_t(item->valuedouble); if (item->valuedouble != parsed.baseline_ms) ok = false; }
        } else ok = false;
        if (seen & bit) ok = false;
        seen |= bit;
    }
    cJSON_Delete(root);
    if (!ok || seen != 7) return fail(error, "Invalid baseline profile. The file has been left untouched.");
    *profile = parsed; return true;
}
bool profileStoreSave(const PresetStore *store, wchar_t *id, const BaselineProfile *profile, char *error) {
    if (!memchr(profile->name, 0, PRESET_NAME_SIZE) || !*profile->name || profile->baseline_ms > PING_MAX_MS)
        return fail(error, "Enter a profile name and a baseline between 0 and 60000 ms.");
    cJSON *root = cJSON_CreateObject();
    if (!root || !cJSON_AddNumberToObject(root, "format_version", 1) || !cJSON_AddStringToObject(root, "name", profile->name) ||
        !cJSON_AddNumberToObject(root, "baseline_ms", profile->baseline_ms)) {
        cJSON_Delete(root); return fail(error, "Cannot allocate profile JSON.");
    }
    char *data = cJSON_Print(root); cJSON_Delete(root);
    if (!data) return fail(error, "Cannot allocate profile JSON.");
    bool ok = save(store, true, id, data, error); free(data); return ok;
}
bool presetStoreDelete(const PresetStore *store, bool profiles, const wchar_t *id, char *error) {
    auto file = path(store, profiles, id, error);
    if (file.isEmpty()) return false;
    return QFile::remove(file) || fail(error, "Cannot delete the selected library file.");
}
bool presetStoreList(const PresetStore *store, bool profiles, LibraryEntry *entries, size_t *count, size_t *skipped, char *error) {
    *count = *skipped = 0;
    QDir directory(text(store->root) + (profiles ? "/profiles" : "/presets"));
    if (!directory.exists() || !QFileInfo(directory.path()).isReadable()) return fail(error, "Cannot list the preset library.");
    for (const auto &file : directory.entryInfoList({"*.json"}, QDir::Files | QDir::Hidden | QDir::System)) {
        const QString id = file.completeBaseName();
        if (*count == LIBRARY_MAX_ITEMS || !validId(id) || file.isSymLink()) { ++*skipped; continue; }
        auto wide = id.toStdWString();
        Preset preset{}; BaselineProfile profile{}; char detail[NETWORK_ERROR_SIZE]{};
        if (!(profiles ? profileStoreRead(store, wide.c_str(), &profile, detail) : presetStoreRead(store, wide.c_str(), &preset, detail))) {
            ++*skipped; continue;
        }
        auto &entry = entries[(*count)++];
        wcscpy(entry.id, wide.c_str()); strcpy(entry.name, profiles ? profile.name : preset.name);
    }
    return true;
}
