// Objective-C++ bridge between the Qt controller and the macOS packet-filter
// prototype. Ports the AppKit host's role (system extension activation, filter
// registration, XPC control with heartbeats) into a headless helper owned by
// MacQtBackend. Committed transitions only: a command succeeds when the
// provider acknowledges it, and unacknowledged changes stay uncommitted.
#import <Foundation/Foundation.h>
#import <NetworkExtension/NetworkExtension.h>
#import <SystemExtensions/SystemExtensions.h>
#import "control.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVariantList>

#include "qt_backend.h"

// Activation can require user approval in System Settings, which takes an
// unbounded time; stages wait bounded so the UI never hangs forever.
static const int kActivationTimeoutMs = 120000;
static const int kRegistrationTimeoutMs = 30000;
// Matches the AppKit host's command timeout.
static const int kCommandTimeoutMs = 10000;

// XPC reply blocks cannot capture C++ references; this record is filled by the
// reply block and read after the nested pump returns.
struct MacReply {
    bool ok = false;
    bool running = false;
    QString error;
    bool done = false;
};

// One activation/registration/XPC session. All entry points run on the calling
// (Qt main) thread and block on a nested event loop until the reply arrives or
// the deadline passes, mirroring LinuxQtBackend's synchronous NetworkBackendOps
// over an asynchronous peer. Provider replies hop to the main queue, and the
// nested pump delivers them.
struct MacControl {
    MacNotify notify = nullptr;
    void *context = nullptr;
    NSXPCConnection *connection = nil;
    BOOL activated = NO;      // System extension accepted by macOS.
    BOOL registered = NO;     // Filter preferences saved.
    BOOL shutdown = NO;       // Owner asked this helper to stop.
    BOOL waiting = NO;        // A command is being waited on.
    BOOL running = NO;        // Provider-reported delay state.
    MacReply *pending = nullptr; // Stage record the current callbacks complete.
};

static void completePending(MacControl *control, MacReply *reply, bool ok, bool running, NSString *error) {
    // Late callbacks after a timeout or stage switch must not write anywhere.
    if (!reply || control->pending != reply || reply->done) return;
    reply->ok = ok;
    reply->running = running;
    reply->error = error ? QString::fromNSString(error) : QString();
    reply->done = true;
}

// Pump the main thread's event loop until the stage completes, the deadline
// passes, or the owner shuts the helper down. XPC replies and activation
// callbacks hop to the main queue, so nested pumping delivers them.
static bool pumpStage(MacControl *control, MacReply *reply, int limitMs, const char *what) {
    QElapsedTimer deadline;
    deadline.start();
    QEventLoop loop;
    while (!reply->done) {
        loop.processEvents(QEventLoop::AllEvents, 50);
        if (control->shutdown) {
            control->pending = nullptr;
            return false;
        }
        if (deadline.elapsed() > limitMs) {
            control->pending = nullptr;
            reply->error = QString("%1 did not finish in time. Approve it in System Settings if asked, then retry. State is unconfirmed.").arg(QString::fromUtf8(what));
            return false;
        }
    }
    control->pending = nullptr;
    return true;
}

@interface MacControlHelper : NSObject <OSSystemExtensionRequestDelegate> {
@public
    MacControl control;
}
- (instancetype)initWithNotify:(MacNotify)notify context:(void *)context;
- (void)invalidate;
- (void)connectionLost;
- (NSXPCConnection *)ensureConnection:(NSString **)error;
- (BOOL)ensureActive:(MacReply *)reply;
- (BOOL)ensureRegistered:(MacReply *)reply;
- (BOOL)sendCommand:(const QJsonObject &)request reply:(MacReply *)reply;
@end

@implementation MacControlHelper

- (instancetype)initWithNotify:(MacNotify)notify context:(void *)context {
    if ((self = [super init])) {
        control.notify = notify;
        control.context = context;
    }
    return self;
}

- (void)dealloc {
    [self invalidate];
}

- (void)invalidate {
    control.shutdown = YES;
    [self connectionLost];
}

// Drop the provider connection. The provider releases held traffic when its
// client disconnects; report uncertainty instead of pretending a Stop was
// acknowledged.
- (void)connectionLost {
    NSXPCConnection *connection = control.connection;
    control.connection = nil;
    if (connection) {
        connection.interruptionHandler = nil;
        connection.invalidationHandler = nil;
        [connection invalidate];
    }
    if (control.running) {
        control.running = NO;
        if (control.notify)
            control.notify(control.context, false, QString::fromNSString(@"The packet provider connection failed or was lost. Close the prototype app if it is running. Held traffic should release; state is unconfirmed."));
    }
    completePending(&control, control.pending, NO, NO, @"The packet provider connection failed or was lost.");
}

- (NSXPCConnection *)ensureConnection:(NSString **)error {
    if (control.connection) return control.connection;
    NSString *service = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"ClumsierService"];
    NSString *extensionID = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"ClumsierExtension"];
    if (!service || !extensionID) {
        if (error) *error = @"This executable lacks the signed bundle metadata. Build the app bundle with the macOS packaging script; a bare developer build cannot reach the provider.";
        return nil;
    }
    NSXPCConnection *connection = [[NSXPCConnection alloc] initWithMachServiceName:service options:0];
    [connection setCodeSigningRequirement:macPeerRequirement(extensionID)];
    connection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(ClumsierControl)];
    __weak MacControlHelper *weakSelf = self;
    connection.interruptionHandler = ^{
        dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf connectionLost]; });
    };
    connection.invalidationHandler = ^{
        dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf connectionLost]; });
    };
    [connection resume];
    control.connection = connection;
    return connection;
}

- (BOOL)ensureActive:(MacReply *)reply {
    if (control.activated) return YES;
    NSString *extensionID = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"ClumsierExtension"];
    if (!extensionID) {
        reply->error = QString::fromNSString(@"This executable lacks the signed bundle metadata. Build the app bundle with the macOS packaging script.");
        return NO;
    }
    OSSystemExtensionRequest *request = [OSSystemExtensionRequest
        activationRequestForExtension:extensionID queue:dispatch_get_main_queue()];
    request.delegate = self;
    control.pending = reply;
    [[OSSystemExtensionManager sharedManager] submitRequest:request];
    if (!pumpStage(&control, reply, kActivationTimeoutMs, "System extension activation")) return NO;
    return reply->ok;
}

- (void)finishActivation:(OSSystemExtensionRequestResult)result replySource:(OSSystemExtensionRequest *)request {
    (void)request;
    if (result == OSSystemExtensionRequestWillCompleteAfterReboot) {
        control.activated = YES;
        completePending(&control, control.pending, YES, NO, nil);
        if (control.notify) control.notify(control.context, false, QString::fromNSString(@"macOS requires a restart to finish changing the extension."));
        return;
    }
    control.activated = YES;
    completePending(&control, control.pending, YES, NO, nil);
}

- (void)requestDidBecomeActive:(OSSystemExtensionRequest *)request {
    [self finishActivation:OSSystemExtensionRequestCompleted replySource:request];
}

- (void)request:(OSSystemExtensionRequest *)request didFailWithError:(NSError *)error {
    (void)request;
    completePending(&control, control.pending, NO, NO, error.localizedDescription);
}

- (void)request:(OSSystemExtensionRequest *)request didFinishWithResult:(OSSystemExtensionRequestResult)result {
    [self finishActivation:result replySource:request];
}

- (OSSystemExtensionReplacementAction)request:(OSSystemExtensionRequest *)request actionForReplacingExtension:(OSSystemExtensionProperties *)oldExtension withExtension:(OSSystemExtensionProperties *)newExtension {
    (void)request; (void)oldExtension; (void)newExtension;
    return OSSystemExtensionReplacementActionReplace;
}

- (void)requestNeedsUserApproval:(OSSystemExtensionRequest *)request {
    (void)request;
    // Keep waiting: approval can take minutes, and the pump deadline bounds
    // the wait with instructions if the user does not respond in time.
}

- (BOOL)ensureRegistered:(MacReply *)reply {
    if (control.registered) return YES;
    NSString *extensionID = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"ClumsierExtension"];
    if (!extensionID) {
        reply->error = QString::fromNSString(@"This executable lacks the signed bundle metadata. Build the app bundle with the macOS packaging script.");
        return NO;
    }
    NEFilterManager *manager = [NEFilterManager sharedManager];
    __weak MacControlHelper *weakSelf = self;
    control.pending = reply;
    [manager loadFromPreferencesWithCompletionHandler:^(NSError *error) {
        dispatch_async(dispatch_get_main_queue(), ^{
            MacControlHelper *strongSelf = weakSelf;
            if (!strongSelf) return;
            if (error) {
                completePending(&strongSelf->control, strongSelf->control.pending, NO, NO, error.localizedDescription);
                return;
            }
            NEFilterProviderConfiguration *existing = manager.providerConfiguration;
            if (existing && manager.enabled &&
                [existing.filterPacketProviderBundleIdentifier isEqualToString:extensionID]) {
                strongSelf->control.registered = YES;
                completePending(&strongSelf->control, strongSelf->control.pending, YES, NO, nil);
                return;
            }
            NEFilterProviderConfiguration *config = [NEFilterProviderConfiguration new];
            config.filterPackets = YES;
            config.filterSockets = NO;
            config.filterPacketProviderBundleIdentifier = extensionID;
            manager.providerConfiguration = config;
            manager.localizedDescription = @"Clumsier experimental packet delay";
            manager.enabled = YES;
            [manager saveToPreferencesWithCompletionHandler:^(NSError *saveError) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    MacControlHelper *savedSelf = weakSelf;
                    if (!savedSelf) return;
                    if (saveError) {
                        completePending(&savedSelf->control, savedSelf->control.pending, NO, NO, saveError.localizedDescription);
                        return;
                    }
                    savedSelf->control.registered = YES;
                    completePending(&savedSelf->control, savedSelf->control.pending, YES, NO, nil);
                });
            }];
        });
    }];
    if (!pumpStage(&control, reply, kRegistrationTimeoutMs, "Filter registration")) return NO;
    return reply->ok;
}

- (BOOL)sendCommand:(const QJsonObject &)request reply:(MacReply *)reply {
    NSString *metadataError = nil;
    NSXPCConnection *connection = [self ensureConnection:&metadataError];
    if (!connection) {
        reply->error = QString::fromNSString(metadataError);
        return NO;
    }
    const QString command = request.value("command").toString();
    __weak MacControlHelper *weakSelf = self;
    id<ClumsierControl> proxy = [connection remoteObjectProxyWithErrorHandler:^(NSError *error) {
        (void)error;
        dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf connectionLost]; });
    }];
    control.pending = reply;
    if (command == "stop") {
        [proxy stop:^(NSString *failure, BOOL active) {
            dispatch_async(dispatch_get_main_queue(), ^{
                MacControlHelper *strongSelf = weakSelf;
                if (!strongSelf) return;
                completePending(&strongSelf->control, strongSelf->control.pending, failure.length == 0, active, failure);
            });
        }];
        if (!pumpStage(&control, reply, kCommandTimeoutMs, "Stop")) {
            [self connectionLost];
            return NO;
        }
        return reply->ok;
    }
    // start/apply carry the preset JSON; the provider parses and resolves it
    // again. Baseline 0 is correct because the Qt controller only sends
    // resolved added-delay settings, which do not use a baseline.
    const QString preset = request.value("preset").toString();
    const QByteArray bytes = preset.toUtf8();
    NSData *json = [NSData dataWithBytes:bytes.constData() length:bytes.size()];
    const BOOL start = command == "start";
    [proxy configure:json baseline:0 step:0 start:start reply:^(NSString *failure, BOOL active) {
        dispatch_async(dispatch_get_main_queue(), ^{
            MacControlHelper *strongSelf = weakSelf;
            if (!strongSelf) return;
            completePending(&strongSelf->control, strongSelf->control.pending, failure.length == 0, active, failure);
        });
    }];
    if (!pumpStage(&control, reply, kCommandTimeoutMs, "The packet provider")) {
        [self connectionLost];
        return NO;
    }
    return reply->ok;
}

@end

void *macControlCreate(MacNotify notify, void *context) {
    if (!notify) return nullptr;
    MacControlHelper *helper = [[MacControlHelper alloc] initWithNotify:notify context:context];
    // The C++ owner releases this reference in macControlDestroy.
    return (__bridge_retained void *)helper;
}

QVariantList macControlCommand(void *raw, const QJsonObject &request) {
    if (!raw) return {false, false, "The macOS network backend is not available."};
    MacControlHelper *helper = (__bridge MacControlHelper *)raw;
    MacControl *control = &helper->control;
    if (control->shutdown) return {false, bool(control->running), "The network backend is shutting down."};
    if (control->waiting) return {false, bool(control->running), "Another network command is still in progress."};
    const QString command = request.value("command").toString();
    // Stop with nothing running must not trigger activation on shutdown.
    if (command == "stop" && !control->connection && !control->running)
        return {true, false, QString()};
    control->waiting = YES;
    MacReply reply;
    bool ok = false;
    if (command == "start" || command == "apply")
        ok = [helper ensureActive:&reply] && [helper ensureRegistered:&reply] && [helper sendCommand:request reply:&reply];
    else if (command == "stop")
        ok = [helper sendCommand:request reply:&reply];
    else
        reply.error = "Unknown network command.";
    control->waiting = NO;
    if (ok) control->running = reply.running;
    return {ok, bool(control->running), reply.error};
}

void macControlHeartbeat(void *raw) {
    if (!raw) return;
    MacControlHelper *helper = (__bridge MacControlHelper *)raw;
    MacControl *control = &helper->control;
    // The provider's watchdog only matters while delay is running; skip the
    // round trip otherwise, and never during a pending command.
    if (control->waiting || control->shutdown || !control->running || !control->connection) return;
    __weak MacControlHelper *weakSelf = helper;
    id<ClumsierControl> proxy = [control->connection remoteObjectProxyWithErrorHandler:^(NSError *error) {
        (void)error;
        dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf connectionLost]; });
    }];
    [proxy heartbeat:^(BOOL active) {
        dispatch_async(dispatch_get_main_queue(), ^{
            MacControlHelper *strongSelf = weakSelf;
            if (!strongSelf) return;
            const BOOL was = strongSelf->control.running;
            strongSelf->control.running = active;
            if (was && !active && strongSelf->control.notify)
                strongSelf->control.notify(strongSelf->control.context, false, QString::fromNSString(@"The packet provider stopped the session. Start to resume."));
        });
    }];
}

void macControlShutdown(void *raw) {
    if (!raw) return;
    MacControlHelper *helper = (__bridge MacControlHelper *)raw;
    // An explicit stop is the caller's job before shutdown. Disconnecting makes
    // the provider flush held traffic, per its documented behavior.
    [helper invalidate];
}

void macControlDestroy(void *raw) {
    if (!raw) return;
    CFBridgingRelease(raw);
}
