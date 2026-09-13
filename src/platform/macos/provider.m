#import <NetworkExtension/NetworkExtension.h>
#import <Network/Network.h>
#import "control.h"
#include "backends/macos/held_packets.h"
#include "backends/macos/packet_match.h"
#include <time.h>

static uint64_t milliseconds(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000; }
@interface ClumsierPacketProvider : NEFilterPacketProvider <NSXPCListenerDelegate, ClumsierControl> {
    dispatch_queue_t owner;
    dispatch_group_t deliveries;
    NSRecursiveLock *stateLock;
    NSMutableArray<NEPacket *> *readyPackets;
    dispatch_source_t timer;
    NSXPCListener *listener;
    NSXPCConnection *client;
    MacHeldPackets held;
    MacPacketMatch match;
    CaptureTarget target;
    BOOL available;
    uint64_t lastContact;
}
- (void)queueReadyPacket:(NEPacket *)packet;
@end
static void allowPacket(void *context, void *pointer) {
    ClumsierPacketProvider *provider = (__bridge ClumsierPacketProvider *)context;
    NEPacket *packet = CFBridgingRelease(pointer);
    [provider queueReadyPacket:packet];
}
@implementation ClumsierPacketProvider
- (instancetype)init {
    if ((self = [super init])) {
        owner = dispatch_queue_create("Clumsier packet owner", DISPATCH_QUEUE_SERIAL);
        deliveries = dispatch_group_create();
        stateLock = [NSRecursiveLock new];
        readyPackets = [NSMutableArray array];
        macHeldInit(&held, allowPacket, (__bridge void *)self);
    }
    return self;
}
- (void)queueReadyPacket:(NEPacket *)packet { [readyPackets addObject:packet]; }
- (void)withState:(void (^)(void))operation {
    [stateLock lock];
    operation();
    NSArray<NEPacket *> *ready = [readyPackets copy];
    [readyPackets removeAllObjects];
    if (ready.count) dispatch_group_enter(deliveries);
    [stateLock unlock];
    // Never hold the state lock during native delivery, which may reenter networking.
    for (NEPacket *packet in ready) [self allowPacket:packet];
    if (ready.count) dispatch_group_leave(deliveries);
}
- (void)startFilterWithCompletionHandler:(void (^)(NSError *))completion {
    __weak ClumsierPacketProvider *weakSelf = self;
    [self withState:^{
        self->available = YES;
        self->timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, self->owner);
        dispatch_source_set_timer(self->timer, dispatch_time(DISPATCH_TIME_NOW, 0), 2 * NSEC_PER_MSEC, NSEC_PER_MSEC);
        dispatch_source_set_event_handler(self->timer, ^{
            ClumsierPacketProvider *self = weakSelf;
            if (!self) return;
            [self withState:^{
                uint64_t now = milliseconds();
                if (self->held.running && now - self->lastContact > 10000) macHeldStop(&self->held);
                macHeldTick(&self->held, now);
            }];
        });
        dispatch_resume(self->timer);
        NSDictionary *keys = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"NetworkExtension"];
        self->listener = [[NSXPCListener alloc] initWithMachServiceName:keys[@"NEMachServiceName"]];
        self->listener.delegate = self;
        [self->listener resume];
        /* withState executes inline. delayCurrentPacket stays on the actual
         * framework callback thread, inside the packet handler lifetime. */
        self.packetHandler = ^NEFilterPacketProviderVerdict(NEFilterPacketContext *context, nw_interface_t interface, NETrafficDirection direction, const void *bytes, size_t length) {
            (void)interface;
            // Keep the provider alive for this callback, not for the block's lifetime.
            ClumsierPacketProvider *self = weakSelf;
            if (!self) return NEFilterPacketProviderVerdictAllow;
            __block NEFilterPacketProviderVerdict verdict = NEFilterPacketProviderVerdictAllow;
            [self withState:^{
                BOOL outbound = direction == NETrafficDirectionOutbound;
                uint32_t delay = outbound ? self->held.lag.outbound_ms : self->held.lag.inbound_ms;
                BOOL selected = outbound ? self->held.lag.outbound : self->held.lag.inbound;
                if ((direction != NETrafficDirectionInbound && !outbound) || !self->available || !self->held.running || !self->held.lag.enabled || !selected || !delay ||
                    self->held.count >= MAC_HELD_LIMIT || length > MAC_HELD_BYTES - self->held.bytes ||
                    !macPacketMatches(&self->match, bytes, length, outbound)) return;
                NEPacket *packet = [self delayCurrentPacket:context];
                if (!packet) return;
                macHeldAdd(&self->held, (void *)CFBridgingRetain(packet), length, outbound, milliseconds());
                verdict = NEFilterPacketProviderVerdictDelay;
            }];
            return verdict;
        };
    }];
    completion(nil);
}
- (void)stopFilterWithReason:(NEProviderStopReason)reason completionHandler:(void (^)(void))completion {
    (void)reason;
    [self withState:^{
        self->available = NO;
        macHeldStop(&self->held);
        self.packetHandler = nil;
        if (self->timer) { dispatch_source_cancel(self->timer); self->timer = nil; }
        [self->listener invalidate]; self->listener = nil;
        [self->client invalidate]; self->client = nil;
    }];
    dispatch_group_notify(deliveries, owner, completion);
}
- (BOOL)listener:(NSXPCListener *)service shouldAcceptNewConnection:(NSXPCConnection *)connection {
    (void)service;
    NSString *host = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"ClumsierHost"];
    [connection setCodeSigningRequirement:macPeerRequirement(host)];
    connection.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(ClumsierControl)];
    connection.exportedObject = self;
    __weak NSXPCConnection *weakConnection = connection;
    __weak ClumsierPacketProvider *weakSelf = self;
    void (^lost)(void) = ^{
        ClumsierPacketProvider *self = weakSelf;
        if (!self) return;
        [self withState:^{
            if (self->client == weakConnection) {
                macHeldStop(&self->held);
                self->client = nil;
            }
        }];
    };
    connection.invalidationHandler = lost;
    connection.interruptionHandler = lost;
    __block BOOL accepted = NO;
    [self withState:^{
        if (self->available && !self->client) { self->client = connection; accepted = YES; }
    }];
    if (accepted) [connection resume];
    return accepted;
}
- (void)configure:(NSData *)json baseline:(NSUInteger)baseline step:(NSUInteger)step start:(BOOL)start reply:(void (^)(NSString *, BOOL))reply {
    NSXPCConnection *sender = [NSXPCConnection currentConnection];
    __block NSString *failure = @""; __block BOOL active;
    [self withState:^{
        Preset preset; StepResult result; MacPacketMatch candidate; char error[NETWORK_ERROR_SIZE] = {0};
        BOOL ok = sender && sender == self->client && self->available && json.length <= PRESET_JSON_MAX && baseline <= PING_MAX_MS;
        if (!ok) strcpy(error, "Provider is unavailable or the request exceeds its limits.");
        if (ok) ok = presetParse(json.bytes, json.length, &preset, error);
        if (ok) ok = presetResolve(&preset, step, true, (uint32_t)baseline, &result, error);
        if (ok) ok = macPacketMatchPrepare(&preset.target, &candidate, error);
        if (ok && self->held.running && !captureTargetsEqual(&self->target, &preset.target)) {
            strcpy(error, "Stop before changing traffic selection."); ok = NO;
        }
        if (ok && self->held.running) ok = macHeldApply(&self->held, &result.lag, milliseconds(), error);
        else if (ok && start) ok = macHeldStart(&self->held, &result.lag, error);
        if (ok) { self->match = candidate; self->target = preset.target; self->lastContact = milliseconds(); }
        failure = ok ? @"" : @(error); active = self->held.running;
    }];
    reply(failure, active);
}
- (void)stop:(void (^)(NSString *, BOOL))reply {
    NSXPCConnection *sender = [NSXPCConnection currentConnection];
    __block NSString *error = @""; __block BOOL active;
    [self withState:^{
        if (sender != self->client) error = @"Connection expired.";
        else macHeldStop(&self->held);
        active = self->held.running;
    }];
    dispatch_group_notify(deliveries, owner, ^{ reply(error, active); });
}
- (void)heartbeat:(void (^)(BOOL))reply {
    NSXPCConnection *sender = [NSXPCConnection currentConnection];
    __block BOOL active;
    [self withState:^{
        if (sender == self->client) self->lastContact = milliseconds();
        active = self->held.running;
    }];
    reply(active);
}
@end
int main(void) {
    @autoreleasepool { [NEProvider startSystemExtensionMode]; dispatch_main(); }
    return 0;
}
