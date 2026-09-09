#include <assert.h>
#include <string.h>
#include "held_packets.h"

static uint32_t delayFor(const MacHeldPackets *queue, bool outbound) {
    if (!queue->running || !queue->lag.enabled) return 0;
    if (outbound) return queue->lag.outbound ? queue->lag.outbound_ms : 0;
    return queue->lag.inbound ? queue->lag.inbound_ms : 0;
}

void macHeldInit(MacHeldPackets *queue, MacAllowPacket allow_packet, void *context) {
    assert(allow_packet);
    memset(queue, 0, sizeof(*queue));
    queue->allow_packet = allow_packet;
    queue->context = context;
}

bool macHeldStart(MacHeldPackets *queue, const LagSettings *lag, char *error) {
    if (queue->running) {
        strcpy(error, "The packet scheduler is already running.");
        return false;
    }
    if (!lagSettingsValidate(lag, error)) return false;
    queue->lag = *lag;
    queue->running = true;
    return true;
}

void macHeldTick(MacHeldPackets *queue, uint64_t now_ms) {
    size_t read, kept = 0;
    for (read = 0; read < queue->count; ++read) {
        MacHeldPacket packet = queue->packets[read];
        uint32_t delay = delayFor(queue, packet.outbound);
        /* Use elapsed time, not a cached deadline. A new setting applies to all
         * held packets without restarting their clocks. A bad backward clock
         * sample must not underflow and accidentally release everything. */
        bool ready = !delay || (now_ms >= packet.received_ms && now_ms - packet.received_ms >= delay);
        if (ready) {
            queue->bytes -= packet.bytes;
            queue->allow_packet(queue->context, packet.packet);
        } else {
            queue->packets[kept++] = packet;
        }
    }
    queue->count = kept;
    /* Every direction is examined, so a long inbound delay cannot block an
     * outbound packet behind it. Arrival order is retained among survivors. */
}

bool macHeldApply(MacHeldPackets *queue, const LagSettings *lag, uint64_t now_ms, char *error) {
    if (!queue->running) {
        strcpy(error, "The packet scheduler is stopped.");
        return false;
    }
    if (!lagSettingsValidate(lag, error)) return false;
    queue->lag = *lag;
    macHeldTick(queue, now_ms);
    return true;
}

void macHeldAdd(MacHeldPackets *queue, void *packet, size_t bytes, bool outbound, uint64_t now_ms) {
    MacHeldPacket held;
    assert(packet);
    if (!delayFor(queue, outbound) || queue->count == MAC_HELD_LIMIT ||
        bytes > MAC_HELD_BYTES - queue->bytes) {
        queue->allow_packet(queue->context, packet);
        return;
    }
    held.packet = packet;
    held.received_ms = now_ms;
    held.bytes = bytes;
    held.outbound = outbound;
    queue->packets[queue->count++] = held;
    queue->bytes += bytes;
}

void macHeldStop(MacHeldPackets *queue) {
    /* Disable admission before flushing. Repeated stop is harmless. The native
     * adapter must synchronize callbacks before destroying this queue. */
    queue->running = false;
    macHeldTick(queue, 0);
}
