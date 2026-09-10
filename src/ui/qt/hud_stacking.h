#pragma once
#include <QObject>
#include <QQuickWindow>
#include <memory>

// Desktop fullscreen windows can be raised above an existing topmost HUD.
// Keep its ordering current without activating it or changing the other app.
class HudStacking final : public QObject {
public:
    explicit HudStacking(QQuickWindow *window);
    ~HudStacking() override;
private:
    struct Data;
    std::unique_ptr<Data> d;
};
