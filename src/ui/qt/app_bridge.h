#pragma once
#include <QObject>
#include <QVariant>
#include <QUrl>
#include <memory>
extern "C" {
#include "core/network.h"
}

// QML owns presentation and editor drafts. The C controller owns accepted
// network state; the platform adapters own disk access and global input.
class AppBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList presets READ presets NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY libraryChanged)
    Q_PROPERTY(QStringList missingServers READ missingServers NOTIFY libraryChanged)
    Q_PROPERTY(QVariantMap draft READ draft NOTIFY draftChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY draftChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY draftChanged)
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString profileId READ profileId NOTIFY stateChanged)
    Q_PROPERTY(QVariantList bindings READ bindings NOTIFY bindingsChanged)
    Q_PROPERTY(QString recording READ recording NOTIFY bindingsChanged)
    Q_PROPERTY(bool globalHotkeysAvailable READ globalHotkeysAvailable CONSTANT)
    Q_PROPERTY(bool hotkeysEnabled READ hotkeysEnabled WRITE setHotkeysEnabled NOTIFY preferencesChanged)
    Q_PROPERTY(bool autoSave READ autoSave WRITE setAutoSave NOTIFY preferencesChanged)
public:
    explicit AppBridge(NetworkBackend backend, const QString &root = {}, bool enableHotkeys = true, QObject *parent = nullptr);
    ~AppBridge() override;
    QVariantList presets() const;
    QVariantList profiles() const;
    QStringList missingServers() const;
    bool installStarterPresets();
    Q_INVOKABLE bool resolveServer(const QString &name, int baseline);
    QVariantMap draft() const;
    QString selectedId() const;
    bool dirty() const;
    QVariantMap state() const;
    QString error() const;
    QString profileId() const;
    QVariantList bindings() const;
    QString recording() const;
    bool globalHotkeysAvailable() const;
    void reportBackendError(const QString &message) { fail(message); }
    bool hotkeysEnabled() const;
    bool autoSave() const;
    void setHotkeysEnabled(bool enabled);
    void setAutoSave(bool enabled);
    Q_INVOKABLE bool executeHotkey(int action);
    Q_INVOKABLE bool batchSequences(const QString &operation, const QStringList &ids, const QUrl &folder = {});
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool selectPreset(const QString &id);
    Q_INVOKABLE bool newPreset();
    Q_INVOKABLE bool commitDraft(const QVariantMap &value, int activeStep = -1);
    Q_INVOKABLE bool setBindingEnabled(int action, bool enabled);
    Q_INVOKABLE void invalidEntry() { fail("Invalid entry. The previous value was restored."); }
    Q_INVOKABLE void updateDraft(const QVariantMap &value);
    Q_INVOKABLE bool save();
    Q_INVOKABLE void discard();
    Q_INVOKABLE bool duplicate();
    Q_INVOKABLE bool deletePreset();
    Q_INVOKABLE bool importPreset(const QUrl &file);
    Q_INVOKABLE bool exportPreset(const QUrl &file);
    Q_INVOKABLE bool saveProfile(const QString &id, const QString &name, int baseline, bool select = true);
    Q_INVOKABLE bool deleteProfile(const QString &id);
    Q_INVOKABLE bool selectProfile(const QString &id);
    Q_INVOKABLE bool loadSequence();
    Q_INVOKABLE bool quickDelay(int milliseconds, int direction);
    Q_INVOKABLE bool switchActivity(const QString &activity);
    Q_INVOKABLE bool execute(int action);
    Q_INVOKABLE bool selectStep(int index);
    Q_INVOKABLE void setInputPaused(bool paused);
    Q_INVOKABLE void recordBinding(int action);
    Q_INVOKABLE void cancelRecording();
    Q_INVOKABLE bool saveBinding(int action, const QString &text);
    Q_INVOKABLE void clearError();
    Q_INVOKABLE QString shortcutForKey(int key, int modifiers) const;
signals:
    void preferencesChanged();
    void libraryChanged();
    void draftChanged();
    void stateChanged();
    void errorChanged();
    void bindingsChanged();
private:
    struct Data;
    std::unique_ptr<Data> d;
    bool fail(const QString &message);
    void prepareSequence();
    bool applySequence(const QVariantMap &value, const QString &profile, size_t step = 0);
    bool storeServerAssociation();
    void restoreServerAssociation();
    bool savePreference(const QString &key, bool value);
    void updateHotkeyPause();
    QString matchingProfile(const QString &name) const;
    QString serverName(const QString &profileId) const;
    static AppBridge *listenerOwner;
};
