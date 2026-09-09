// Linux-only lab executable, not a NetworkBackend implementation. Its rules are
// deliberately installed/removed by the tester inside a named network namespace.
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink_queue.h>
#include <linux/netlink.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include <libnetfilter_queue/libnetfilter_queue.h>
#include "lag_queue.h"

typedef struct {
    LinuxLagQueue held;
    struct nfq_q_handle *handle;
    bool failed;
    uint64_t accepted;
} Probe;

static volatile sig_atomic_t interrupted;

static void interruptProbe(int signal_number) {
    (void)signal_number;
    interrupted = 1;
}

static uint64_t monotonicMilliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        interrupted = 1;
        return 0;
    }
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static bool acceptPacket(void *context, uint32_t id) {
    Probe *probe = context;
    // A zero-length verdict keeps the kernel's original bytes and checksum /
    // offload metadata. NF_ACCEPT resumes processing; NF_REPEAT would requeue.
    if (nfq_set_verdict(probe->handle, id, NF_ACCEPT, 0, NULL) < 0) return false;
    ++probe->accepted;
    return true;
}

static int receivePacket(struct nfq_q_handle *handle, struct nfgenmsg *message,
                         struct nfq_data *data, void *context) {
    Probe *probe = context;
    struct nfqnl_msg_packet_hdr *header = nfq_get_msg_packet_hdr(data);
    uint32_t id;
    (void)handle;
    (void)message;
    if (!header) { probe->failed = true; return -1; }
    id = ntohl(header->packet_id);
    if (header->hook != NF_INET_LOCAL_IN && header->hook != NF_INET_LOCAL_OUT) {
        // Forwarded traffic has different direction semantics. Never interpret
        // it as inbound by accident. Release it and report the invalid setup.
        (void)acceptPacket(probe, id);
        probe->failed = true;
        return -1;
    }
    if (!linuxLagQueuePush(&probe->held, id, header->hook == NF_INET_LOCAL_OUT,
                           monotonicMilliseconds())) {
        // Overload trades accurate delay for connectivity. Kernel fail-open is
        // also enabled, but neither mechanism guarantees lossless overload.
        if (!acceptPacket(probe, id)) { probe->failed = true; return -1; }
    }
    return 0;
}

static bool inNamedNamespace(const char *name) {
    char path[128];
    struct stat current, named, initial;
    size_t i, length = strlen(name);
    if (!length || length > 48) return false;
    for (i = 0; i < length; ++i) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    snprintf(path, sizeof(path), "/var/run/netns/%s", name);
    return stat("/proc/self/ns/net", &current) == 0 && stat(path, &named) == 0 &&
           stat("/proc/1/ns/net", &initial) == 0 &&
           current.st_dev == named.st_dev && current.st_ino == named.st_ino &&
           !(current.st_dev == initial.st_dev && current.st_ino == initial.st_ino);
}

static bool readPackets(Probe *probe, struct nfq_handle *connection, int fd) {
    // COPY_META avoids payload truncation and does not ask userspace to inspect
    // fragments/GSO frames. Detect truncated netlink messages instead of parsing
    // partial attributes and potentially losing a packet's ID silently.
    union { char bytes[65536]; struct nlmsghdr alignment; } buffer;
    struct iovec vector = {buffer.bytes, sizeof(buffer.bytes)};
    struct msghdr message;
    ssize_t length;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    length = recvmsg(fd, &message, MSG_DONTWAIT);
    if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return true;
    if (length <= 0 || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) return false;
    return nfq_handle_packet(connection, buffer.bytes, (int)length) >= 0 && !probe->failed;
}

static bool parseDelay(const char **position, uint32_t *delay) {
    char *end;
    unsigned long value;
    const char *start = *position;
    while (*start == ' ') ++start;
    if (*start < '0' || *start > '9') return false;
    errno = 0;
    value = strtoul(start, &end, 10);
    if (errno || value > LAG_MAX_MS || (*end && *end != ' ')) return false;
    *position = end;
    *delay = (uint32_t)value;
    return true;
}

static bool command(Probe *probe, const char *line, bool *quit) {
    uint32_t inbound = 0, outbound = 0;
    char error[NETWORK_ERROR_SIZE];
    const char *position = line;
    bool valid_lag = false;
    LagSettings lag = probe->held.lag;
    if (!strncmp(line, "lag ", 4)) {
        position += 4;
        valid_lag = parseDelay(&position, &inbound) && parseDelay(&position, &outbound);
        while (*position == ' ') ++position;
        valid_lag = valid_lag && !*position;
    }
    if (!strcmp(line, "quit")) { *quit = true; return true; }
    if (!strcmp(line, "off")) lag.enabled = false;
    else if (valid_lag) {
        lag.enabled = true;
        lag.inbound = lag.outbound = true;
        lag.inbound_ms = inbound;
        lag.outbound_ms = outbound;
    } else {
        fputs("Use: lag IN_MS OUT_MS (0-15000), off, or quit.\n", stderr);
        return true;
    }
    if (!linuxLagQueueConfigure(&probe->held, &lag, error)) {
        fprintf(stderr, "%s\n", error);
        return false;
    }
    printf("Applied: %u ms inbound, %u ms outbound, %s; held=%zu\n",
           lag.inbound_ms, lag.outbound_ms, lag.enabled ? "enabled" : "disabled", probe->held.count);
    fflush(stdout);
    return true;
}

int main(int argc, char **argv) {
    Probe probe;
    struct nfq_handle *connection = NULL;
    struct pollfd events[2];
    char line[128], *end;
    size_t used = 0;
    bool quit = false, discard_line = false;
    unsigned long queue_number;
    int result = 1, fd;
    struct sigaction action;
    if (argc != 3 || !inNamedNamespace(argv[1])) {
        fputs("Run ONLY in a fresh lab namespace: nfqueue-probe NAMESPACE QUEUE_NUMBER\n", stderr);
        return 1;
    }
    errno = 0;
    queue_number = strtoul(argv[2], &end, 10);
    if (errno || argv[2][0] < '0' || argv[2][0] > '9' || *end || queue_number > UINT16_MAX) return 1;
    memset(&probe, 0, sizeof(probe));
    linuxLagQueueInit(&probe.held);
    memset(&action, 0, sizeof(action));
    action.sa_handler = interruptProbe;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) goto cleanup;
    connection = nfq_open();
    if (!connection) goto cleanup;
    // Do not call nfq_unbind_pf: another queue consumer may own that binding.
    probe.handle = nfq_create_queue(connection, (uint16_t)queue_number, receivePacket, &probe);
    if (!probe.handle) goto cleanup;
    if (nfq_set_mode(probe.handle, NFQNL_COPY_META, 0) < 0 ||
        nfq_set_queue_maxlen(probe.handle, LINUX_LAG_QUEUE_CAPACITY) < 0 ||
        nfq_set_queue_flags(probe.handle, NFQA_CFG_F_FAIL_OPEN | NFQA_CFG_F_GSO,
                            NFQA_CFG_F_FAIL_OPEN | NFQA_CFG_F_GSO) < 0) goto cleanup;
    fd = nfq_fd(connection);
    if (fd < 0) goto cleanup;
    events[0].fd = fd;
    events[0].events = POLLIN;
    events[1].fd = STDIN_FILENO;
    events[1].events = POLLIN;
    puts("Ready (Lag off). Install lab rules now. Commands: lag IN_MS OUT_MS; off; quit.");
    puts("Before quit: remove the lab table, send off, then quit. Existing kernel packets may be lost on a crash.");
    fflush(stdout);
    while (!quit && !interrupted) {
        int status;
        if (!linuxLagQueueProcess(&probe.held, monotonicMilliseconds(), false, acceptPacket, &probe)) goto cleanup;
        status = poll(events, 2, linuxLagQueueWait(&probe.held, monotonicMilliseconds()));
        if (status < 0 && errno == EINTR) continue;
        if (status < 0 || (events[0].revents & (POLLERR | POLLHUP | POLLNVAL))) goto cleanup;
        if ((events[0].revents & POLLIN) && !readPackets(&probe, connection, fd)) goto cleanup;
        if (events[1].revents & (POLLIN | POLLHUP)) {
            char input[128];
            ssize_t count = read(STDIN_FILENO, input, sizeof(input));
            ssize_t i;
            if (count == 0) quit = true;
            else if (count < 0 && errno != EINTR) goto cleanup;
            for (i = 0; i < count; ++i) {
                if (input[i] == '\n') {
                    line[used] = 0;
                    if (!discard_line && !command(&probe, line, &quit)) goto cleanup;
                    used = 0;
                    discard_line = false;
                } else if (input[i] != '\r' && !discard_line) {
                    if (used + 1 < sizeof(line)) line[used++] = input[i];
                    else { discard_line = true; fputs("Command too long; ignored.\n", stderr); }
                }
            }
        }
        if (events[1].revents & (POLLERR | POLLNVAL)) goto cleanup;
    }
    result = interrupted ? 1 : 0;
cleanup:
    if (result) fprintf(stderr, "Probe stopping with an error/interruption%s%s\n",
                        errno ? ": " : ".", errno ? strerror(errno) : "");
    if (probe.handle) {
        // After the tester removes the table, consume remaining notifications
        // without adding delay. The cap prevents a mistaken quit under ongoing
        // traffic from hanging forever. This cannot repair lost notifications.
        if (!result) {
            unsigned passes;
            struct pollfd pending = {nfq_fd(connection), POLLIN, 0};
            probe.held.lag.enabled = false;
            for (passes = 0; passes < 2048; ++passes) {
                int status;
                if (!linuxLagQueueProcess(&probe.held, monotonicMilliseconds(), true, acceptPacket, &probe)) {
                    result = 1;
                    break;
                }
                status = poll(&pending, 1, 0);
                if (status == 0) break;
                if (status < 0 || !(pending.revents & POLLIN) ||
                    !readPackets(&probe, connection, pending.fd)) { result = 1; break; }
            }
            if (passes == 2048) result = 1;
            if (result) fputs("Pending kernel notifications could not be drained.\n", stderr);
        }
        if (!linuxLagQueueProcess(&probe.held, monotonicMilliseconds(), true, acceptPacket, &probe)) {
            fprintf(stderr, "Could not release %zu tracked packets.\n", probe.held.count);
            result = 1;
        }
        // This does NOT guarantee a lossless stop: messages not yet received
        // are outside our list. Production needs owned-rule removal + draining
        // the kernel queue before closing, with observable recovery failures.
        if (nfq_destroy_queue(probe.handle) < 0) result = 1;
    }
    if (connection && nfq_close(connection) < 0) result = 1;
    fprintf(stderr, "Accepted %llu packets. Remove the lab table if it is still installed.\n",
            (unsigned long long)probe.accepted);
    return result;
}
