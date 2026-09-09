#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif
#include <string.h>
#include "packet_match.h"
static unsigned word(const unsigned char *p) { return ((unsigned)p[0] << 8) | p[1]; }
bool macPacketMatchPrepare(const CaptureTarget *target, MacPacketMatch *match, char *error) {
    MacPacketMatch next = {0};
    if (!captureTargetValidate(target, error)) return false;
    if (target->native_filter[0]) { strcpy(error, "macOS requires structured traffic selection, not a native Windows filter."); return false; }
    next.traffic = target->traffic;
    if (target->traffic.remote_address[0]) {
        if (inet_pton(AF_INET, target->traffic.remote_address, next.address) == 1) next.address_bytes = 4;
        else if (inet_pton(AF_INET6, target->traffic.remote_address, next.address) == 1) next.address_bytes = 16;
        else { strcpy(error, "Remote address must be a numeric IPv4 or IPv6 address without a scope suffix."); return false; }
    }
    *match = next;
    return true;
}
bool macPacketMatches(const MacPacketMatch *m, const unsigned char *p, size_t n, bool outbound) {
    unsigned type, protocol;
    size_t offset = 14, end, transport;
    const unsigned char *remote;
    int family;
    bool fragmented = false;
    unsigned extensions = 0;
    if ((outbound && m->traffic.direction == TRAFFIC_INBOUND) || (!outbound && m->traffic.direction == TRAFFIC_OUTBOUND) || n < 14) return false;
    type = word(p + 12);
    while (type == 0x8100 || type == 0x88a8) {
        if (++extensions > 2 || n - offset < 4) return false;
        type = word(p + offset + 2); offset += 4;
    }
    if (type == 0x0800) {
        size_t header;
        if (n - offset < 20 || p[offset] >> 4 != 4) return false;
        header = (p[offset] & 15u) * 4u;
        if (header < 20 || header > n - offset || word(p + offset + 2) < header || word(p + offset + 2) > n - offset) return false;
        end = offset + word(p + offset + 2); protocol = p[offset + 9];
        remote = p + offset + (outbound ? 16 : 12); family = 4;
        fragmented = (word(p + offset + 6) & 0x3fffu) != 0;
        transport = offset + header;
    } else if (type == 0x86dd) {
        if (n - offset < 40 || p[offset] >> 4 != 6 || !word(p + offset + 4) || word(p + offset + 4) > n - offset - 40) return false;
        end = offset + 40 + word(p + offset + 4); protocol = p[offset + 6];
        remote = p + offset + (outbound ? 24 : 8); family = 16;
        transport = offset + 40; extensions = 0;
        while (protocol == 0 || protocol == 43 || protocol == 60 || protocol == 44 || protocol == 51) {
            size_t bytes;
            if (++extensions > 16 || end - transport < 8) return false;
            if (protocol == 44) {
                fragmented = (word(p + transport + 2) & 0xfff9u) != 0;
                bytes = 8;
            } else bytes = protocol == 51 ? ((size_t)p[transport + 1] + 2) * 4 : ((size_t)p[transport + 1] + 1) * 8;
            if (bytes > end - transport) return false;
            protocol = p[transport]; transport += bytes;
            if (fragmented) break;
        }
    } else return false;
    if (m->address_bytes && (m->address_bytes != family || memcmp(remote, m->address, (size_t)family))) return false;
    if ((m->traffic.protocol == TRAFFIC_TCP && protocol != 6) || (m->traffic.protocol == TRAFFIC_UDP && protocol != 17)) return false;
    if (m->traffic.remote_port) {
        /* Port-filtered fragmented datagrams are deliberately bypassed in this
         * prototype: associating later fragments needs a bounded flow cache. */
        if (fragmented || end - transport < (protocol == 6 ? 20u : 8u)) return false;
        if (word(p + transport + (outbound ? 2 : 0)) != m->traffic.remote_port) return false;
    }
    return true;
}
