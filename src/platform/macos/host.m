#import <AppKit/AppKit.h>
#import <NetworkExtension/NetworkExtension.h>
#import <SystemExtensions/SystemExtensions.h>
#import "control.h"
#include "backends/macos/packet_match.h"

@interface ClumsierApp : NSObject <NSApplicationDelegate, OSSystemExtensionRequestDelegate> {
    NSWindow *window;
    NSTextField *baselineField, *status, *summary;
    NSMutableArray<NSButton *> *buttons;
    NSXPCConnection *connection;
    NSData *presetJSON;
    Preset preset;
    size_t step;
    BOOL running, busy;
    NSUInteger requestNumber;
    OSSystemExtensionRequest *removalRequest;
}
@end
@implementation ClumsierApp
- (NSString *)extensionID { return [[NSBundle mainBundle] objectForInfoDictionaryKey:@"ClumsierExtension"]; }
- (void)message:(NSString *)text { status.stringValue = text; }
- (void)setBusy:(BOOL)value {
    busy = value;
    for (NSButton *button in buttons) button.enabled = !value;
    baselineField.enabled = !value;
}
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    (void)note;
    window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 680, 430) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    window.title = @"Clumsier — macOS prototype";
    buttons = [NSMutableArray array];
    NSStackView *stack = [[NSStackView alloc] initWithFrame:NSMakeRect(20, 20, 640, 390)];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading; stack.spacing = 14;
    [window.contentView addSubview:stack];
    NSArray *titles = @[@"Enable provider", @"Import sequence", @"Start", @"Stop", @"Previous", @"Reset", @"Next", @"Remove provider"];
    SEL actions[] = {@selector(enable:), @selector(import:), @selector(start:), @selector(stop:), @selector(previous:), @selector(reset:), @selector(next:), @selector(remove:)};
    for (NSUInteger row = 0; row < 2; ++row) {
        NSStackView *bar = [NSStackView stackViewWithViews:@[]]; bar.spacing = 8;
        for (NSUInteger i = row * 4; i < row * 4 + 4; ++i) {
            NSButton *button = [NSButton buttonWithTitle:titles[i] target:self action:actions[i]];
            [buttons addObject:button]; [bar addArrangedSubview:button];
        }
        [stack addArrangedSubview:bar];
    }
    [stack addArrangedSubview:[NSTextField labelWithString:@"Your normal server ping (ms), measured with Clumsier stopped:"]];
    baselineField = [NSTextField textFieldWithString:@""];
    [baselineField.widthAnchor constraintEqualToConstant:130].active = YES;
    [stack addArrangedSubview:baselineField];
    summary = [NSTextField wrappingLabelWithString:@"Import a shared Clumsier JSON sequence. No delay starts automatically."];
    summary.font = [NSFont systemFontOfSize:16 weight:NSFontWeightMedium];
    [summary.widthAnchor constraintEqualToConstant:630].active = YES;
    [stack addArrangedSubview:summary];
    status = [NSTextField wrappingLabelWithString:@"Enable provider first. macOS may ask you to approve the system extension and network filter."];
    [status.widthAnchor constraintEqualToConstant:630].active = YES;
    [stack addArrangedSubview:status];
    [window center]; [window makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
    NSMenu *menu = [NSMenu new]; NSMenuItem *appItem = [NSMenuItem new]; [menu addItem:appItem];
    NSMenu *appMenu = [NSMenu new]; [appMenu addItemWithTitle:@"Quit Clumsier" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu; NSApp.mainMenu = menu;
    [NSTimer scheduledTimerWithTimeInterval:3 repeats:YES block:^(NSTimer *timer) {
        (void)timer;
        if (!self->connection) return;
        NSUInteger generation = self->requestNumber;
        NSXPCConnection *sentOn = self->connection;
        BOOL commandInFlight = self->busy;
        [[self proxy] heartbeat:^(BOOL active) {
            dispatch_async(dispatch_get_main_queue(), ^{
                if (commandInFlight || generation != self->requestNumber || sentOn != self->connection) return;
                if (self->running && !active && !self->busy) {
                    self->running = NO;
                    self->summary.stringValue = @"Delay stopped by provider";
                    [self message:@"The provider stopped the session (for example, after missing host heartbeats). Start to resume."];
                }
            });
        }];
    }];
}
- (void)enable:(id)sender {
    (void)sender; [self setBusy:YES];
    OSSystemExtensionRequest *request = [OSSystemExtensionRequest activationRequestForExtension:[self extensionID] queue:dispatch_get_main_queue()];
    request.delegate = self;
    [[OSSystemExtensionManager sharedManager] submitRequest:request];
    [self message:@"Requesting extension activation. Approve it in System Settings if asked."];
}
- (void)requestNeedsUserApproval:(OSSystemExtensionRequest *)request {
    (void)request; [self message:@"macOS needs your approval in System Settings → Login Items & Extensions / Network Extensions."];
}
- (OSSystemExtensionReplacementAction)request:(OSSystemExtensionRequest *)request actionForReplacingExtension:(OSSystemExtensionProperties *)old withExtension:(OSSystemExtensionProperties *)new {
    (void)request; (void)old; (void)new; return OSSystemExtensionReplacementActionReplace;
}
- (void)request:(OSSystemExtensionRequest *)request didFailWithError:(NSError *)error {
    (void)request; [self setBusy:NO]; [self message:error.localizedDescription];
}
- (void)request:(OSSystemExtensionRequest *)request didFinishWithResult:(OSSystemExtensionRequestResult)result {
    (void)request;
    if (result == OSSystemExtensionRequestWillCompleteAfterReboot) {
        [self setBusy:NO]; [self message:@"macOS requires a restart to finish changing the extension."]; return;
    }
    /* Removal clears the connection before submitting its request. */
    if (request == removalRequest) {
        removalRequest = nil;
        [self setBusy:NO]; [self message:@"Provider removed."]; return;
    }
    NEFilterManager *manager = [NEFilterManager sharedManager];
    [manager loadFromPreferencesWithCompletionHandler:^(NSError *error) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (error) { [self setBusy:NO]; [self message:error.localizedDescription]; return; }
            NEFilterProviderConfiguration *config = [NEFilterProviderConfiguration new];
            config.filterPackets = YES; config.filterSockets = NO;
            config.filterPacketProviderBundleIdentifier = [self extensionID];
            manager.providerConfiguration = config;
            manager.localizedDescription = @"Clumsier experimental packet delay";
            manager.enabled = YES;
            [manager saveToPreferencesWithCompletionHandler:^(NSError *saveError) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [self setBusy:NO];
                    [self message:saveError ? saveError.localizedDescription : @"Provider enabled; delay stopped. Import a sequence, then Start. If it is still activating, retry Start shortly."];
                });
            }];
        });
    }];
}
- (void)import:(id)sender {
    (void)sender;
    if (running) { [self message:@"Stop before importing another sequence."]; return; }
    NSOpenPanel *panel = [NSOpenPanel openPanel]; panel.allowsMultipleSelection = NO;
    [panel beginSheetModalForWindow:window completionHandler:^(NSModalResponse response) {
        if (response != NSModalResponseOK) return;
        NSNumber *length; NSError *readError;
        [panel.URL getResourceValue:&length forKey:NSURLFileSizeKey error:&readError];
        if (!length || length.unsignedLongLongValue > PRESET_JSON_MAX) { [self message:@"Choose a JSON preset no larger than 256 KiB."]; return; }
        NSData *json = [NSData dataWithContentsOfURL:panel.URL options:0 error:&readError];
        Preset loaded; MacPacketMatch selection; char error[NETWORK_ERROR_SIZE];
        if (!json) { [self message:readError.localizedDescription]; return; }
        if (!presetParse(json.bytes, json.length, &loaded, error) || !macPacketMatchPrepare(&loaded.target, &selection, error)) { [self message:@(error)]; return; }
        self->preset = loaded; self->presetJSON = json; self->step = 0;
        self->summary.stringValue = [NSString stringWithFormat:@"%s (stopped)\nStep: %s", loaded.name, loaded.steps[0].name];
        [self message:@"Sequence imported. Enter your normal ping for target-ping mode, then Start."];
    }];
}
- (BOOL)baseline:(uint32_t *)value {
    NSString *text = baselineField.stringValue;
    if (!text.length && preset.mode == PRESET_ADDED_DELAY) { *value = 0; return YES; }
    NSCharacterSet *digits = [NSCharacterSet characterSetWithCharactersInString:@"0123456789"];
    if (!text.length || [text rangeOfCharacterFromSet:[digits invertedSet]].location != NSNotFound || text.longLongValue > PING_MAX_MS) {
        [self message:@"Normal ping must be a whole number from 0 to 60000 ms."]; return NO;
    }
    *value = (uint32_t)text.longLongValue; return YES;
}
- (void)connectionLost:(NSXPCConnection *)lost {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!lost || self->connection != lost) return;
        self->connection = nil; ++self->requestNumber;
        [self setBusy:NO];
        /* A missing acknowledgement is not a Stop acknowledgement. The provider
         * flushes on disconnect; report uncertainty until a fresh command wins. */
        self->running = NO;
        self->summary.stringValue = @"Provider connection lost — network state unconfirmed";
        [self message:@"The provider is designed to release held traffic on disconnect. Reconnect with Stop before continuing, or remove the provider."];
    });
}
- (id<ClumsierControl>)proxy {
    if (!connection) {
        NSString *service = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"ClumsierService"];
        connection = [[NSXPCConnection alloc] initWithMachServiceName:service options:0];
        [connection setCodeSigningRequirement:macPeerRequirement([self extensionID])];
        connection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(ClumsierControl)];
        __weak NSXPCConnection *weakConnection = connection;
        // The host owns the connection; its retained handlers must not own the host.
        __weak ClumsierApp *weakSelf = self;
        connection.interruptionHandler = ^{ [weakSelf connectionLost:weakConnection]; };
        connection.invalidationHandler = ^{ [weakSelf connectionLost:weakConnection]; };
        [connection resume];
    }
    NSXPCConnection *current = connection;
    return [connection remoteObjectProxyWithErrorHandler:^(NSError *error) { (void)error; [current invalidate]; [self connectionLost:current]; }];
}
- (NSUInteger)beginCommand {
    [self setBusy:YES]; NSUInteger number = ++requestNumber;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        if (self->busy && self->requestNumber == number) {
            NSXPCConnection *lost = self->connection;
            [lost invalidate]; [self connectionLost:lost];
        }
    });
    return number;
}
- (void)move:(size_t)next start:(BOOL)start {
    if (busy) return;
    if (!presetJSON) { [self message:@"Import a sequence first."]; return; }
    uint32_t baseline; if (![self baseline:&baseline]) return;
    StepResult result; char error[NETWORK_ERROR_SIZE];
    for (size_t i = 0; i < preset.step_count; ++i)
        if (!presetResolve(&preset, i, true, baseline, &result, error)) { [self message:@(error)]; return; }
    if (!presetResolve(&preset, next, true, baseline, &result, error)) { [self message:@(error)]; return; }
    NSUInteger number = [self beginCommand];
    [[self proxy] configure:presetJSON baseline:baseline step:next start:start reply:^(NSString *failure, BOOL active) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (number != self->requestNumber) return;
            [self setBusy:NO];
            if (failure.length) { [self message:failure]; return; }
            self->step = next; self->running = active;
            NSString *detail = self->preset.mode == PRESET_TARGET_PING ? [NSString stringWithFormat:@"Expected ping: ~%u ms%@", result.estimated_ping_ms, result.below_baseline ? @" (target below your baseline)" : @""] : [NSString stringWithFormat:@"Added delay: %u ms in / %u ms out", result.lag.inbound_ms, result.lag.outbound_ms];
            self->summary.stringValue = [NSString stringWithFormat:@"%s (%@)\nStep: %s\n%@", self->preset.name, active ? @"running" : @"stopped", self->preset.steps[next].name, detail];
            [self message:@"Settings acknowledged by the packet provider."];
        });
    }];
}
- (void)start:(id)sender { (void)sender; [self move:step start:YES]; }
- (void)next:(id)sender { (void)sender; [self move:step + 1 < preset.step_count ? step + 1 : preset.loop ? 0 : step start:NO]; }
- (void)previous:(id)sender { (void)sender; [self move:step ? step - 1 : preset.loop && preset.step_count ? preset.step_count - 1 : 0 start:NO]; }
- (void)reset:(id)sender { (void)sender; [self move:0 start:NO]; }
- (void)stop:(id)sender {
    (void)sender; if (busy) return;
    NSUInteger number = [self beginCommand];
    [[self proxy] stop:^(NSString *error, BOOL active) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (number != self->requestNumber) return;
            [self setBusy:NO];
            if (error.length) { [self message:error]; return; }
            self->running = active;
            self->summary.stringValue = self->presetJSON ? [NSString stringWithFormat:@"%s (stopped)\nStep: %s", self->preset.name, self->preset.steps[self->step].name] : @"Delay stopped";
            [self message:@"Stopped and flushed. The installed provider now allows traffic."];
        });
    }];
}
- (void)remove:(id)sender {
    (void)sender; if (busy) return; [self setBusy:YES];
    /* Removing the saved filter asks the provider to stop and flush even when
     * its command connection is unavailable. This affects only this app's filter. */
    [[NEFilterManager sharedManager] removeFromPreferencesWithCompletionHandler:^(NSError *error) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (error) { [self setBusy:NO]; [self message:error.localizedDescription]; return; }
            [self->connection invalidate]; self->connection = nil; self->running = NO;
            [self message:@"Removing provider…"];
            OSSystemExtensionRequest *request = [OSSystemExtensionRequest deactivationRequestForExtension:[self extensionID] queue:dispatch_get_main_queue()];
            self->removalRequest = request;
            request.delegate = self; [[OSSystemExtensionManager sharedManager] submitRequest:request];
        });
    }];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app { (void)app; return YES; }
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)app {
    (void)app;
    /* Closing the XPC connection triggers the provider's serialized flush. The
     * extension stays installed but pass-through until the next explicit Start. */
    [connection invalidate]; return NSTerminateNow;
}
@end
int main(void) {
    @autoreleasepool { NSApplication *app = [NSApplication sharedApplication]; ClumsierApp *delegate = [ClumsierApp new]; app.delegate = delegate; [app setActivationPolicy:NSApplicationActivationPolicyRegular]; [app run]; }
    return 0;
}
