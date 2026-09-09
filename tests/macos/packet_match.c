#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "backends/macos/packet_match.h"
static void word(unsigned char *p, unsigned n) { p[0] = (unsigned char)(n >> 8); p[1] = (unsigned char)n; }
int main(void) {
    unsigned char p[128] = {0};
    CaptureTarget target = {0}; MacPacketMatch match, old;
    char error[NETWORK_ERROR_SIZE];
    target.traffic.protocol = TRAFFIC_UDP; target.traffic.direction = TRAFFIC_INBOUND;
    target.traffic.remote_port = 1234; strcpy(target.traffic.remote_address, "192.0.2.1");
    assert(macPacketMatchPrepare(&target, &match, error));
    word(p + 12, 0x800); p[14] = 0x45; word(p + 16, 28); p[23] = 17;
    p[26] = 192; p[28] = 2; p[29] = 1; word(p + 34, 1234);
    assert(macPacketMatches(&match, p, 42, false));
    assert(!macPacketMatches(&match, p, 42, true));
    for (size_t i = 0; i < 42; ++i) assert(!macPacketMatches(&match, p, i, false));
    p[14] = 0x44; assert(!macPacketMatches(&match, p, 42, false)); p[14] = 0x45;
    word(p + 20, 0x2000); assert(!macPacketMatches(&match, p, 42, false)); word(p + 20, 0);
    p[29] = 2; assert(!macPacketMatches(&match, p, 42, false)); p[29] = 1;
    old = match; strcpy(target.traffic.remote_address, "192.0.2.1;bad");
    assert(!macPacketMatchPrepare(&target, &match, error)); assert(!memcmp(&match, &old, sizeof(match)));
    memset(&target, 0, sizeof(target)); strcpy(target.native_backend, "windivert"); strcpy(target.native_filter, "inbound");
    assert(!macPacketMatchPrepare(&target, &match, error));
    memset(&target, 0, sizeof(target)); target.traffic.protocol = TRAFFIC_UDP;
    target.traffic.remote_port = 1234; strcpy(target.traffic.remote_address, "2001:db8::1");
    assert(macPacketMatchPrepare(&target, &match, error));
    memset(p, 0, sizeof(p)); word(p + 12, 0x86dd); p[14] = 0x60;
    word(p + 18, 16); p[20] = 0; /* Hop-by-hop header, then UDP. */
    p[38] = 0x20; p[39] = 1; p[40] = 0x0d; p[41] = 0xb8; p[53] = 1;
    p[54] = 17; word(p + 64, 1234);
    assert(macPacketMatches(&match, p, 70, true));
    assert(!macPacketMatches(&match, p, 70, false));
    p[55] = 10; assert(!macPacketMatches(&match, p, 70, true)); p[55] = 0;
    p[20] = 44; word(p + 56, 1); assert(!macPacketMatches(&match, p, 70, true));
    word(p + 56, 0); assert(macPacketMatches(&match, p, 70, true)); /* Atomic fragment. */
    p[20] = 0;
    memmove(p + 18, p + 14, 56); word(p + 12, 0x8100); word(p + 16, 0x86dd);
    assert(macPacketMatches(&match, p, 74, true));
    puts("macOS packet selection tests passed (synthetic frames only).");
    return 0;
}
