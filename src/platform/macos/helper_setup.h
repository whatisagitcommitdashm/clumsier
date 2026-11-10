#pragma once
#include <QObject>
#include <QProcess>
class DummynetBackend;
class MacHelperSetup : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool packaged READ packaged CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
public:
    explicit MacHelperSetup(DummynetBackend *backend, QObject *parent = nullptr);
    ~MacHelperSetup() override;
    bool packaged() const;
    bool busy() const { return process_.state() != QProcess::NotRunning; }
    QString message() const { return message_; }
    Q_INVOKABLE void authorize();
    Q_INVOKABLE void installRuntime();
    Q_INVOKABLE void acknowledge();
    Q_PROPERTY(bool firstRun READ firstRun CONSTANT)
    bool firstRun() const;
signals:
    void changed();
private:
    DummynetBackend *backend_;
    QProcess process_;
    QString message_;
};
