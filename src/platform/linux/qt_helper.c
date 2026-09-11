#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "backends/linux/backend.h"
#include "core/preset.h"
#include "../../../external/cjson/cJSON.h"

// Private, bounded JSON-lines protocol over the launching process's pipes.
// No socket, filesystem requests, shell commands, or arbitrary nft input.
static volatile sig_atomic_t exiting;
static void interrupt(int sig) { (void)sig; exiting = 1; }
static bool parseLag(const cJSON *value, LagSettings *lag) {
    if (!cJSON_IsArray(value) || cJSON_GetArraySize(value) != 5) return false;
    const cJSON *enabled = cJSON_GetArrayItem(value, 0), *in = cJSON_GetArrayItem(value, 1), *out = cJSON_GetArrayItem(value, 2);
    const cJSON *in_ms = cJSON_GetArrayItem(value, 3), *out_ms = cJSON_GetArrayItem(value, 4);
    if (!cJSON_IsBool(enabled) || !cJSON_IsBool(in) || !cJSON_IsBool(out) ||
        !cJSON_IsNumber(in_ms) || !cJSON_IsNumber(out_ms) ||
        !(in_ms->valuedouble >= 0 && in_ms->valuedouble <= LAG_MAX_MS) ||
        !(out_ms->valuedouble >= 0 && out_ms->valuedouble <= LAG_MAX_MS) ||
        in_ms->valuedouble != in_ms->valueint || out_ms->valuedouble != out_ms->valueint) return false;
    *lag = (LagSettings){cJSON_IsTrue(enabled), cJSON_IsTrue(in), cJSON_IsTrue(out),
                        (uint32_t)in_ms->valueint, (uint32_t)out_ms->valueint};
    return true;
}
static bool reply(bool ok, bool running, const char *error) {
    cJSON *object = cJSON_CreateObject();
    if (!object) return false;
    bool built = cJSON_AddBoolToObject(object, "ok", ok) && cJSON_AddBoolToObject(object, "running", running) &&
        cJSON_AddStringToObject(object, "error", error);
    char *line = built ? cJSON_PrintUnformatted(object) : NULL;
    cJSON_Delete(object);
    if (!line) return false;
    bool sent = puts(line) >= 0 && fflush(stdout) == 0;
    free(line); return sent;
}
int main(void) {
    struct sigaction action = {0};
    action.sa_handler = interrupt;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) return 1;
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, NULL)) return 1;
    char error[NETWORK_ERROR_SIZE] = "";
    LinuxBackend *native = linuxBackendCreate(error);
    if (!native) { reply(false, false, error); return 1; }
    NetworkBackend backend = linuxBackendInterface(native);
    char line[16384]; size_t used = 0;
    while (!exiting) {
        struct pollfd input = {STDIN_FILENO, POLLIN, 0};
        int ready = poll(&input, 1, 100);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0 || (input.revents & (POLLERR | POLLNVAL))) break;
        if (!ready) continue;
        char byte;
        ssize_t count = read(STDIN_FILENO, &byte, 1);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0 || byte == '\0' || used == sizeof(line) - 1) break;
        if (byte != '\n') { line[used++] = byte; continue; }
        line[used] = 0;
        cJSON *request = cJSON_ParseWithLengthOpts(line, used + 1, NULL, 1);
        used = 0; error[0] = 0;
        const cJSON *command = cJSON_GetObjectItemCaseSensitive(request, "command");
        bool ok = false;
        LagSettings lag;
        if (!cJSON_IsString(command)) strcpy(error, "Invalid helper request.");
        else if (!strcmp(command->valuestring, "stop")) { backend.ops->stop(backend.context); ok = true; }
        else if (!strcmp(command->valuestring, "status")) ok = true;
        else if (!strcmp(command->valuestring, "start") || !strcmp(command->valuestring, "apply")) {
            if (!parseLag(cJSON_GetObjectItemCaseSensitive(request, "lag"), &lag)) strcpy(error, "Invalid Lag settings.");
            else if (!strcmp(command->valuestring, "apply")) ok = backend.ops->apply_lag(backend.context, &lag, error);
            else {
                const cJSON *json = cJSON_GetObjectItemCaseSensitive(request, "preset");
                Preset preset;
                if (!cJSON_IsString(json)) strcpy(error, "Missing traffic selection.");
                else if (presetParse(json->valuestring, strlen(json->valuestring), &preset, error))
                    ok = backend.ops->start(backend.context, &preset.target, &lag, error);
            }
        } else strcpy(error, "Unknown helper command.");
        bool running = backend.ops->is_running(backend.context);
        if (!error[0]) linuxBackendLastError(native, error);
        cJSON_Delete(request);
        if (!reply(ok && !error[0], running, error)) break;
    }
    // EOF also covers GUI crashes and closed pipes. Keep the backend's normal
    // quiesce/drain/close/remove ordering for every orderly helper exit.
    linuxBackendDestroy(native);
    return 0;
}
