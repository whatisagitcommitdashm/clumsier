#include "hud_stacking.h"
#include <QPointer>
#include <QTimer>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unordered_map>
#endif

struct HudStacking::Data {
    QPointer<QQuickWindow> window;
    QTimer settle;
#ifdef Q_OS_WIN
    HWND handle = nullptr;
    HWINEVENTHOOK foregroundHook = nullptr, locationHook = nullptr;
    // Out-of-context WinEvent callbacks arrive on the registering UI thread.
    inline static std::unordered_map<HWINEVENTHOOK, Data *> listeners;

    static void CALLBACK changed(HWINEVENTHOOK hook, DWORD, HWND source,
                                 LONG object, LONG, DWORD, DWORD) {
        const auto found = listeners.find(hook);
        if (found == listeners.end()) return;
        auto *self = found->second;
        if (!self->window || !self->window->isVisible() || source == self->handle) return;
        if (object != OBJID_WINDOW || source != GetForegroundWindow()) return;
        // Let the fullscreen transition settle. Coalesce resize events instead
        // of repeatedly rearranging windows while the user drags one.
        if (!self->settle.isActive()) self->settle.start();
    }
    void repairOrder() {
        if (!window || !window->isVisible() || !IsWindow(handle)) return;
        const HWND foreground = GetForegroundWindow();
        if (!foreground || foreground == handle) return;
        // An existing topmost flag isn't enough: another topmost window may
        // have moved ahead of us. Only raise if the foreground is above us.
        for (HWND above = GetWindow(foreground, GW_HWNDPREV); above; above = GetWindow(above, GW_HWNDPREV))
            if (above == handle) return;
        SetWindowPos(handle, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    void unhook(HWINEVENTHOOK &hook) {
        if (!hook) return;
        listeners.erase(hook);
        UnhookWinEvent(hook);
        hook = nullptr;
    }
    void watchVisibility() {
        if (window && window->isVisible()) {
            if (!foregroundHook) {
                foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                    nullptr, changed, 0, 0, WINEVENT_OUTOFCONTEXT);
                if (foregroundHook) listeners[foregroundHook] = this;
            }
            if (!locationHook) {
                // F11 can resize a window without changing the foreground app.
                locationHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                    nullptr, changed, 0, 0, WINEVENT_OUTOFCONTEXT);
                if (locationHook) listeners[locationHook] = this;
            }
            settle.start();
        } else {
            settle.stop(); unhook(foregroundHook); unhook(locationHook);
        }
    }
    ~Data() { unhook(foregroundHook); unhook(locationHook); }
#endif
};

HudStacking::HudStacking(QQuickWindow *window) : d(std::make_unique<Data>()) {
    d->window = window;
#ifdef Q_OS_WIN
    d->handle = reinterpret_cast<HWND>(window->winId());
    d->settle.setSingleShot(true);
    d->settle.setInterval(80);
    connect(&d->settle, &QTimer::timeout, this, [this] { d->repairOrder(); });
    connect(window, &QWindow::visibleChanged, this, [this] { d->watchVisibility(); });
    d->watchVisibility();
#endif
}
HudStacking::~HudStacking() = default;
