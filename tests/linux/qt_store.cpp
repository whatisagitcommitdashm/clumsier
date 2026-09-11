#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <cstring>
extern "C" {
#include "platform/linux/preset_store.h"
}
static void check(bool ok, const char *message) { if (!ok) qFatal("FAIL: %s", message); }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    PresetStore store{}; char error[NETWORK_ERROR_SIZE]{};
    auto root = (temp.path() + "/unicode-λ").toStdWString();
    check(presetStoreOpen(&store, root.c_str(), error), error);
    Preset preset; presetDefault(&preset);
    wchar_t id[LIBRARY_ID_SIZE]{};
    check(presetStoreSave(&store, id, &preset, error), error);
    Preset read{};
    check(presetStoreRead(&store, id, &read, error) && !strcmp(read.name, preset.name), "Unicode path round trip");
    check(!presetStoreRead(&store, L"../../outside", &read, error), "Traversal ID rejected");
    BaselineProfile profile{}; strcpy(profile.name, "Local server"); profile.baseline_ms = 50;
    wchar_t profileId[LIBRARY_ID_SIZE]{};
    check(profileStoreSave(&store, profileId, &profile, error), error);
    const QString file = QString::fromStdWString(root) + "/profiles/" + QString::fromWCharArray(profileId) + ".json";
    for (const QByteArray &bad : {QByteArray("[]"), QByteArray("[1]"), QByteArray("null"),
            QByteArray("{\"format_version\":1,\"name\":\"a\",\"baseline_ms\":1.5}"),
            QByteArray("{\"format_version\":1,\"name\":\"a\",\"baseline_ms\":5,\"baseline_ms\":6}")}) {
        QFile output(file); check(output.open(QIODevice::WriteOnly), "Write corrupt fixture");
        check(output.write(bad) == bad.size(), "Fixture write complete"); output.close();
        profile.baseline_ms = 999;
        check(!profileStoreRead(&store, profileId, &profile, error) && profile.baseline_ms == 999, "Malformed profile rejected transactionally");
        LibraryEntry entries[LIBRARY_MAX_ITEMS]{}; size_t count = 0, skipped = 0;
        check(presetStoreList(&store, true, entries, &count, &skipped, error) && count == 0 && skipped == 1,
              "Corrupt profile listed as skipped");
        check(output.open(QIODevice::ReadOnly) && output.readAll() == bad, "Corrupt profile preserved");
    }
    // Replacing a destination directory must fail without losing the draft ID.
    const QString blocked = QString::fromStdWString(root) + "/blocked.json";
    check(QDir().mkpath(blocked), "Create blocked destination");
    check(!presetFileWrite(blocked.toStdWString().c_str(), &preset, error) && QDir(blocked).exists(), "Failed atomic write preserves destination");
    check(presetStoreDelete(&store, false, id, error), error);
    qInfo("PASS Linux storage: Unicode, validation, corrupt-file preservation and failed atomic writes");
    return 0;
}
