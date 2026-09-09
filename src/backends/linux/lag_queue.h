#pragma once
#include "../../core/network.h"

#define LINUX_LAG_QUEUE_CAPACITY 1024

typedef struct {
    uint32_t id;
    uint64_t arrived_ms;
    bool outbound;
} LinuxHeldPacket;

typedef struct {
    LinuxHeldPacket packets[LINUX_LAG_QUEUE_CAPACITY];
    size_t count;
    LagSettings lag;
} LinuxLagQueue;

// The eventual Linux worker owns this object. These functions are deliberately
// single-threaded; a UI must send commands to that worker, not edit it directly.
// IDs identify packets still owned by the kernel. We never copy/edit payloads.
typedef bool (*LinuxAcceptPacket)(void *context, uint32_t id);

void linuxLagQueueInit(LinuxLagQueue *queue);
bool linuxLagQueueConfigure(LinuxLagQueue *queue, const LagSettings *lag, char *error);
// False means ownership has NOT transferred: the caller must handle this ID.
bool linuxLagQueuePush(LinuxLagQueue *queue, uint32_t id, bool outbound, uint64_t now_ms);
// A failed verdict keeps its ID, and all later IDs, available for retry.
bool linuxLagQueueProcess(LinuxLagQueue *queue, uint64_t now_ms, bool drain,
                          LinuxAcceptPacket accept, void *context);
// Milliseconds until the next packet is eligible, or -1 when empty.
int linuxLagQueueWait(const LinuxLagQueue *queue, uint64_t now_ms);
