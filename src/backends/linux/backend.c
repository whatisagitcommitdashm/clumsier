#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink_queue.h>
#include <linux/netlink.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include <libnetfilter_queue/libnetfilter_queue.h>
#include "backend.h"
#include "lag_queue.h"
#include "nft.h"
#include "rules.h"
#include "session.h"

struct LinuxBackend {
    pthread_mutex_t lock;
    pthread_cond_t ready;
    pthread_t worker;
    bool joinable, started, running, stopping;
    LagSettings requested;
    CaptureTarget target;
    int wake;
    char failure[NETWORK_ERROR_SIZE];
    // Everything below belongs exclusively to the worker while it exists.
    LinuxSession session;
    LinuxLagQueue held;
    struct nfq_handle *connection;
    struct nfq_q_handle *queue;
    uint16_t queue_number;
    char table[LINUX_TABLE_SIZE];
    bool callback_failed;
};

static void recordFailure(LinuxBackend *backend, const char *error);

static uint64_t nowMilliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static bool randomBytes(void *buffer, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        ssize_t count = getrandom((char *)buffer + offset, length - offset, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        offset += (size_t)count;
    }
    return true;
}

static bool acceptPacket(void *context, uint32_t id) {
    LinuxBackend *backend = context;
    // No replacement bytes: the kernel retains checksums, fragments and GSO
    // metadata. NF_ACCEPT continues the hook; no raw reinjection or NF_REPEAT.
    return nfq_set_verdict(backend->queue, id, NF_ACCEPT, 0, NULL) >= 0;
}

static int receivePacket(struct nfq_q_handle *queue, struct nfgenmsg *message,
                         struct nfq_data *data, void *context) {
    LinuxBackend *backend = context;
    struct nfqnl_msg_packet_hdr *header = nfq_get_msg_packet_hdr(data);
    uint32_t id;
    uint64_t now = nowMilliseconds();
    (void)queue;
    (void)message;
    if (!header) { backend->callback_failed = true; return -1; }
    id = ntohl(header->packet_id);
    if (!now || (header->hook != NF_INET_LOCAL_IN && header->hook != NF_INET_LOCAL_OUT)) {
        (void)acceptPacket(backend, id);
        backend->callback_failed = true;
        return -1;
    }
    if (!linuxLagQueuePush(&backend->held, id, header->hook == NF_INET_LOCAL_OUT, now)) {
        // Same bounded-overload policy as the kernel's fail-open flag: preserving
        // connectivity takes precedence over faithfully adding delay under load.
        if (!acceptPacket(backend, id)) { backend->callback_failed = true; return -1; }
    }
    return 0;
}

static void closeQueue(void *context) {
    LinuxBackend *backend = context;
    char error[NETWORK_ERROR_SIZE] = "";
    if (backend->queue && nfq_destroy_queue(backend->queue) < 0) {
        snprintf(error, sizeof(error), "Queue close reported an error: %s", strerror(errno));
        recordFailure(backend, error);
    }
    backend->queue = NULL;
    if (backend->connection && nfq_close(backend->connection) < 0) {
        snprintf(error, sizeof(error), "Netlink close reported an error: %s", strerror(errno));
        recordFailure(backend, error);
    }
    backend->connection = NULL;
    linuxLagQueueInit(&backend->held);
}

static bool openQueue(void *context, char *error) {
    LinuxBackend *backend = context;
    unsigned attempt;
    int fd, socket_buffer = 4 * 1024 * 1024;
    struct timeval send_timeout = {1, 0};
    linuxLagQueueInit(&backend->held);
    backend->callback_failed = false;
    backend->connection = nfq_open();
    if (!backend->connection) goto failure;
    fd = nfq_fd(backend->connection);
    if (fd < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout)) < 0) goto failure;
    // This is best-effort tuning; the kernel may cap SO_RCVBUF. ENOBUFS remains
    // visible and is treated as a failure, rather than silently stranding IDs.
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &socket_buffer, sizeof(socket_buffer));
    for (attempt = 0; attempt < 32; ++attempt) {
        if (!randomBytes(&backend->queue_number, sizeof(backend->queue_number))) goto failure;
        backend->queue = nfq_create_queue(backend->connection, backend->queue_number, receivePacket, backend);
        if (backend->queue) break;
        if (errno != EBUSY && errno != EEXIST) goto failure;
    }
    if (!backend->queue ||
        nfq_set_mode(backend->queue, NFQNL_COPY_META, 0) < 0 ||
        nfq_set_queue_maxlen(backend->queue, LINUX_LAG_QUEUE_CAPACITY) < 0 ||
        nfq_set_queue_flags(backend->queue, NFQA_CFG_F_FAIL_OPEN | NFQA_CFG_F_GSO,
                            NFQA_CFG_F_FAIL_OPEN | NFQA_CFG_F_GSO) < 0) goto failure;
    return true;
failure:
    snprintf(error, NETWORK_ERROR_SIZE, "Could not open NFQUEUE (requires CAP_NET_ADMIN/root): %s", strerror(errno));
    closeQueue(backend);
    return false;
}

static bool installRules(void *context, bool *owned, char *error) {
    LinuxBackend *backend = context;
    char rules[LINUX_RULES_SIZE], output[2048], check[NETWORK_ERROR_SIZE] = "";
    int ownership;
    *owned = false;
    // Never treat a pre-existing table as ours, even if its comment matches.
    ownership = linuxNftOwned(backend->table, error);
    if (ownership != 0) {
        if (ownership > 0) strcpy(error, "Private Linux table name collision; try Start again.");
        return false;
    }
    if (!linuxBuildRules(&backend->target, backend->table, backend->queue_number, rules, error)) return false;
    if (linuxNftRun(rules, output, sizeof(output), error)) { *owned = true; return true; }
    // A helper timeout can occur after an atomic transaction committed. Only
    // claim it after independently finding our unpredictable ownership token.
    ownership = linuxNftOwned(backend->table, check);
    *owned = ownership == 1;
    if (ownership < 0)
        fprintf(stderr, "Linux: cannot determine table ownership after failed install: %s (%s).\n", backend->table, check);
    return false;
}

static bool removeRules(void *context, char *error) {
    LinuxBackend *backend = context;
    char command[128], output[2048];
    int ownership = linuxNftOwned(backend->table, error);
    if (!ownership) return true;
    if (ownership < 0) return false;
    snprintf(command, sizeof(command), "delete table inet %s\n", backend->table);
    return linuxNftRun(command, output, sizeof(output), error);
}

// 1 = handled a message, 0 = no message, -1 = a fatal transport/parsing error.
static int readPackets(LinuxBackend *backend) {
    union { char bytes[65536]; struct nlmsghdr alignment; } buffer;
    struct iovec vector = {buffer.bytes, sizeof(buffer.bytes)};
    struct msghdr message;
    ssize_t count;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    count = recvmsg(nfq_fd(backend->connection), &message, MSG_DONTWAIT);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
    if (count <= 0 || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) return -1;
    if (nfq_handle_packet(backend->connection, buffer.bytes, (int)count) < 0 || backend->callback_failed) return -1;
    return 1;
}

static bool drainQueue(void *context, char *error) {
    LinuxBackend *backend = context;
    unsigned pass;
    bool failed = false;
    // Rules have already been removed if possible. Accepted packets cannot
    // recapture themselves; consume notifications not yet added to our list.
    backend->held.lag.enabled = false;
    backend->callback_failed = false;
    for (pass = 0; pass < 8192; ++pass) {
        int received;
        if (!linuxLagQueueProcess(&backend->held, nowMilliseconds(), true, acceptPacket, backend)) {
            failed = true;
            break;
        }
        received = readPackets(backend);
        if (received < 0) { failed = true; break; }
        if (!received) break;
    }
    if (!linuxLagQueueProcess(&backend->held, nowMilliseconds(), true, acceptPacket, backend)) failed = true;
    if (pass == 8192) failed = true;
    if (failed) snprintf(error, NETWORK_ERROR_SIZE,
        "Linux queue drain failed (%zu tracked IDs remain); closing may lose queued packets.", backend->held.count);
    return !failed;
}

static void recordFailure(LinuxBackend *backend, const char *error) {
    pthread_mutex_lock(&backend->lock);
    snprintf(backend->failure, sizeof(backend->failure), "%s", error);
    pthread_mutex_unlock(&backend->lock);
    fprintf(stderr, "Linux backend: %s\n", error);
}

static void *workerMain(void *context) {
    LinuxBackend *backend = context;
    char error[NETWORK_ERROR_SIZE] = "";
    bool okay = linuxSessionStart(&backend->session, error);
    pthread_mutex_lock(&backend->lock);
    backend->running = okay;
    backend->started = true;
    if (!okay) snprintf(backend->failure, sizeof(backend->failure), "%s", error);
    pthread_cond_broadcast(&backend->ready);
    pthread_mutex_unlock(&backend->lock);
    if (!okay) return NULL;
    for (;;) {
        LagSettings lag;
        struct pollfd events[2];
        uint64_t now;
        int wait, status;
        pthread_mutex_lock(&backend->lock);
        if (backend->stopping) { pthread_mutex_unlock(&backend->lock); break; }
        lag = backend->requested;
        pthread_mutex_unlock(&backend->lock);
        if (!linuxLagQueueConfigure(&backend->held, &lag, error)) break;
        now = nowMilliseconds();
        if (!now || !linuxLagQueueProcess(&backend->held, now, false, acceptPacket, backend)) {
            snprintf(error, sizeof(error), "Clock or packet verdict failed: %s", strerror(errno)); break;
        }
        events[0] = (struct pollfd){backend->wake, POLLIN, 0};
        events[1] = (struct pollfd){nfq_fd(backend->connection), POLLIN, 0};
        wait = linuxLagQueueWait(&backend->held, now);
        if (wait < 0 || wait > 100) wait = 100;
        status = poll(events, 2, wait);
        if (status < 0 && errno == EINTR) continue;
        if (status < 0 || (events[0].revents & (POLLERR | POLLHUP | POLLNVAL)) ||
            (events[1].revents & (POLLERR | POLLHUP | POLLNVAL))) {
            snprintf(error, sizeof(error), "Linux packet polling failed: %s", strerror(errno)); break;
        }
        if (events[0].revents & POLLIN) {
            uint64_t notifications;
            (void)read(backend->wake, &notifications, sizeof(notifications));
            continue; // Observe accepted commands before processing more packets.
        }
        if ((events[1].revents & POLLIN) && readPackets(backend) < 0) {
            snprintf(error, sizeof(error), "NFQUEUE receive failed (possible lost packet notification): %s", strerror(errno)); break;
        }
    }
    pthread_mutex_lock(&backend->lock);
    backend->running = false;
    pthread_mutex_unlock(&backend->lock);
    if (error[0]) recordFailure(backend, error);
    error[0] = 0;
    if (!linuxSessionStop(&backend->session, error)) {
        recordFailure(backend, error);
        if (backend->session.owned)
            fprintf(stderr, "Linux: owned table still needs cleanup: inet %s\n", backend->table);
    }
    return NULL;
}

static bool isRunning(void *context) {
    LinuxBackend *backend = context;
    bool running;
    pthread_mutex_lock(&backend->lock);
    running = backend->running;
    pthread_mutex_unlock(&backend->lock);
    return running;
}

static void stop(void *context) {
    LinuxBackend *backend = context;
    uint64_t wake = 1;
    char error[NETWORK_ERROR_SIZE] = "";
    if (backend->joinable) {
        pthread_mutex_lock(&backend->lock);
        backend->stopping = true;
        (void)write(backend->wake, &wake, sizeof(wake));
        pthread_mutex_unlock(&backend->lock);
        pthread_join(backend->worker, NULL);
        backend->joinable = false;
    }
    // The worker is gone, so the owner may retry cleanup after a transient
    // nft failure. Keep ownership information until deletion really succeeds.
    if (!linuxSessionStop(&backend->session, error)) recordFailure(backend, error);
}

static bool start(void *context, const CaptureTarget *target, const LagSettings *lag, char *error) {
    LinuxBackend *backend = context;
    unsigned char token[16];
    char rules[LINUX_RULES_SIZE];
    size_t i;
    int status;
    if (isRunning(backend)) return true;
    stop(backend);
    if (backend->session.owned) {
        strcpy(error, "Previous Linux table cleanup failed; resolve that error before restarting."); return false;
    }
    if (!lagSettingsValidate(lag, error)) return false;
    if (!randomBytes(token, sizeof(token))) {
        strcpy(error, "Could not obtain a Linux ownership token.");
        return false;
    }
    strcpy(backend->table, "clumsier_");
    for (i = 0; i < sizeof(token); ++i) snprintf(backend->table + 9 + 2 * i, 3, "%02x", token[i]);
    if (!linuxBuildRules(target, backend->table, 0, rules, error)) return false;
    backend->target = *target;
    backend->requested = *lag;
    backend->started = backend->stopping = false;
    backend->failure[0] = 0;
    status = pthread_create(&backend->worker, NULL, workerMain, backend);
    if (status) { snprintf(error, NETWORK_ERROR_SIZE, "Could not create Linux worker: %s", strerror(status)); return false; }
    backend->joinable = true;
    pthread_mutex_lock(&backend->lock);
    while (!backend->started) pthread_cond_wait(&backend->ready, &backend->lock);
    status = backend->running;
    if (!status) snprintf(error, NETWORK_ERROR_SIZE, "%s", backend->failure);
    pthread_mutex_unlock(&backend->lock);
    if (!status) stop(backend);
    return status != 0;
}

static bool applyLag(void *context, const LagSettings *lag, char *error) {
    LinuxBackend *backend = context;
    uint64_t wake = 1;
    LagSettings previous;
    ssize_t count;
    if (!lagSettingsValidate(lag, error)) return false;
    pthread_mutex_lock(&backend->lock);
    if (!backend->running || backend->stopping) {
        snprintf(error, NETWORK_ERROR_SIZE, "%s", backend->failure[0] ? backend->failure : "Linux capture is stopped.");
        pthread_mutex_unlock(&backend->lock);
        return false;
    }
    previous = backend->requested;
    backend->requested = *lag;
    do { count = write(backend->wake, &wake, sizeof(wake)); } while (count < 0 && errno == EINTR);
    if (count < 0 && errno != EAGAIN) {
        backend->requested = previous;
        snprintf(error, NETWORK_ERROR_SIZE, "Could not wake Linux worker: %s", strerror(errno));
        pthread_mutex_unlock(&backend->lock);
        return false;
    }
    pthread_mutex_unlock(&backend->lock);
    return true;
}

LinuxBackend *linuxBackendCreate(char error[NETWORK_ERROR_SIZE]) {
    static const LinuxSessionOps operations = {openQueue, installRules, removeRules, drainQueue, closeQueue};
    LinuxBackend *backend = calloc(1, sizeof(*backend));
    int status;
    if (!backend) { strcpy(error, "Could not allocate Linux backend."); return NULL; }
    status = pthread_mutex_init(&backend->lock, NULL);
    if (status) { free(backend); strcpy(error, "Could not initialize Linux backend lock."); return NULL; }
    status = pthread_cond_init(&backend->ready, NULL);
    if (status) { pthread_mutex_destroy(&backend->lock); free(backend); strcpy(error, "Could not initialize Linux startup signal."); return NULL; }
    backend->wake = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (backend->wake < 0) {
        pthread_cond_destroy(&backend->ready);
        pthread_mutex_destroy(&backend->lock);
        free(backend); strcpy(error, "Could not create Linux worker wakeup."); return NULL;
    }
    backend->session = (LinuxSession){&operations, backend, false, false};
    return backend;
}

NetworkBackend linuxBackendInterface(LinuxBackend *backend) {
    static const NetworkBackendOps operations = {start, applyLag, stop, isRunning};
    return (NetworkBackend){&operations, backend};
}

void linuxBackendLastError(LinuxBackend *backend, char error[NETWORK_ERROR_SIZE]) {
    pthread_mutex_lock(&backend->lock);
    snprintf(error, NETWORK_ERROR_SIZE, "%s", backend->failure);
    pthread_mutex_unlock(&backend->lock);
}

void linuxBackendDestroy(LinuxBackend *backend) {
    if (!backend) return;
    stop(backend);
    if (backend->session.owned)
        fprintf(stderr, "Linux: could not remove owned table inet %s; queue is closed and bypass is active.\n", backend->table);
    close(backend->wake);
    pthread_cond_destroy(&backend->ready);
    pthread_mutex_destroy(&backend->lock);
    free(backend);
}
