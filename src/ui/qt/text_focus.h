#pragma once
#include <QCoreApplication>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>

// Observe presses before QML dispatches them. This lets a click finish the old
// edit and still reach its destination (including Rename, which starts an edit).
class TextFocus : public QObject {
public:
    explicit TextFocus(QQuickWindow *window) : QObject(window), window_(window) {
        QCoreApplication::instance()->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject *target, QEvent *event) override {
        if (target != window_ || event->type() != QEvent::MouseButtonPress) return false;
        auto *field = window_->activeFocusItem();
        if (!field || !field->property("cursorPosition").isValid()) return false;
        const auto position = static_cast<QMouseEvent *>(event)->position();
        if (!field->contains(field->mapFromScene(position))) {
            field->setFocus(false, Qt::MouseFocusReason);
            window_->contentItem()->forceActiveFocus(Qt::MouseFocusReason);
        }
        return false;
    }
private:
    QQuickWindow *window_;
};
