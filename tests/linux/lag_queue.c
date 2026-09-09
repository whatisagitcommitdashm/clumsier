#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/backends/linux/lag_queue.h"

typedef struct {
    uint32_t ids[LINUX_LAG_QUEUE_CAPACITY + 8];
    size_t count;
    uint32_t fail_id;
} Verdicts;

static bool acceptPacket(void *context, uint32_t id) {
    Verdicts *verdicts = context;
    if (id == verdicts->fail_id) return false;
    assert(verdicts->count < sizeof(verdicts->ids) / sizeof(verdicts->ids[0]));
    verdicts->ids[verdicts->count++] = id;
    return true;
}

static void liveChanges(void) {
    LinuxLagQueue queue;
    Verdicts verdicts = {0};
    char error[NETWORK_ERROR_SIZE];
    LagSettings lag = {true, true, true, 500, 100};
    linuxLagQueueInit(&queue);
    assert(linuxLagQueueWait(&queue, 1000) == -1);
    assert(linuxLagQueueConfigure(&queue, &lag, error));
    assert(linuxLagQueuePush(&queue, 1, false, 1000));
    assert(linuxLagQueuePush(&queue, 2, true, 1000));
    assert(linuxLagQueueWait(&queue, 1099) == 1);
    assert(linuxLagQueueProcess(&queue, 1100, false, acceptPacket, &verdicts));
    assert(verdicts.count == 1 && verdicts.ids[0] == 2);
    lag.inbound_ms = 1000;
    assert(linuxLagQueueConfigure(&queue, &lag, error));
    assert(linuxLagQueueWait(&queue, 1500) == 500);
    lag.inbound_ms = 250;
    assert(linuxLagQueueConfigure(&queue, &lag, error));
    assert(linuxLagQueueProcess(&queue, 1500, false, acceptPacket, &verdicts));
    assert(verdicts.count == 2 && verdicts.ids[1] == 1);
    assert(linuxLagQueuePush(&queue, 3, false, 1600));
    lag.inbound_ms = 0;
    assert(linuxLagQueueConfigure(&queue, &lag, error));
    assert(linuxLagQueueWait(&queue, 1600) == 0);
    assert(linuxLagQueueProcess(&queue, 1600, false, acceptPacket, &verdicts));
    assert(verdicts.ids[2] == 3 && queue.count == 0);
    // Zero also applies to new packets at exactly the same clock tick.
    assert(linuxLagQueuePush(&queue, 4, false, 1600));
    assert(linuxLagQueueProcess(&queue, 1600, false, acceptPacket, &verdicts));
    assert(queue.count == 0 && verdicts.ids[3] == 4);
}

static void failuresAndDrain(void) {
    LinuxLagQueue queue;
    Verdicts verdicts = {0};
    char error[NETWORK_ERROR_SIZE];
    LagSettings lag = {true, true, true, 50, 500};
    linuxLagQueueInit(&queue);
    assert(linuxLagQueueConfigure(&queue, &lag, error));
    assert(linuxLagQueuePush(&queue, 1, false, 100));
    assert(linuxLagQueuePush(&queue, 2, true, 100));
    assert(linuxLagQueuePush(&queue, 3, false, 100));
    assert(linuxLagQueuePush(&queue, 4, false, 100));
    verdicts.fail_id = 3;
    assert(!linuxLagQueueProcess(&queue, 150, false, acceptPacket, &verdicts));
    assert(verdicts.count == 1 && verdicts.ids[0] == 1);
    assert(queue.count == 3 && queue.packets[0].id == 2 && queue.packets[1].id == 3);
    assert(queue.packets[2].id == 4 && queue.packets[1].arrived_ms == 100);
    lag.inbound_ms = LAG_MAX_MS + 1;
    assert(!linuxLagQueueConfigure(&queue, &lag, error));
    assert(queue.lag.inbound_ms == 50 && queue.lag.outbound_ms == 500);
    verdicts.fail_id = 0;
    assert(linuxLagQueueProcess(&queue, 150, false, acceptPacket, &verdicts));
    assert(queue.count == 1 && queue.packets[0].id == 2);
    verdicts.fail_id = 2;
    assert(!linuxLagQueueProcess(&queue, 150, true, acceptPacket, &verdicts));
    assert(queue.count == 1);
    verdicts.fail_id = 0;
    assert(linuxLagQueueProcess(&queue, 150, true, acceptPacket, &verdicts));
    assert(queue.count == 0 && verdicts.ids[3] == 2);
    assert(linuxLagQueueProcess(&queue, 150, true, acceptPacket, &verdicts));
    assert(verdicts.count == 4);
}

static void limitsAndDisable(void) {
    LinuxLagQueue queue;
    Verdicts verdicts = {0};
    char error[NETWORK_ERROR_SIZE];
    LagSettings lag = {true, true, true, LAG_MAX_MS, LAG_MAX_MS};
    size_t i;
    linuxLagQueueInit(&queue);
    assert(linuxLagQueueConfigure(&queue, &lag, error));
    for (i = 0; i < LINUX_LAG_QUEUE_CAPACITY; ++i)
        assert(linuxLagQueuePush(&queue, (uint32_t)i + 1, (i % 2) != 0, 100));
    assert(!linuxLagQueuePush(&queue, 9000, false, 100));
    assert(!linuxLagQueuePush(&queue, 1, false, 100));
    assert(queue.count == LINUX_LAG_QUEUE_CAPACITY);
    assert(linuxLagQueueWait(&queue, 99) == LAG_MAX_MS);
    lag.inbound = false;
    assert(linuxLagQueueConfigure(&queue, &lag, error));
    assert(linuxLagQueueProcess(&queue, 100, false, acceptPacket, &verdicts));
    assert(queue.count == LINUX_LAG_QUEUE_CAPACITY / 2);
    for (i = 0; i < verdicts.count; ++i) assert(verdicts.ids[i] == (uint32_t)(2 * i + 1));
    lag.enabled = false;
    assert(linuxLagQueueConfigure(&queue, &lag, error));
    assert(linuxLagQueueProcess(&queue, 100, false, acceptPacket, &verdicts));
    assert(queue.count == 0 && verdicts.count == LINUX_LAG_QUEUE_CAPACITY);
    // A 64-bit monotonic timestamp keeps working past the 32-bit ms boundary.
    lag.enabled = true;
    lag.inbound = true;
    lag.inbound_ms = 1;
    assert(linuxLagQueueConfigure(&queue, &lag, error));
    assert(linuxLagQueuePush(&queue, 2000, false, UINT64_C(4294967295)));
    assert(!linuxLagQueuePush(&queue, 2000, false, UINT64_C(4294967295)));
    assert(linuxLagQueueProcess(&queue, UINT64_C(4294967296), false, acceptPacket, &verdicts));
    assert(queue.count == 0);
}

int main(void) {
    liveChanges();
    failuresAndDrain();
    limitsAndDisable();
    puts("Linux Lag queue logic tests passed (no Linux networking exercised).");
    return 0;
}
