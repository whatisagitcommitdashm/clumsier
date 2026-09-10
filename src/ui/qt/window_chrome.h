#pragma once

#include <QAbstractNativeEventFilter>
#include <QQuickWindow>

// Keep Windows' resize/maximize frame mechanics while drawing our own header.
// Other platforms use Qt's system move/resize operations directly.
class WindowChrome final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit WindowChrome(QQuickWindow *window);
    ~WindowChrome() override;
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;
public slots:
    void toggleMaximized();
private:
    QQuickWindow *window_;
    WId handle_ = 0;
};
