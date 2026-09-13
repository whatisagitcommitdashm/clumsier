#include "hotkeys.h"
#include <QMetaObject>
#include <QObject>
#include <CoreGraphics/CoreGraphics.h>
#include <condition_variable>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

namespace {
void setListenerError(char *error, const char *text) {
    if (!error) return;
    std::strncpy(error, text, MAC_HOTKEY_ERROR_SIZE - 1);
    error[MAC_HOTKEY_ERROR_SIZE - 1] = 0;
}

CGEventMask eventMask() {
    return CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp)
        | CGEventMaskBit(kCGEventFlagsChanged)
        | CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventLeftMouseUp)
        | CGEventMaskBit(kCGEventRightMouseDown) | CGEventMaskBit(kCGEventRightMouseUp)
        | CGEventMaskBit(kCGEventOtherMouseDown) | CGEventMaskBit(kCGEventOtherMouseUp);
}
}

struct MacHotkeyListener::Impl : std::enable_shared_from_this<Impl> {
    explicit Impl(QObject *context) : deliveryContext(context) {}

    QObject *deliveryContext = nullptr;
    MacHotkeyDispatcher dispatcher;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable ready;
    CFMachPortRef tap = nullptr;
    CFRunLoopSourceRef source = nullptr;
    CFRunLoopRef runLoop = nullptr;
    bool started = false;
    bool opening = false;
    std::atomic<unsigned long> generation{0};

    static void queueInput(Impl *self, unsigned int physical, bool down) {
        if (!physical || !self->deliveryContext) return;
        const auto lifetime = self->shared_from_this();
        const unsigned long queuedGeneration = lifetime->generation.load(std::memory_order_acquire);
        QMetaObject::invokeMethod(lifetime->deliveryContext, [lifetime, physical, down, queuedGeneration] {
            if (queuedGeneration == lifetime->generation.load(std::memory_order_acquire))
                lifetime->dispatcher.input(physical, down);
        }, Qt::QueuedConnection);
    }

    static void queueReset(Impl *self) {
        const auto lifetime = self->shared_from_this();
        const unsigned long resetGeneration = lifetime->generation.fetch_add(1,
            std::memory_order_acq_rel) + 1;
        if (!lifetime->deliveryContext) return;
        QMetaObject::invokeMethod(lifetime->deliveryContext, [lifetime, resetGeneration] {
            if (resetGeneration == lifetime->generation.load(std::memory_order_acquire))
                lifetime->dispatcher.reset();
        }, Qt::QueuedConnection);
    }

    static CGEventRef callback(CGEventTapProxy, CGEventType type, CGEventRef event, void *userInfo) {
        auto *self = static_cast<Impl *>(userInfo);
        if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
            // Input may have been missed while the tap was disabled. Discard
            // every held/latching state before accepting events again.
            queueReset(self);
            if (self->tap) CGEventTapEnable(self->tap, true);
            return event;
        }

        unsigned int physical = 0;
        bool down = false;
        switch (type) {
            case kCGEventKeyDown:
            case kCGEventKeyUp:
            case kCGEventFlagsChanged: {
                const auto keyCode = unsigned(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
                physical = macHotkeyPhysicalKey(keyCode);
                if (type == kCGEventFlagsChanged) {
                    // Caps Lock is a toggle, not a normal held modifier. Treat
                    // each toggle event as one press/release so recording can
                    // finish and the binding can rearm.
                    if (keyCode == 0x39) {
                        queueInput(self, physical, true);
                        queueInput(self, physical, false);
                        return event;
                    }
                    switch (keyCode) {
                        case 0x3b: case 0x3e: case 0x3a: case 0x3d:
                        case 0x38: case 0x3c: case 0x36: case 0x37:
                            down = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState,
                                CGKeyCode(keyCode));
                            break;
                        default: return event;
                    }
                } else down = type == kCGEventKeyDown;
                break;
            }
            case kCGEventLeftMouseDown: case kCGEventLeftMouseUp:
            case kCGEventRightMouseDown: case kCGEventRightMouseUp:
            case kCGEventOtherMouseDown: case kCGEventOtherMouseUp:
                physical = macHotkeyFromMouseButton(unsigned(CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber)));
                down = type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown || type == kCGEventOtherMouseDown;
                break;
            default: break;
        }
        queueInput(self, physical, down);
        return event;
    }

    void run() {
        CFMachPortRef localTap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
            kCGEventTapOptionListenOnly, eventMask(), callback, this);
        CFRunLoopSourceRef localSource = localTap
            ? CFMachPortCreateRunLoopSource(kCFAllocatorDefault, localTap, 0) : nullptr;
        CFRunLoopRef localRunLoop = CFRunLoopGetCurrent();
        if (localRunLoop) CFRetain(localRunLoop);
        const bool readyToRun = localTap && localSource && localRunLoop;
        if (readyToRun) {
            CFRunLoopAddSource(localRunLoop, localSource, kCFRunLoopCommonModes);
            CGEventTapEnable(localTap, true);
        }
        {
            std::lock_guard lock(mutex);
            tap = localTap;
            source = localSource;
            runLoop = localRunLoop;
            started = readyToRun;
            opening = false;
        }
        ready.notify_one();
        if (readyToRun) {
            CFRunLoopRun();
            CFRunLoopRemoveSource(localRunLoop, localSource, kCFRunLoopCommonModes);
        }
        {
            std::lock_guard lock(mutex);
            started = false;
            tap = nullptr;
            source = nullptr;
            runLoop = nullptr;
        }
        if (localSource) CFRelease(localSource);
        if (localTap) CFRelease(localTap);
        if (localRunLoop) CFRelease(localRunLoop);
    }

    void resetInput() {
        generation.fetch_add(1, std::memory_order_acq_rel);
    }
};

MacHotkeyListener::MacHotkeyListener(QObject *deliveryContext)
    : impl_(std::make_shared<Impl>(deliveryContext)) {}

MacHotkeyListener::~MacHotkeyListener() { close(); }

bool MacHotkeyListener::open(ActionCallback callback, bool requestPermission, char *error) {
    if (isOpen()) { impl_->dispatcher.setActionCallback(std::move(callback)); return true; }
    if (!CGPreflightListenEventAccess()
        && (!requestPermission || !CGRequestListenEventAccess())) {
        setListenerError(error, "Global hotkeys need Input Monitoring permission. Enable Clumsier in System Settings > Privacy & Security > Input Monitoring, then turn hotkeys on again.");
        return false;
    }
    impl_->dispatcher.setActionCallback(std::move(callback));
    {
        std::lock_guard lock(impl_->mutex);
        impl_->opening = true;
    }
    impl_->thread = std::thread([impl = impl_.get()] { impl->run(); });
    {
        std::unique_lock lock(impl_->mutex);
        impl_->ready.wait(lock, [this] { return !impl_->opening; });
    }
    if (!isOpen()) {
        if (impl_->thread.joinable()) impl_->thread.join();
        setListenerError(error, "Could not create the macOS global input listener. Check Input Monitoring permission and try again.");
        return false;
    }
    return true;
}

void MacHotkeyListener::close() {
    CFRunLoopRef runLoop = nullptr;
    {
        std::lock_guard lock(impl_->mutex);
        runLoop = impl_->runLoop;
        if (runLoop) CFRetain(runLoop);
    }
    if (runLoop) {
        CFRunLoopStop(runLoop);
        CFRunLoopWakeUp(runLoop);
        CFRelease(runLoop);
    }
    if (impl_->thread.joinable()) impl_->thread.join();
    impl_->resetInput();
}

bool MacHotkeyListener::isOpen() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->started;
}

bool MacHotkeyListener::apply(const HotkeySettings &settings, const QString &path, char *error) {
    if (!isOpen()) { setListenerError(error, "Global hotkeys are unavailable."); return false; }
    if (!macHotkeyValidate(&settings, error)) return false;
    if (!path.isEmpty() && !macHotkeySave(path, &settings, error)) return false;
    impl_->resetInput();
    impl_->dispatcher.apply(settings);
    return true;
}

void MacHotkeyListener::pause() { impl_->resetInput(); impl_->dispatcher.pause(); }
void MacHotkeyListener::resume() { impl_->resetInput(); impl_->dispatcher.resume(); }
void MacHotkeyListener::record(RecordCallback callback) { impl_->resetInput(); impl_->dispatcher.record(std::move(callback)); }
void MacHotkeyListener::cancelRecord() { impl_->resetInput(); impl_->dispatcher.cancelRecord(); }
