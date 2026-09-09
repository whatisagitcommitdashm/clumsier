#pragma once
#include "core/network.h"

/* Experimental scheduler for NEFilterPacketProvider, not a NetworkBackend yet.
 * One serialized owner calls every function, including timer ticks. The adapter
 * owns retained NEPacket objects; allow_packet must allow AND release each one.
 * It must not reenter this scheduler. No packet bytes are copied or modified. */
#define MAC_HELD_LIMIT 256
#define MAC_HELD_BYTES (4u * 1024u * 1024u)

typedef void (*MacAllowPacket)(void *context, void *packet);
typedef struct {
    void *packet;
    uint64_t received_ms;
    size_t bytes;
    bool outbound;
} MacHeldPacket;
typedef struct {
    MacHeldPacket packets[MAC_HELD_LIMIT];
    size_t count, bytes;
    LagSettings lag;
    bool running;
    MacAllowPacket allow_packet;
    void *context;
} MacHeldPackets;

/* Initialize once before use; never reinitialize a live queue. */
void macHeldInit(MacHeldPackets *queue, MacAllowPacket allow_packet, void *context);
/* Internal lifecycle check: rejects a second start instead of discarding held
 * packets. A future NetworkBackend adapter must make user Start idempotent, as
 * the controller already does, rather than exposing this as a user error. */
bool macHeldStart(MacHeldPackets *queue, const LagSettings *lag, char *error);
bool macHeldApply(MacHeldPackets *queue, const LagSettings *lag, uint64_t now_ms, char *error);
/* Takes ownership of a non-NULL retained packet on every path. The adapter must
 * return the delay verdict because it already called delayCurrentPacket. Prefer
 * bypassing that call for zero delay; this fallback still safely allows it.
 * A full queue allows the new packet immediately (possible overload reordering). */
void macHeldAdd(MacHeldPackets *queue, void *packet, size_t bytes, bool outbound, uint64_t now_ms);
void macHeldTick(MacHeldPackets *queue, uint64_t now_ms);
void macHeldStop(MacHeldPackets *queue);
