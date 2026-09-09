#include "lag_engine.h"
#define KEEP_AT_MOST 2000
#define FLUSH_WHEN_FULL 800

short windowsLagEnabled;
static SRWLOCK settingsLock = SRWLOCK_INIT;
static LagSettings currentSettings = {false, true, true, 50, 50};
static PacketNode lagHeadNode = {0}, lagTailNode = {0};
static PacketNode *bufHead = &lagHeadNode, *bufTail = &lagTailNode;
static int bufSize;

void windowsLagConfigure(const LagSettings *settings) {
    AcquireSRWLockExclusive(&settingsLock);
    currentSettings = *settings;
    InterlockedExchange16(&windowsLagEnabled, settings->enabled ? 1 : 0);
    ReleaseSRWLockExclusive(&settingsLock);
}

static short isBufEmpty(void) {
    short empty = bufHead->next == bufTail;
    if (empty) assert(bufSize == 0);
    return empty;
}
void windowsLagStart(void) {
    if (bufHead->next == NULL && bufTail->next == NULL) {
        bufHead->next = bufTail;
        bufTail->prev = bufHead;
        bufSize = 0;
    } else {
        assert(isBufEmpty());
    }
    startTimePeriod();
}

void windowsLagStop(PacketNode *head, PacketNode *tail) {
    PacketNode *oldLast = tail->prev;
    UNREFERENCED_PARAMETER(head);
    // flush all buffered packets
    LOG("Closing down lag, flushing %d packets", bufSize);
    while(!isBufEmpty()) {
        insertAfter(popNode(bufTail->prev), oldLast);
        --bufSize;
    }
    endTimePeriod();
}

short windowsLagProcess(PacketNode *head, PacketNode *tail) {
    DWORD currentTime;
    LagSettings settings;
    AcquireSRWLockShared(&settingsLock);
    settings = currentSettings;
    ReleaseSRWLockShared(&settingsLock);
    PacketNode *pac = tail->prev;
    // pick up all packets and fill in the current time
    while (bufSize < KEEP_AT_MOST && pac != head) {
        if (settings.enabled && checkDirection(pac->addr.Outbound, settings.inbound, settings.outbound)) {
            insertAfter(popNode(pac), bufHead)->timestamp = timeGetTime();
            ++bufSize;
            pac = tail->prev;
        } else {
            pac = pac->prev;
        }
    }

    // Held packets follow the active settings, not the settings at arrival.
    // Recompute eligibility from their original timestamps on each pass, so a
    // delay edit changes their remaining wait instead of restarting the clock.
    // Scan both directions: a long inbound delay must not hold outbound packets
    // behind it. Unsigned subtraction also survives the Windows timer wrapping.
    currentTime = timeGetTime();
    pac = bufTail->prev;
    while (pac != bufHead) {
        PacketNode *previous = pac->prev;
        DWORD delay = pac->addr.Outbound ? settings.outbound_ms : settings.inbound_ms;
        BOOL selected = settings.enabled && checkDirection(pac->addr.Outbound, settings.inbound, settings.outbound);
        if (!selected || currentTime - pac->timestamp > delay) {
            insertAfter(popNode(pac), head);
            --bufSize;
        }
        pac = previous;
    }
    // if buffer is full just flush things out
    if (bufSize >= KEEP_AT_MOST) {
        int flushCnt = FLUSH_WHEN_FULL;
        while (flushCnt-- > 0) {
            insertAfter(popNode(bufTail->prev), head);
            --bufSize;
        }
    }

    return bufSize > 0;
}
