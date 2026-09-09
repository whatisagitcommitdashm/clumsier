/* Compile-only API probe. This is deliberately not an installable extension.
 * Compiling says nothing about entitlements, activation, or packet delivery. */
#import <NetworkExtension/NetworkExtension.h>

NEFilterPacketProviderVerdict clumsierCheckPacketAPI(NEFilterPacketProvider *provider,
                                                    NEFilterPacketContext *context) {
    /* A real adapter may call this API only inside packetHandler. Strong
     * retention is essential: releasing a delayed packet without allowing it
     * drops it. This probe is never executed. */
    NEPacket *packet = [provider delayCurrentPacket:context];
    [provider allowPacket:packet];
    return NEFilterPacketProviderVerdictDelay;
}
