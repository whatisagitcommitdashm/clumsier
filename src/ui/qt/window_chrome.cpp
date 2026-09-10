#include "window_chrome.h"
#include <QCoreApplication>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#endif

WindowChrome::WindowChrome(QQuickWindow *window) : window_(window)
{
    handle_ = window_->winId();
    QCoreApplication::instance()->installNativeEventFilter(this);
    connect(window_, SIGNAL(maximizeRequested()), this, SLOT(toggleMaximized()));
#ifdef Q_OS_WIN
    const auto handle = reinterpret_cast<HWND>(handle_);
    const auto style = GetWindowLongPtr(handle, GWL_STYLE);
    SetWindowLongPtr(handle, GWL_STYLE, style | WS_THICKFRAME | WS_CAPTION |
                     WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
    SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    // Let the compositor round the actual window, including the client area.
    // Windows keeps maximized/snapped windows square. Older Windows versions
    // ignore this preference and retain their normal frame behavior.
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(handle, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
#endif
}

void WindowChrome::toggleMaximized()
{
#ifdef Q_OS_WIN
    // Qt's frameless maximize path only resizes the rectangle. Ask Windows for
    // actual maximization so native restore geometry and taskbar behavior work.
    const auto handle = reinterpret_cast<HWND>(handle_);
    ShowWindow(handle, IsZoomed(handle) ? SW_RESTORE : SW_MAXIMIZE);
#else
    if (window_->windowStates().testFlag(Qt::WindowMaximized)) window_->showNormal();
    else window_->showMaximized();
#endif
}

WindowChrome::~WindowChrome()
{
    QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool WindowChrome::nativeEventFilter(const QByteArray &, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    auto *event = static_cast<MSG *>(message);
    // Never call winId() here: during destruction it can recreate the window.
    if (event->hwnd != reinterpret_cast<HWND>(handle_)) return false;
    if (event->message == WM_NCCALCSIZE && event->wParam) {
        // The normal client fills the entire window. On maximize, use the work
        // area: treating the whole monitor as client can hide the taskbar and
        // make Qt mistake maximization for exclusive fullscreen.
        auto *area = reinterpret_cast<NCCALCSIZE_PARAMS *>(event->lParam);
        if (IsZoomed(event->hwnd)) {
            MONITORINFO monitor{sizeof(MONITORINFO)};
            if (GetMonitorInfo(MonitorFromWindow(event->hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
                area->rgrc[0] = monitor.rcWork;
        }
        *result = 0;
        return true;
    }
#else
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return false;
}
