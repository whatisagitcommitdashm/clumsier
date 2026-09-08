#include <stdlib.h>
#include <memory.h>
#include <winsock2.h>
#include <Ws2tcpip.h>
#include "windivert.h"
#include "common.h"
#define DIVERT_PRIORITY 0
#define MAX_PACKETSIZE 0xFFFF
#define READ_TIME_PER_STEP 3
// FIXME does this need to be larger then the time to process the list?
#define CLOCK_WAITMS 40
#define QUEUE_LEN 2 << 10
#define QUEUE_TIME 2 << 9 

static HANDLE divertHandle = INVALID_HANDLE_VALUE;
static volatile LONG stopLooping;
static HANDLE loopThread, clockThread, mutex, workersReady;
// Only lifecycle callers take this lock; workers must never acquire it.
static SRWLOCK lifecycleLock = SRWLOCK_INIT;
static BOOL running;

static DWORD WINAPI divertReadLoop(LPVOID arg);
static DWORD WINAPI divertClockLoop(LPVOID arg);

static BOOL stopping(void) {
    return InterlockedCompareExchange(&stopLooping, 0, 0) != 0;
}

static void closeWorkerHandles(void) {
    if (loopThread) CloseHandle(loopThread);
    if (clockThread) CloseHandle(clockThread);
    if (mutex) CloseHandle(mutex);
    if (workersReady) CloseHandle(workersReady);
    loopThread = clockThread = mutex = workersReady = NULL;
}

// not to put these in common.h since modules shouldn't see these
extern PacketNode * const head;
extern PacketNode * const tail;

#ifdef _DEBUG
PWINDIVERT_IPHDR dbg_ip_header;
PWINDIVERT_IPV6HDR dbg_ipv6_header;
PWINDIVERT_TCPHDR dbg_tcp_header;
PWINDIVERT_UDPHDR dbg_udp_header;
PWINDIVERT_ICMPHDR dbg_icmp_header;
PWINDIVERT_ICMPV6HDR dbg_icmpv6_header;
UINT payload_len;
void dumpPacket(char *buf, int len, PWINDIVERT_ADDRESS paddr) {
    char *protocol;
    UINT16 srcPort = 0, dstPort = 0;

    WinDivertHelperParsePacket(buf, len, &dbg_ip_header, &dbg_ipv6_header, NULL,
        &dbg_icmp_header, &dbg_icmpv6_header, &dbg_tcp_header, &dbg_udp_header,
        NULL, &payload_len, NULL, NULL);
    // need to cast byte order on port numbers
    if (dbg_tcp_header != NULL) {
        protocol = "TCP ";
        srcPort = ntohs(dbg_tcp_header->SrcPort);
        dstPort = ntohs(dbg_tcp_header->DstPort);
    } else if (dbg_udp_header != NULL) {
        protocol = "UDP ";
        srcPort = ntohs(dbg_udp_header->SrcPort);
        dstPort = ntohs(dbg_udp_header->DstPort);
    } else if (dbg_icmp_header || dbg_icmpv6_header) {
        protocol = "ICMP";
    } else {
        protocol = "???";
    }

    if (dbg_ip_header != NULL) {
        UINT8 *src_addr = (UINT8*)&dbg_ip_header->SrcAddr;
        UINT8 *dst_addr = (UINT8*)&dbg_ip_header->DstAddr;
        LOG("%s.%s: %u.%u.%u.%u:%d->%u.%u.%u.%u:%d",
            protocol,
            paddr->Outbound ? "OUT " : "IN  ",
            src_addr[0], src_addr[1], src_addr[2], src_addr[3], srcPort,
            dst_addr[0], dst_addr[1], dst_addr[2], dst_addr[3], dstPort);
    } else if (dbg_ipv6_header != NULL) {
        UINT16 *src_addr6 = (UINT16*)&dbg_ipv6_header->SrcAddr;
        UINT16 *dst_addr6 = (UINT16*)&dbg_ipv6_header->DstAddr;
        LOG("%s.%s: %x:%x:%x:%x:%x:%x:%x:%x:%d->%x:%x:%x:%x:%x:%x:%x:%x:%d",
            protocol,
            paddr->Outbound ? "OUT " : "IN  ",
            src_addr6[0], src_addr6[1], src_addr6[2], src_addr6[3],
            src_addr6[4], src_addr6[5], src_addr6[6], src_addr6[7], srcPort,
            dst_addr6[0], dst_addr6[1], dst_addr6[2], dst_addr6[3],
            dst_addr6[4], dst_addr6[5], dst_addr6[6], dst_addr6[7], dstPort);
    }
}
#else
#define dumpPacket(x, y, z)
#endif

int divertStart(const char *filter, char buf[]) {
    int ix;

    AcquireSRWLockExclusive(&lifecycleLock);
    if (running) {
        ReleaseSRWLockExclusive(&lifecycleLock);
        return TRUE; // Start is idempotent: preserve the current session.
    }

    divertHandle = WinDivertOpen(filter, WINDIVERT_LAYER_NETWORK, DIVERT_PRIORITY, 0);
    if (divertHandle == INVALID_HANDLE_VALUE) {
        DWORD lastError = GetLastError();
        if (lastError == ERROR_INVALID_PARAMETER) {
            strcpy(buf, "Failed to start filtering : filter syntax error.");
        } else {
            sprintf(buf, "Failed to start filtering : failed to open device (code:%lu).\n"
                "Make sure you run clumsy as Administrator.", lastError);
        }
        goto failed;
    }
    LOG("Divert opened handle.");

    if (!WinDivertSetParam(divertHandle, WINDIVERT_PARAM_QUEUE_LENGTH, QUEUE_LEN) ||
        !WinDivertSetParam(divertHandle, WINDIVERT_PARAM_QUEUE_TIME, QUEUE_TIME)) {
        sprintf(buf, "Failed to configure capture queue (%lu)", GetLastError());
        goto failed;
    }
    LOG("WinDivert internal queue Len: %d, queue time: %d", QUEUE_LEN, QUEUE_TIME);

    // init package link list
    initPacketNodeList();

    // reset module
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        modules[ix]->lastEnabled = 0;
    }

    // kick off the loop
    LOG("Creating threads and mutex...");
    InterlockedExchange(&stopLooping, FALSE);
    mutex = CreateMutex(NULL, FALSE, NULL);
    if (mutex == NULL) {
        sprintf(buf, "Failed to create mutex (%lu)", GetLastError());
        goto failed;
    }

    workersReady = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!workersReady) {
        sprintf(buf, "Failed to create worker startup event (%lu)", GetLastError());
        goto failed;
    }
    loopThread = CreateThread(NULL, 0, divertReadLoop, NULL, 0, NULL);
    if (loopThread == NULL) {
        sprintf(buf, "Failed to create recv loop thread (%lu)", GetLastError());
        goto failed;
    }
    clockThread = CreateThread(NULL, 0, divertClockLoop, NULL, 0, NULL);
    if (clockThread == NULL) {
        sprintf(buf, "Failed to create clock loop thread (%lu)", GetLastError());
        goto failed;
    }

    LOG("Threads created");

    running = TRUE;
    SetEvent(workersReady);
    ReleaseSRWLockExclusive(&lifecycleLock);
    return TRUE;

failed:
    // A partially created worker cannot touch packets until this gate opens.
    InterlockedExchange(&stopLooping, TRUE);
    if (workersReady) SetEvent(workersReady);
    if (loopThread) WaitForSingleObject(loopThread, INFINITE);
    if (clockThread) WaitForSingleObject(clockThread, INFINITE);
    if (divertHandle != INVALID_HANDLE_VALUE) WinDivertClose(divertHandle);
    divertHandle = INVALID_HANDLE_VALUE;
    closeWorkerHandles();
    ReleaseSRWLockExclusive(&lifecycleLock);
    return FALSE;
}

static int sendAllListPackets() {
    // send packet from tail to head and remove sent ones
    int sendCount = 0;
    UINT sendLen;
    PacketNode *pnode;
#ifdef _DEBUG
    // check the list is good
    // might go into dead loop but it's better for debugging
    PacketNode *p = head;
    do {
        p = p->next;
    } while (p->next);
    assert(p == tail);
#endif

    while (!isListEmpty()) {
        pnode = popNode(tail->prev);
        sendLen = 0;
        assert(pnode != head);
        // FIXME inbound injection on any kind of packet is failing with a very high percentage
        //       need to contact windivert auther and wait for next release
        if (!WinDivertSend(divertHandle, pnode->packet, pnode->packetLen, &sendLen, &(pnode->addr))) {
            PWINDIVERT_ICMPHDR icmp_header;
            PWINDIVERT_ICMPV6HDR icmpv6_header;
            PWINDIVERT_IPHDR ip_header;
            PWINDIVERT_IPV6HDR ipv6_header;
            LOG("Failed to send a packet. (%lu)", GetLastError());
            dumpPacket(pnode->packet, pnode->packetLen, &(pnode->addr));
            // as noted in windivert help, reinject inbound icmp packets some times would fail
            // workaround this by resend them as outbound
            // TODO not sure is this even working as can't find a way to test
            //      need to document about this
            WinDivertHelperParsePacket(pnode->packet, pnode->packetLen, &ip_header, &ipv6_header, NULL,
                &icmp_header, &icmpv6_header, NULL, NULL, NULL, NULL, NULL, NULL);
            if ((icmp_header || icmpv6_header) && !pnode->addr.Outbound) {
                BOOL resent;
                pnode->addr.Outbound = TRUE;
                if (ip_header) {
                    UINT32 tmp = ip_header->SrcAddr;
                    ip_header->SrcAddr = ip_header->DstAddr;
                    ip_header->DstAddr = tmp;
                } else if (ipv6_header) {
                    UINT32 tmpArr[4];
                    memcpy(tmpArr, ipv6_header->SrcAddr, sizeof(tmpArr));
                    memcpy(ipv6_header->SrcAddr, ipv6_header->DstAddr, sizeof(tmpArr));
                    memcpy(ipv6_header->DstAddr, tmpArr, sizeof(tmpArr));
                }
                resent = WinDivertSend(divertHandle, pnode->packet, pnode->packetLen, &sendLen, &(pnode->addr));
                LOG("Resend failed inbound ICMP packets as outbound: %s", resent ? "SUCCESS" : "FAIL");
                InterlockedExchange16(&sendState, SEND_STATUS_SEND);
            } else {
                InterlockedExchange16(&sendState, SEND_STATUS_FAIL);
            }
        } else {
            if (sendLen < pnode->packetLen) {
                // TODO don't know how this can happen, or it needs to be resent like good old UDP packet
                LOG("Internal Error: DivertSend truncated send packet.");
                InterlockedExchange16(&sendState, SEND_STATUS_FAIL);
            } else {
                InterlockedExchange16(&sendState, SEND_STATUS_SEND);
            }
        }


        freeNode(pnode);
        ++sendCount;
    }
    assert(isListEmpty()); // all packets should be sent by now

    return sendCount;
}

// step function to let module process and consume all packets on the list
static void divertConsumeStep() {
#ifdef _DEBUG
    DWORD startTick = GetTickCount(), dt;
#endif
    int ix, cnt;
    // use lastEnabled to keep track of module starting up and closing down
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        Module *module = modules[ix];
        if (*(module->enabledFlag)) {
            if (!module->lastEnabled) {
                module->startUp();
                module->lastEnabled = 1;
            }
            if (module->process(head, tail)) {
                InterlockedIncrement16(&(module->processTriggered));
            }
        } else {
            if (module->lastEnabled) {
                module->closeDown(head, tail);
                module->lastEnabled = 0;
            }
        }
    }
    cnt = sendAllListPackets();
#ifdef _DEBUG
    dt =  GetTickCount() - startTick;
    if (dt > CLOCK_WAITMS / 2) {
        LOG("Costy consume step: %lu ms, sent %d packets", GetTickCount() - startTick, cnt);
    }
#endif
}

// periodically try to consume packets to keep the network responsive and not blocked by recv
static DWORD WINAPI divertClockLoop(LPVOID arg) {
    UNREFERENCED_PARAMETER(arg);
    WaitForSingleObject(workersReady, INFINITE);
    while (!stopping()) {
        DWORD waitResult = WaitForSingleObject(mutex, CLOCK_WAITMS);
        if (waitResult == WAIT_OBJECT_0) {
            if (!stopping()) divertConsumeStep();
            ReleaseMutex(mutex);
        } else if (waitResult != WAIT_TIMEOUT) {
            if (waitResult == WAIT_ABANDONED) ReleaseMutex(mutex);
            InterlockedExchange(&stopLooping, TRUE);
            return 1;
        }
        Sleep(CLOCK_WAITMS);
    }
    return 0;
}
static DWORD WINAPI divertReadLoop(LPVOID arg) {
    char packetBuf[MAX_PACKETSIZE];
    WINDIVERT_ADDRESS addrBuf;
    UINT readLen;
    PacketNode *pnode;
    DWORD waitResult;

    UNREFERENCED_PARAMETER(arg);

    WaitForSingleObject(workersReady, INFINITE);
    if (stopping()) return 0;
    for(;;) {
        if (!WinDivertRecv(divertHandle, packetBuf, MAX_PACKETSIZE, &readLen, &addrBuf)) {
            DWORD lastError = GetLastError();
            if (lastError == ERROR_NO_DATA || lastError == ERROR_INVALID_HANDLE || lastError == ERROR_OPERATION_ABORTED) {
                // treat closing handle as quit
                LOG("Handle died or operation aborted. Exit loop.");
                return 0;
            }
            LOG("Failed to recv a packet. (%lu)", GetLastError());
            if (stopping()) return 1;
            continue;
        }
        if (readLen > MAX_PACKETSIZE) {
            // don't know how this can happen
            LOG("Internal Error: DivertRecv truncated recv packet."); 
        }

        //dumpPacket(packetBuf, readLen, &addrBuf);  

        waitResult = WaitForSingleObject(mutex, INFINITE);
        switch(waitResult) {
            case WAIT_OBJECT_0:
                /***************** enter critical region ************************/
                // create node and put it into the list
                assert(isListEmpty());
                pnode = createNode(packetBuf, readLen, &addrBuf);
                appendNode(pnode);
                // Retain this in-flight packet until Stop flushes module buffers.
                // Stop drains the remainder of the driver's queue after we exit.
                if (stopping()) {
                    ReleaseMutex(mutex);
                    return 0;
                }
                divertConsumeStep();
                /***************** leave critical region ************************/
                if (!ReleaseMutex(mutex)) {
                    LOG("Fatal: Failed to release mutex (%lu)", GetLastError());
                    ABORT();
                }
                break;
            case WAIT_TIMEOUT:
                LOG("Acquire timeout, dropping one read packet");
                continue;
                break;
            case WAIT_ABANDONED:
                LOG("Acquire abandoned.");
                return 0;
            case WAIT_FAILED:
                LOG("Acquire failed.");
                return 0;
        }
    }
}

void divertStop() {
    int ix;
    BOOL captureClosed = FALSE;
    AcquireSRWLockExclusive(&lifecycleLock);
    if (!running) {
        ReleaseSRWLockExclusive(&lifecycleLock);
        return;
    }

    LOG("Stopping...");
    InterlockedExchange(&stopLooping, TRUE);
    // Stop new captures and wake a receiver blocked with no traffic.
    if (!WinDivertShutdown(divertHandle, WINDIVERT_SHUTDOWN_RECV)) {
        LOG("Receive shutdown failed (%lu); closing capture to unblock receiver", GetLastError());
        WinDivertClose(divertHandle);
        captureClosed = TRUE;
    }
    WaitForSingleObject(loopThread, INFINITE);
    WaitForSingleObject(clockThread, INFINITE);

    // Neither worker can access the lists now. Close only modules that started,
    // even if the UI has just switched their enabled flag off.
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        Module *module = modules[ix];
        if (module->lastEnabled) {
            module->closeDown(head, tail);
            module->lastEnabled = 0;
        }
    }
    sendAllListPackets();
    if (!captureClosed) {
        // Shutdown leaves the driver's existing queue readable. Bypass effects
        // for these final packets rather than discarding them on handle close.
        char packet[MAX_PACKETSIZE];
        WINDIVERT_ADDRESS addr;
        UINT len, sent;
        while (WinDivertRecv(divertHandle, packet, sizeof(packet), &len, &addr)) {
            if (!WinDivertSend(divertHandle, packet, len, &sent, &addr)) {
                LOG("Failed to send queued packet during stop (%lu)", GetLastError());
            }
        }
        WinDivertClose(divertHandle);
    }
    divertHandle = INVALID_HANDLE_VALUE;
    closeWorkerHandles();
    running = FALSE;
    ReleaseSRWLockExclusive(&lifecycleLock);

    LOG("Successfully waited threads and stopped.");
}
