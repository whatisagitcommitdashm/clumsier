#include <QGuiApplication>
#include <QQuickWindow>
#import <AppKit/AppKit.h>
void macConfigureHudWindow(QQuickWindow *window);
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQuickWindow window;
    window.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    macConfigureHudWindow(&window);
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(window.winId());
    NSWindow *native = view.window;
    const auto behavior = native.collectionBehavior;
    if (!native || native.hidesOnDeactivate || native.level != NSStatusWindowLevel) return 1;
    if (!(behavior & NSWindowCollectionBehaviorCanJoinAllSpaces)) return 2;
    if (!(behavior & NSWindowCollectionBehaviorFullScreenAuxiliary)) return 3;
    if (behavior & (NSWindowCollectionBehaviorMoveToActiveSpace | NSWindowCollectionBehaviorFullScreenPrimary)) return 4;
    if (@available(macOS 13.0, *)) {
        if (!(behavior & NSWindowCollectionBehaviorCanJoinAllApplications)) return 5;
    }
    if (native.isKeyWindow) return 6;
    return 0;
}
