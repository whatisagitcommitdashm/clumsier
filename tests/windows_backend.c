#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "backends/windows/backend.h"
#include "backends/windows/lag_engine.h"

static DWORD now;
static int timerUsers, starts;
static BOOL running;
static DWORD fakeTime(void) { return now; }
void startTimePeriod(void) { ++timerUsers; }
void endTimePeriod(void) { --timerUsers; }
#define timeGetTime fakeTime
#include "../src/backends/windows/lag_engine.c"
#undef timeGetTime
int divertStart(const char *filter, char *error) { (void)filter; (void)error; ++starts; running = TRUE; return 1; }
void divertStop(void) { running = FALSE; }
BOOL divertIsRunning(void) { return running; }
extern PacketNode * const head, * const tail;
static void packet(BOOL outbound) {
    WINDIVERT_ADDRESS address = {0};
    char byte = 1;
    address.Outbound = outbound;
    appendNode(createNode(&byte, 1, &address));
}
static int drain(void) {
    int count = 0;
    while (!isListEmpty()) { freeNode(popNode(tail->prev)); ++count; }
    return count;
}
int main(void) {
    CaptureTarget target = {0};
    LagSettings lag = {true, true, true, 100, 10};
    char filter[NATIVE_FILTER_SIZE], error[NETWORK_ERROR_SIZE];
    NetworkBackend backend = windowsNetworkBackend();
    const char *parseError;
    UINT position;
    target.traffic.protocol = TRAFFIC_UDP;
    target.traffic.remote_port = 1234;
    strcpy(target.traffic.remote_address, "127.0.0.1");
    assert(windowsBuildFilter(&target, filter, error));
    assert(WinDivertHelperCompileFilter(filter, WINDIVERT_LAYER_NETWORK, NULL, 0, &parseError, &position));
    strcpy(target.traffic.remote_address, "2001:db8::1");
    assert(windowsBuildFilter(&target, filter, error));
    assert(WinDivertHelperCompileFilter(filter, WINDIVERT_LAYER_NETWORK, NULL, 0, &parseError, &position));
    strcpy(target.traffic.remote_address, "1.2.3.4 or true");
    assert(!windowsBuildFilter(&target, filter, error));
    memset(&target, 0, sizeof(target)); strcpy(target.native_filter, "udp and inbound"); strcpy(target.native_backend, "windivert");
    assert(windowsBuildFilter(&target, filter, error) && !strcmp(filter, target.native_filter));
    assert(backend.ops->start(backend.context, &target, &lag, error) && starts == 1);
    assert(backend.ops->is_running(backend.context));
    backend.ops->stop(backend.context); assert(!running);
    puts("PASS Windows backend: native filters, structured IPv4/IPv6 filters and capture delegation");

    initPacketNodeList(); windowsLagStart(); windowsLagConfigure(&lag);
    now = 1000; packet(FALSE); windowsLagProcess(head, tail);
    packet(TRUE); windowsLagProcess(head, tail); assert(drain() == 0);
    now = 1011; windowsLagProcess(head, tail); assert(drain() == 1 && bufSize == 1);
    // A live edit changes the deadline of a packet already in the Lag queue.
    lag.inbound_ms = 5; windowsLagConfigure(&lag);
    windowsLagProcess(head, tail); assert(drain() == 1 && !bufSize);
    lag.inbound_ms = 100; windowsLagConfigure(&lag);
    now = 2000; packet(FALSE); windowsLagProcess(head, tail);
    lag.inbound = false; windowsLagConfigure(&lag);
    windowsLagProcess(head, tail); assert(drain() == 1);
    lag.inbound = true; windowsLagConfigure(&lag);
    packet(FALSE); windowsLagProcess(head, tail); assert(bufSize == 1);
    lag.enabled = false; windowsLagConfigure(&lag);
    windowsLagProcess(head, tail); assert(drain() == 1 && !bufSize);
    lag.enabled = true;
    lag.inbound = true; lag.inbound_ms = 10; windowsLagConfigure(&lag);
    now = 0xfffffff0; packet(FALSE); windowsLagProcess(head, tail);
    now = 5; windowsLagProcess(head, tail); assert(drain() == 1);
    now = 100; packet(FALSE); windowsLagProcess(head, tail);
    // Zero delay bypasses new packets and releases held packets on the same
    // clock tick. The other direction can still have a nonzero delay.
    lag.inbound_ms = 0; windowsLagConfigure(&lag);
    packet(FALSE); windowsLagProcess(head, tail); assert(drain() == 2 && !bufSize);
    packet(FALSE); packet(TRUE); windowsLagProcess(head, tail); assert(drain() == 1 && bufSize == 1);
    windowsLagStop(head, tail); assert(drain() == 1 && !bufSize && !timerUsers);
    puts("PASS Windows Lag: independent directions, live edits, direction disable, timer wrap and stop flush");
    return 0;
}
