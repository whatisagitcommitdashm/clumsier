#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "backends/macos/held_packets.h"

typedef struct { int values[1024]; size_t count; } Allowed;
static void allowPacket(void *context, void *packet) {
    Allowed *allowed = context;
    assert(allowed->count < 1024);
    allowed->values[allowed->count++] = *(int *)packet;
}
int main(void) {
    MacHeldPackets queue;
    Allowed allowed = {{0}, 0};
    LagSettings lag = {true, true, true, 500, 100}, invalid;
    char error[NETWORK_ERROR_SIZE];
    int packets[MAC_HELD_LIMIT + 2];
    size_t i;
    for (i = 0; i < MAC_HELD_LIMIT + 2; ++i) packets[i] = (int)i;
    macHeldInit(&queue, allowPacket, &allowed);
    invalid = lag; invalid.inbound_ms = LAG_MAX_MS + 1;
    assert(!macHeldStart(&queue, &invalid, error));
    assert(!queue.running && !queue.count);
    assert(macHeldStart(&queue, &lag, error));
    assert(!macHeldStart(&queue, &lag, error));
    macHeldAdd(&queue, &packets[0], 20, false, 1000);
    macHeldAdd(&queue, &packets[1], 30, true, 1000);
    macHeldTick(&queue, 999);
    assert(!allowed.count);
    macHeldTick(&queue, 1100);
    assert(allowed.count == 1 && allowed.values[0] == 1 && queue.bytes == 20);
    assert(!macHeldApply(&queue, &invalid, 1600, error));
    assert(queue.lag.inbound_ms == 500 && queue.count == 1);
    lag.inbound_ms = 1000;
    assert(macHeldApply(&queue, &lag, 1200, error));
    macHeldTick(&queue, 1500);
    assert(queue.count == 1); /* Increased delay changed this existing packet. */
    lag.inbound_ms = 200;
    assert(macHeldApply(&queue, &lag, 1500, error));
    assert(!queue.count && allowed.values[1] == 0);
    macHeldAdd(&queue, &packets[2], 40, false, 2000);
    lag.inbound = false;
    assert(macHeldApply(&queue, &lag, 2000, error));
    assert(!queue.count && allowed.values[2] == 2);
    macHeldAdd(&queue, &packets[3], 10, false, 2000);
    assert(!queue.count && allowed.values[3] == 3);
    lag.outbound_ms = 0;
    assert(macHeldApply(&queue, &lag, 2000, error));
    macHeldAdd(&queue, &packets[4], 10, true, 2000);
    assert(!queue.count && allowed.values[4] == 4);
    lag.inbound = true; lag.outbound_ms = 100;
    assert(macHeldApply(&queue, &lag, UINT64_MAX - 50, error));
    macHeldAdd(&queue, &packets[5], 10, true, UINT64_MAX - 50);
    macHeldTick(&queue, UINT64_MAX);
    assert(queue.count == 1); /* No deadline overflow near the clock limit. */
    macHeldStop(&queue);
    assert(!queue.count && !queue.bytes && allowed.values[5] == 5);
    macHeldStop(&queue);
    assert(allowed.count == 6);
    assert(!macHeldApply(&queue, &lag, 0, error));
    assert(macHeldStart(&queue, &lag, error));
    allowed.count = 0;
    for (i = 0; i < MAC_HELD_LIMIT; ++i)
        macHeldAdd(&queue, &packets[i], 1, false, 0);
    macHeldAdd(&queue, &packets[MAC_HELD_LIMIT], 1, false, 0);
    assert(queue.count == MAC_HELD_LIMIT && allowed.count == 1);
    assert(allowed.values[0] == MAC_HELD_LIMIT);
    macHeldStop(&queue);
    for (i = 0; i < MAC_HELD_LIMIT; ++i) assert(allowed.values[i + 1] == (int)i);
    assert(macHeldStart(&queue, &lag, error));
    macHeldAdd(&queue, &packets[0], MAC_HELD_BYTES, false, 0);
    macHeldAdd(&queue, &packets[1], (size_t)-1, false, 0);
    assert(queue.count == 1 && queue.bytes == MAC_HELD_BYTES);
    lag.enabled = false;
    assert(macHeldApply(&queue, &lag, 0, error));
    assert(!queue.count && !queue.bytes);
    macHeldStop(&queue);
    allowed.count = 0;
    lag.enabled = true; lag.inbound_ms = 100;
    assert(macHeldStart(&queue, &lag, error));
    macHeldAdd(&queue, &packets[0], 1, false, 1000);
    lag.inbound_ms = 500;
    assert(macHeldApply(&queue, &lag, 1050, error));
    macHeldTick(&queue, 1499);
    assert(queue.count == 1);
    macHeldTick(&queue, 1500);
    assert(queue.count == 0 && allowed.count == 1);
    macHeldStop(&queue);
    puts("macOS candidate scheduler tests passed (no macOS networking exercised).");
    return 0;
}
