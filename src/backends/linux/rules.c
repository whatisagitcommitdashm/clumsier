#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define parseAddress InetPtonA
#define printAddress InetNtopA
#else
#include <arpa/inet.h>
#define parseAddress inet_pton
#define printAddress inet_ntop
#endif
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "rules.h"

static bool append(char *text, size_t *used, const char *format, ...) {
    int count;
    va_list arguments;
    va_start(arguments, format);
    count = vsnprintf(text + *used, LINUX_RULES_SIZE - *used, format, arguments);
    va_end(arguments);
    if (count < 0 || (size_t)count >= LINUX_RULES_SIZE - *used) return false;
    *used += (size_t)count;
    return true;
}

bool linuxBuildRules(const CaptureTarget *target, const char *table, uint16_t queue,
                     char rules[LINUX_RULES_SIZE], char error[NETWORK_ERROR_SIZE]) {
    char result[LINUX_RULES_SIZE], address[INET6_ADDRSTRLEN] = "";
    union { struct in_addr v4; struct in6_addr v6; } parsed;
    int family = 0;
    size_t used = 0, i;
    const TrafficSelector *selection = &target->traffic;
    if (!captureTargetValidate(target, error)) return false;
    if (target->native_filter[0]) {
        strcpy(error, "Linux does not support native filter strings; use structured traffic selection.");
        return false;
    }
    if (strncmp(table, "clumsier_", 9) || strlen(table) >= LINUX_TABLE_SIZE) {
        strcpy(error, "Invalid private Linux table name."); return false;
    }
    for (i = 9; table[i]; ++i) if (!((table[i] >= '0' && table[i] <= '9') ||
                                    (table[i] >= 'a' && table[i] <= 'f'))) {
        strcpy(error, "Invalid private Linux table token."); return false;
    }
    if (i == 9) { strcpy(error, "Missing private Linux table token."); return false; }
    if (selection->remote_address[0]) {
        if (parseAddress(AF_INET, selection->remote_address, &parsed.v4) == 1) family = AF_INET;
        else if (parseAddress(AF_INET6, selection->remote_address, &parsed.v6) == 1) family = AF_INET6;
        else { strcpy(error, "Remote address must be a numeric IPv4 or IPv6 address."); return false; }
        if (!printAddress(family, &parsed, address, sizeof(address))) {
            strcpy(error, "Could not format the numeric remote address."); return false;
        }
    }
    // One transaction: an existing table makes CREATE fail, so no existing
    // rules are touched. Its comment is checked before any cleanup deletion.
    if (!append(result, &used, "create table inet %s { comment \"%s\"; }\n", table, table)) goto full;
    for (i = 0; i < 2; ++i) {
        bool outbound = i != 0;
        const char *chain = outbound ? "outgoing" : "incoming";
        const char *protocol = selection->protocol == TRAFFIC_TCP ? "tcp" : "udp";
        if ((outbound && selection->direction == TRAFFIC_INBOUND) ||
            (!outbound && selection->direction == TRAFFIC_OUTBOUND)) continue;
        if (!append(result, &used,
            "add chain inet %s %s { type filter hook %s priority 0; policy accept; }\n"
            "add rule inet %s %s ", table, chain, outbound ? "output" : "input", table, chain)) goto full;
        if (family && !append(result, &used, "%s %s %s ", family == AF_INET ? "ip" : "ip6",
                              outbound ? "daddr" : "saddr", address)) goto full;
        // meta l4proto understands IPv6 extension headers; ip6 nexthdr would
        // accidentally narrow TCP/UDP selection to packets without extensions.
        if (selection->protocol != TRAFFIC_ANY && !append(result, &used, "meta l4proto %s ", protocol)) goto full;
        if (selection->remote_port && !append(result, &used, "%s %s %u ", protocol,
                         outbound ? "dport" : "sport", (unsigned)selection->remote_port)) goto full;
        if (!append(result, &used, "counter queue num %u bypass\n", (unsigned)queue)) goto full;
    }
    memcpy(rules, result, used + 1);
    return true;
full:
    strcpy(error, "Linux rules exceeded their bounded buffer."); return false;
}
