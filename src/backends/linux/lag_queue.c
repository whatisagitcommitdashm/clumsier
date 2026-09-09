#include <string.h>
#include "lag_queue.h"

void linuxLagQueueInit(LinuxLagQueue *queue) {
    memset(queue, 0, sizeof(*queue));
    lagSettingsDefault(&queue->lag);
}

bool linuxLagQueueConfigure(LinuxLagQueue *queue, const LagSettings *lag, char *error) {
    if (!lagSettingsValidate(lag, error)) return false;
    queue->lag = *lag;
    return true;
}

bool linuxLagQueuePush(LinuxLagQueue *queue, uint32_t id, bool outbound, uint64_t now_ms) {
    LinuxHeldPacket packet = {id, now_ms, outbound};
    size_t i;
    if (queue->count == LINUX_LAG_QUEUE_CAPACITY) return false;
    for (i = 0; i < queue->count; ++i)
        if (queue->packets[i].id == id) return false;
    queue->packets[queue->count++] = packet;
    return true;
}

static uint32_t remaining(const LinuxHeldPacket *packet, const LagSettings *lag, uint64_t now_ms) {
    uint32_t delay = packet->outbound ? lag->outbound_ms : lag->inbound_ms;
    bool selected = packet->outbound ? lag->outbound : lag->inbound;
    uint64_t age;
    if (!lag->enabled || !selected || !delay) return 0;
    // CLOCK_MONOTONIC should never go backwards. Being conservative here also
    // keeps a bad caller timestamp from turning subtraction into a huge age.
    age = now_ms >= packet->arrived_ms ? now_ms - packet->arrived_ms : 0;
    return age >= delay ? 0 : delay - (uint32_t)age;
}

bool linuxLagQueueProcess(LinuxLagQueue *queue, uint64_t now_ms, bool drain,
                          LinuxAcceptPacket accept, void *context) {
    size_t read, kept = 0;
    for (read = 0; read < queue->count; ++read) {
        LinuxHeldPacket packet = queue->packets[read];
        // Recompute from arrival time and CURRENT settings. Storing a deadline
        // at arrival would make a later delay change leave held packets behind.
        if (drain || remaining(&packet, &queue->lag, now_ms) == 0) {
            if (!accept(context, packet.id)) {
                // Only successful verdicts remove ownership. Stop this pass on
                // failure rather than hammering a broken netlink connection.
                size_t tail = queue->count - read;
                memmove(queue->packets + kept, queue->packets + read,
                        tail * sizeof(queue->packets[0]));
                queue->count = kept + tail;
                return false;
            }
        } else {
            queue->packets[kept++] = packet;
        }
    }
    queue->count = kept;
    return true;
}

int linuxLagQueueWait(const LinuxLagQueue *queue, uint64_t now_ms) {
    size_t i;
    int shortest = -1;
    for (i = 0; i < queue->count; ++i) {
        int wait = (int)remaining(&queue->packets[i], &queue->lag, now_ms);
        if (shortest < 0 || wait < shortest) shortest = wait;
    }
    return shortest;
}
