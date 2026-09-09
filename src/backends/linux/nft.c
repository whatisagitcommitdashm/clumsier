#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "../../../external/cjson/cJSON.h"
#include "nft.h"

static int temporaryFile(void) {
    char path[] = "/tmp/clumsier-nft-XXXXXX";
    int fd = mkstemp(path);
    if (fd >= 0) {
        unlink(path);
        if (fd < 3) {
            int replacement = fcntl(fd, F_DUPFD_CLOEXEC, 3);
            close(fd);
            fd = replacement;
            if (fd < 0) return -1;
        }
        if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) { close(fd); return -1; }
    }
    return fd;
}

bool linuxNftRun(const char *input, char *output, size_t output_size, char *error) {
    static const char *paths[] = {"/usr/sbin/nft", "/sbin/nft", "/usr/bin/nft", "/bin/nft"};
    const char *program = NULL;
    char *environment[] = {"PATH=/usr/sbin:/usr/bin:/sbin:/bin", "LC_ALL=C", NULL};
    char *arguments[] = {NULL, "-j", "-f", "-", NULL};
    posix_spawn_file_actions_t actions;
    int in = -1, out = -1, status = 0, spawn_error;
    pid_t child = -1;
    bool initialized = false, okay = false, finished = false;
    size_t i, length = strlen(input), written = 0;
    struct timespec pause = {0, 10000000};
    error[0] = 0;
    if (output_size) output[0] = 0;
    if (length > 4096) { strcpy(error, "nft input is too long."); return false; }
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i)
        if (access(paths[i], X_OK) == 0) { program = paths[i]; break; }
    if (!program) { strcpy(error, "Install nftables: no nft executable in standard system paths."); return false; }
    in = temporaryFile();
    out = temporaryFile();
    if (in < 0 || out < 0) goto cleanup;
    while (written < length) {
        ssize_t count = write(in, input + written, length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) goto cleanup;
        written += (size_t)count;
    }
    if (lseek(in, 0, SEEK_SET) < 0) goto cleanup;
    if (posix_spawn_file_actions_init(&actions)) goto cleanup;
    initialized = true;
    if (posix_spawn_file_actions_adddup2(&actions, in, STDIN_FILENO) ||
        posix_spawn_file_actions_adddup2(&actions, out, STDOUT_FILENO) ||
        posix_spawn_file_actions_adddup2(&actions, out, STDERR_FILENO)) goto cleanup;
    arguments[0] = (char *)program;
    spawn_error = posix_spawn(&child, program, &actions, NULL, arguments, environment);
    if (spawn_error) { errno = spawn_error; child = -1; goto cleanup; }
    // nft is a short local transaction. Bound a stuck helper; after any failed
    // install the caller queries the private comment before deciding ownership.
    for (i = 0; i < 500; ++i) {
        pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) { finished = true; break; }
        if (waited < 0 && errno != EINTR) break;
        nanosleep(&pause, NULL);
    }
    if (!finished) {
        kill(child, SIGKILL);
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        strcpy(error, "nft helper did not finish; checking whether its transaction applied.");
        goto cleanup;
    }
    if (output_size > 1 && lseek(out, 0, SEEK_SET) >= 0) {
        ssize_t count = read(out, output, output_size - 1);
        if (count >= 0) output[count] = 0;
    }
    okay = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!okay) snprintf(error, NETWORK_ERROR_SIZE, "nft failed: %.430s", output_size ? output : "no output");
cleanup:
    if (!okay && !error[0]) snprintf(error, NETWORK_ERROR_SIZE, "Could not run nft: %s", strerror(errno));
    if (initialized) posix_spawn_file_actions_destroy(&actions);
    if (in >= 0) close(in);
    if (out >= 0) close(out);
    return okay;
}

int linuxNftOwned(const char *table, char *error) {
    char command[128], output[65536];
    cJSON *root, *objects, *entry;
    int found = 0;
    // Listing table names succeeds even when our table disappeared. A failed
    // targeted query cannot distinguish absence from a permission/network error.
    if (!linuxNftRun("list tables\n", output, sizeof(output), error)) return -1;
    root = cJSON_Parse(output);
    objects = root ? cJSON_GetObjectItemCaseSensitive(root, "nftables") : NULL;
    if (!cJSON_IsArray(objects)) { cJSON_Delete(root); strcpy(error, "Invalid nft table-list response."); return -1; }
    cJSON_ArrayForEach(entry, objects) {
        cJSON *value = cJSON_GetObjectItemCaseSensitive(entry, "table");
        cJSON *name = value ? cJSON_GetObjectItemCaseSensitive(value, "name") : NULL;
        cJSON *family = value ? cJSON_GetObjectItemCaseSensitive(value, "family") : NULL;
        if (cJSON_IsString(name) && cJSON_IsString(family) && !strcmp(name->valuestring, table) &&
            !strcmp(family->valuestring, "inet")) found = 1;
    }
    cJSON_Delete(root);
    if (!found) return 0;
    snprintf(command, sizeof(command), "list table inet %s\n", table);
    if (!linuxNftRun(command, output, sizeof(output), error)) return -1;
    root = cJSON_Parse(output);
    objects = root ? cJSON_GetObjectItemCaseSensitive(root, "nftables") : NULL;
    found = -1;
    cJSON_ArrayForEach(entry, objects) {
        cJSON *value = cJSON_GetObjectItemCaseSensitive(entry, "table");
        cJSON *name = value ? cJSON_GetObjectItemCaseSensitive(value, "name") : NULL;
        cJSON *comment = value ? cJSON_GetObjectItemCaseSensitive(value, "comment") : NULL;
        if (cJSON_IsString(name) && cJSON_IsString(comment) && !strcmp(name->valuestring, table) &&
            !strcmp(comment->valuestring, table)) found = 1;
    }
    cJSON_Delete(root);
    if (found < 0) strcpy(error, "Refusing to remove a table without our ownership comment.");
    return found;
}
