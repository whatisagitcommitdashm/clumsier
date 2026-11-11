#include <QQuickWindow>
#import <AppKit/AppKit.h>

// Qt's Cocoa WId is the content NSView, not the NSWindow itself.
void macConfigureHudWindow(QQuickWindow *window) {
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    NSWindow *native = view.window;
    if (!native) return;
    // Qt.Tool can otherwise hide on application deactivation. Keep this HUD
    // independent of the editor and eligible for other apps' full-screen Spaces.
    native.hidesOnDeactivate = NO;
    native.level = NSStatusWindowLevel;
    NSWindowCollectionBehavior behavior = native.collectionBehavior;
    behavior &= ~(NSWindowCollectionBehaviorMoveToActiveSpace
        | NSWindowCollectionBehaviorManaged | NSWindowCollectionBehaviorStationary
        | NSWindowCollectionBehaviorParticipatesInCycle
        | NSWindowCollectionBehaviorFullScreenPrimary | NSWindowCollectionBehaviorFullScreenNone);
    behavior |= NSWindowCollectionBehaviorCanJoinAllSpaces
        | NSWindowCollectionBehaviorTransient | NSWindowCollectionBehaviorIgnoresCycle
        | NSWindowCollectionBehaviorFullScreenAuxiliary;
    if (@available(macOS 13.0, *)) {
        behavior &= ~(NSWindowCollectionBehaviorPrimary | NSWindowCollectionBehaviorAuxiliary);
        behavior |= NSWindowCollectionBehaviorCanJoinAllApplications;
    }
    native.collectionBehavior = behavior;
    if (window->isVisible()) [native orderFrontRegardless];
    // Never activate the app or make the HUD key: the game keeps keyboard focus.
}
