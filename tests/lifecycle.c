// Real Windows threads/mutexes, simulated WinDivert: no driver or admin needed.
#include <stdlib.h>
#include <winsock2.h>
#include <windows.h>
#include "common.h"

static HANDLE dataReady, shutdownReady, heldReady;
static volatile LONG pending, sends, opens, closes;
static int failOpen, failParam, failMutex, failEvent, failThread, threadCalls;

static HANDLE fakeOpen(const char *f, WINDIVERT_LAYER l, INT16 p, UINT64 flags) {
    UNREFERENCED_PARAMETER(f); UNREFERENCED_PARAMETER(l);
    UNREFERENCED_PARAMETER(p); UNREFERENCED_PARAMETER(flags);
    if (failOpen) { SetLastError(ERROR_INVALID_PARAMETER); return INVALID_HANDLE_VALUE; }
    dataReady = CreateEvent(NULL, TRUE, FALSE, NULL);
    shutdownReady = CreateEvent(NULL, TRUE, FALSE, NULL);
    assert(dataReady && shutdownReady);
    InterlockedExchange(&pending, 0);
    InterlockedIncrement(&opens);
    return shutdownReady;
}
static BOOL fakeParam(HANDLE h, WINDIVERT_PARAM p, UINT64 v) {
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); UNREFERENCED_PARAMETER(v);
    if (failParam) SetLastError(ERROR_INVALID_PARAMETER);
    return !failParam;
}
static BOOL fakeRecv(HANDLE h, PVOID packet, UINT capacity, UINT *len, WINDIVERT_ADDRESS *addr) {
    HANDLE events[2] = {dataReady, shutdownReady};
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(capacity);
    for (;;) {
        LONG count = InterlockedCompareExchange(&pending, 0, 0);
        if (count > 0 && InterlockedCompareExchange(&pending, count - 1, count) == count) {
            *(char*)packet = 42; *len = 1;
            memset(addr, 0, sizeof(*addr)); addr->Outbound = TRUE;
            return TRUE;
        }
        if (WaitForSingleObject(shutdownReady, 0) == WAIT_OBJECT_0) {
            SetLastError(ERROR_NO_DATA); return FALSE;
        }
        ResetEvent(dataReady);
        // A producer sets the count before signalling the event.
        if (InterlockedCompareExchange(&pending, 0, 0)) continue;
        assert(WaitForMultipleObjects(2, events, FALSE, 5000) != WAIT_TIMEOUT);
    }
}
static BOOL fakeSend(HANDLE h, const VOID *p, UINT len, UINT *sent, const WINDIVERT_ADDRESS *a) {
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); UNREFERENCED_PARAMETER(a);
    *sent = len; InterlockedIncrement(&sends); return TRUE;
}
static BOOL fakeShutdown(HANDLE h, WINDIVERT_SHUTDOWN how) {
    UNREFERENCED_PARAMETER(h); assert(how == WINDIVERT_SHUTDOWN_RECV);
    return SetEvent(shutdownReady);
}
static BOOL fakeClose(HANDLE h) {
    assert(h == shutdownReady);
    CloseHandle(dataReady); CloseHandle(shutdownReady);
    InterlockedIncrement(&closes); return TRUE;
}
static HANDLE testMutex(LPSECURITY_ATTRIBUTES a, BOOL owner, LPCSTR name) {
    if (failMutex) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    return CreateMutexA(a, owner, name);
}
static HANDLE testEvent(LPSECURITY_ATTRIBUTES a, BOOL manual, BOOL initial, LPCSTR name) {
    if (failEvent) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    return CreateEventA(a, manual, initial, name);
}
static HANDLE testThread(LPSECURITY_ATTRIBUTES a, SIZE_T size, LPTHREAD_START_ROUTINE f, LPVOID arg, DWORD flags, LPDWORD id) {
    if (++threadCalls == failThread) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    return CreateThread(a, size, f, arg, flags, id);
}

#define WinDivertOpen fakeOpen
#define WinDivertSetParam fakeParam
#define WinDivertRecv fakeRecv
#define WinDivertSend fakeSend
#define WinDivertShutdown fakeShutdown
#define WinDivertClose fakeClose
#undef CreateMutex
#define CreateMutex testMutex
#undef CreateEvent
#define CreateEvent testEvent
#define CreateThread testThread
#include "../src/backends/windows/legacy/divert.c"
#undef CreateThread
#undef CreateEvent
#undef CreateMutex

volatile short sendState;
static short enabled[MODULE_CNT];
static Module testModules[MODULE_CNT];
Module *modules[MODULE_CNT];
static PacketNode *held;
static int moduleStarts, moduleCloses;
static void moduleStart(void) { ++moduleStarts; }
static short moduleProcess(PacketNode *h, PacketNode *t) {
    if (!held && h->next != t) {
        held = popNode(h->next); SetEvent(heldReady);
    }
    return held != NULL;
}
static void moduleClose(PacketNode *h, PacketNode *t) {
    UNREFERENCED_PARAMETER(h);
    if (held) { insertBefore(held, t); held = NULL; }
    ++moduleCloses;
}
static void checkStopped(void) {
    assert(!running && divertHandle == INVALID_HANDLE_VALUE);
    assert(!loopThread && !clockThread && !mutex && !workersReady);
    assert(opens == closes);
}
static DWORD WINAPI startCaller(LPVOID unused) {
    char error[MSG_BUFSIZE]; UNREFERENCED_PARAMETER(unused);
    assert(divertStart("test", error)); return 0;
}
static DWORD WINAPI stopCaller(LPVOID unused) {
    UNREFERENCED_PARAMETER(unused); divertStop(); return 0;
}
int main(void) {
    char error[MSG_BUFSIZE]; int i; DWORD handlesBefore, handlesAfter;
    HANDLE callers[8];
    heldReady = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (i = 0; i < MODULE_CNT; ++i) {
        modules[i] = &testModules[i];
        modules[i]->enabledFlag = &enabled[i];
        modules[i]->startUp = moduleStart;
        modules[i]->process = moduleProcess;
        modules[i]->closeDown = moduleClose;
    }
    divertStop(); divertStop(); checkStopped();
    puts("PASS stop before start");

    // Each failure must release all acquired resources and permit a retry.
    for (i = 0; i < 6; ++i) {
        enabled[0] = 1;
        failOpen = i == 0; failParam = i == 1; failMutex = i == 2;
        failEvent = i == 3; failThread = i >= 4 ? i - 3 : 0; threadCalls = 0;
        assert(!divertStart("test", error)); checkStopped();
        assert(moduleStarts == 0 && moduleCloses == 0);
        enabled[0] = 0;
        failOpen = failParam = failMutex = failEvent = failThread = 0;
        assert(divertStart("test", error)); divertStop(); checkStopped();
    }
    puts("PASS startup failure at open, queue, mutex, event, and either thread; retry");

    enabled[0] = 1;
    assert(divertStart("test", error));
    InterlockedExchange(&pending, 1); SetEvent(dataReady);
    assert(WaitForSingleObject(heldReady, 5000) == WAIT_OBJECT_0);
    { LONG priorOpens = opens; HANDLE priorHandle = divertHandle;
      assert(divertStart("different filter ignored", error));
      assert(opens == priorOpens && divertHandle == priorHandle); }
    // Disable immediately before Stop: lastEnabled must still drive cleanup.
    InterlockedExchange16(&enabled[0], 0);
    InterlockedExchange(&pending, 3); SetEvent(dataReady);
    divertStop(); divertStop(); checkStopped();
    assert(!held && isListEmpty() && sends == 4);
    assert(moduleStarts == 1 && moduleCloses == 1);
    puts("PASS repeated start preserves buffered packet; stop flushes all packets once");

    { LONG priorOpens = opens;
    for (i = 0; i < 8; ++i) callers[i] = CreateThread(NULL, 0, startCaller, NULL, 0, NULL);
    assert(WaitForMultipleObjects(8, callers, TRUE, 5000) == WAIT_OBJECT_0);
    for (i = 0; i < 8; ++i) CloseHandle(callers[i]);
    assert(opens == priorOpens + 1); }
    for (i = 0; i < 8; ++i) callers[i] = CreateThread(NULL, 0, stopCaller, NULL, 0, NULL);
    assert(WaitForMultipleObjects(8, callers, TRUE, 5000) == WAIT_OBJECT_0);
    for (i = 0; i < 8; ++i) CloseHandle(callers[i]);
    checkStopped();
    puts("PASS concurrent start callers and concurrent stop callers");

    GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
    for (i = 0; i < 50; ++i) {
        assert(divertStart("test", error));
        assert(divertStart("test", error));
        divertStop(); divertStop(); checkStopped();
    }
    GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
    assert(handlesAfter == handlesBefore);
    puts("PASS 50 start/start/stop/stop cycles without handle growth");
    CloseHandle(heldReady);
    return 0;
}
