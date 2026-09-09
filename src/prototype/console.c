#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <poll.h>
#include <unistd.h>
#endif
#include "core/controller.h"
#include "console.h"

#define CONSOLE_LINE_SIZE 4096

typedef struct {
    AppController app;
    bool has_baseline;
    uint32_t baseline;
} Console;

static volatile sig_atomic_t interrupted;
static void interruptConsole(int number) { (void)number; interrupted = 1; }

static const char *skipSpaces(const char *text) {
    while (isspace((unsigned char)*text)) ++text;
    return text;
}

/* Reject signs, overflow, and suffixes instead of accepting a partial number. */
static bool number(const char **text, uint32_t maximum, uint32_t *value) {
    char *end;
    unsigned long parsed;
    const char *start = skipSpaces(*text);
    if (*start < '0' || *start > '9') return false;
    errno = 0;
    parsed = strtoul(start, &end, 10);
    if (errno || parsed > maximum || (*end && !isspace((unsigned char)*end))) return false;
    *text = end;
    *value = (uint32_t)parsed;
    return true;
}

static bool load(Console *console, const char *path, char *error) {
    FILE *file;
    char *json;
    size_t length;
    Preset preset;
    bool ok;
    file = fopen(path, "rb");
    if (!file) {
        snprintf(error, NETWORK_ERROR_SIZE, "Cannot open preset: %s", strerror(errno));
        return false;
    }
    json = malloc(PRESET_JSON_MAX + 1u);
    if (!json) { fclose(file); strcpy(error, "Not enough memory to read the preset."); return false; }
    /* Read one extra byte to detect oversized files without trusting file size
     * metadata. Parsing and controller loading both preserve state on failure. */
    length = fread(json, 1, PRESET_JSON_MAX + 1u, file);
    ok = !ferror(file);
    if (fclose(file)) ok = false;
    if (!ok) strcpy(error, "Could not read the complete preset file.");
    else if (length > PRESET_JSON_MAX) { strcpy(error, "Preset file exceeds 256 KiB."); ok = false; }
    else ok = presetParse(json, length, &preset, error);
    free(json);
    return ok && controllerLoadPreset(&console->app, &preset,
                                     console->has_baseline, console->baseline, error);
}

static void status(const Console *console, FILE *output) {
    const AppController *app = &console->app;
    fprintf(output, "%s | %s\n", controllerIsRunning(app) ? "Running" : "Stopped",
            app->preset_loaded ? app->preset.name : "Manual delay");
    if (app->preset_loaded) {
        const PresetStep *step = &app->preset.steps[app->active_step];
        fprintf(output, "Step %u/%u: %s\n", (unsigned)app->active_step + 1,
                (unsigned)app->preset.step_count, step->name);
        if (app->preset.mode == PRESET_TARGET_PING) {
            if (step->kind == STEP_LOWEST) fputs("Lowest available", output);
            else fprintf(output, "Target: %u ms", (unsigned)step->target_ms);
            fprintf(output, " | Expected: ~%u ms | Baseline: %u ms\n",
                    (unsigned)app->step_result.estimated_ping_ms, (unsigned)app->baseline_ms);
            if (app->step_result.below_baseline)
                fputs("This target is below your baseline; no delay is added.\n", output);
        }
        if (step->note[0]) fprintf(output, "%s\n", step->note);
    }
    fprintf(output, "Added delay: inbound %u ms, outbound %u ms%s\n",
            (unsigned)(app->lag.enabled && app->lag.inbound ? app->lag.inbound_ms : 0),
            (unsigned)(app->lag.enabled && app->lag.outbound ? app->lag.outbound_ms : 0),
            controllerIsRunning(app) ? "" : " (applies after Start)");
}

static void help(FILE *output) {
    fputs("Commands:\n"
          "  load PATH             Load shared preset JSON; does not start capture\n"
          "  baseline MS|none      Set your server's normal ping while stopped\n"
          "  start | stop | toggle Control capture\n"
          "  next | previous | reset\n"
          "                        Move through the loaded preset\n"
          "  delay IN_MS OUT_MS    Use manual delay with the current traffic selection\n"
          "  status | help | quit\n"
          "Delays are 0-15000 ms per direction. Baseline is 0-60000 ms.\n"
          "New manual sessions select both traffic directions. Presets supply their own selection.\n",
          output);
}

static bool command(Console *console, char *line, FILE *output, bool *quit, char *error) {
    char *verb = (char *)skipSpaces(line), *tail = verb;
    const char *arguments;
    AppAction action;
    while (*tail && !isspace((unsigned char)*tail)) ++tail;
    if (*tail) *tail++ = 0;
    arguments = skipSpaces(tail);
    if (!*verb) return true;
    if (!strcmp(verb, "load")) {
        /* The rest of the line is the path, so spaces need no shell escaping. */
        char *path = (char *)arguments;
        size_t length = strlen(path);
        while (length && isspace((unsigned char)path[length - 1])) path[--length] = 0;
        if (length >= 2 && ((path[0] == '"' && path[length - 1] == '"') ||
                            (path[0] == '\'' && path[length - 1] == '\''))) {
            path[length - 1] = 0;
            ++path;
        }
        if (!*path) { strcpy(error, "Use: load PATH"); return false; }
        if (!load(console, path, error)) return false;
    } else if (!strcmp(verb, "baseline")) {
        uint32_t baseline = 0;
        bool has_baseline = strcmp(arguments, "none") != 0;
        if (controllerIsRunning(&console->app)) {
            strcpy(error, "Stop capture before changing your baseline."); return false;
        }
        if (has_baseline && (!number(&arguments, PING_MAX_MS, &baseline) || *skipSpaces(arguments))) {
            strcpy(error, "Use: baseline MS (0-60000), or baseline none"); return false;
        }
        if (console->app.preset_loaded &&
            !controllerLoadPreset(&console->app, &console->app.preset, has_baseline, baseline, error)) return false;
        console->has_baseline = has_baseline;
        console->baseline = baseline;
        fputs("Baseline changed. A loaded sequence returns to its first step.\n", output);
    } else if (!strcmp(verb, "delay")) {
        LagSettings lag = {true, true, true, 0, 0};
        TrafficDirection direction = console->app.target.traffic.direction;
        if (!number(&arguments, LAG_MAX_MS, &lag.inbound_ms) ||
            !number(&arguments, LAG_MAX_MS, &lag.outbound_ms) || *skipSpaces(arguments)) {
            strcpy(error, "Use: delay IN_MS OUT_MS (each 0-15000)"); return false;
        }
        if ((direction == TRAFFIC_INBOUND && lag.outbound_ms) ||
            (direction == TRAFFIC_OUTBOUND && lag.inbound_ms)) {
            strcpy(error, "The current traffic selection does not capture that delay direction."); return false;
        }
        lag.enabled = lag.inbound_ms || lag.outbound_ms;
        if (!controllerSetLag(&console->app, &lag, error)) return false;
    } else {
        if (*arguments) { strcpy(error, "This command takes no arguments."); return false; }
        if (!strcmp(verb, "help")) { help(output); return true; }
        if (!strcmp(verb, "quit")) { *quit = true; return true; }
        if (!strcmp(verb, "status")) { status(console, output); return true; }
        if (!strcmp(verb, "start")) action = ACTION_START_CAPTURE;
        else if (!strcmp(verb, "stop")) action = ACTION_STOP_CAPTURE;
        else if (!strcmp(verb, "toggle")) action = ACTION_TOGGLE_CAPTURE;
        else if (!strcmp(verb, "next")) action = ACTION_NEXT_STEP;
        else if (!strcmp(verb, "previous")) action = ACTION_PREVIOUS_STEP;
        else if (!strcmp(verb, "reset")) action = ACTION_RESET_SEQUENCE;
        else { strcpy(error, "Unknown command. Type help for the available commands."); return false; }
        if (!controllerExecute(&console->app, action, error)) return false;
    }
    status(console, output);
    return true;
}

static int readLine(FILE *input, char *line, size_t capacity, bool *invalid) {
    size_t used = 0;
    *invalid = false;
    for (;;) {
        int character;
        if (interrupted) return 0;
#ifndef _WIN32
        if (input == stdin) {
            unsigned char byte;
            struct pollfd pending = {STDIN_FILENO, POLLIN, 0};
            int ready = poll(&pending, 1, 100);
            ssize_t count;
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0 || (pending.revents & (POLLERR | POLLNVAL))) return -1;
            if (!ready) continue;
            count = read(STDIN_FILENO, &byte, 1);
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) return -1;
            character = count ? byte : EOF;
            /* A signal may reach the backend worker instead of this thread.
             * Polling bounds exit latency even when read wasn't interrupted. */
        } else
#endif
        {
            character = fgetc(input);
            if (character == EOF && ferror(input)) return -1;
        }
        if (character == EOF || character == '\n') {
            line[used] = 0;
            return character == EOF && !used && !*invalid ? 0 : 1;
        }
        /* Discard the whole invalid line, including its tail; otherwise a long
         * or binary line could accidentally become a second command. */
        if (!character || used + 1 >= capacity) *invalid = true;
        if (!*invalid) line[used++] = (char)character;
    }
}

int prototypeRunStreams(NetworkBackend backend, int argc, char **argv,
                        FILE *input, FILE *output, FILE *errors) {
    Console console;
    const char *path = NULL;
    char line[CONSOLE_LINE_SIZE], error[NETWORK_ERROR_SIZE];
    bool quit = false, baseline_seen = false;
    int i, result = 0;
    memset(&console, 0, sizeof(console));
    controllerInit(&console.app, backend);
    /* Without a preset, Start initially captures with no artificial delay. */
    console.app.lag.inbound_ms = console.app.lag.outbound_ms = 0;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help")) {
            fputs("Usage: clumsier [--preset FILE] [--baseline MS]\n", output);
            help(output);
            goto cleanup;
        }
        if (!strcmp(argv[i], "--preset") && i + 1 < argc && !path) path = argv[++i];
        else if (!strcmp(argv[i], "--baseline") && i + 1 < argc && !baseline_seen) {
            const char *argument = argv[++i];
            if (!number(&argument, PING_MAX_MS, &console.baseline) || *skipSpaces(argument)) {
                fputs("Invalid --baseline; use 0-60000 ms.\n", errors); result = 1; goto cleanup;
            }
            console.has_baseline = baseline_seen = true;
        } else {
            fputs("Usage: clumsier [--preset FILE] [--baseline MS]\n", errors); result = 1; goto cleanup;
        }
    }
    if (path && !load(&console, path, error)) { fprintf(errors, "%s\n", error); result = 1; goto cleanup; }
    fputs("Clumsier native prototype. Type help for commands. Capture starts only when requested.\n", output);
    status(&console, output);
    while (!quit && !interrupted) {
        size_t length;
        bool invalid;
        int read_result;
        fputs("> ", output);
        if (fflush(output) || ferror(errors)) { result = 1; break; }
        read_result = readLine(input, line, sizeof(line), &invalid);
        if (read_result <= 0) {
            if (read_result < 0 && !interrupted) { fputs("Could not read console input.\n", errors); result = 1; }
            break;
        }
        if (invalid) {
            fputs("Command is too long or contains a NUL byte; ignored.\n", errors);
            continue;
        }
        length = strlen(line);
        while (length && isspace((unsigned char)line[length - 1])) line[--length] = 0;
        if (!command(&console, line, output, &quit, error)) { fprintf(errors, "%s\n", error); fflush(errors); }
    }
cleanup:
    controllerShutdown(&console.app);
    if (fflush(output) || fflush(errors) || ferror(output) || ferror(errors)) result = 1;
    return interrupted ? 1 : result;
}

int prototypeRun(NetworkBackend backend, int argc, char **argv) {
    int result;
#ifdef _WIN32
    void (*old_int)(int), (*old_term)(int);
    interrupted = 0;
    old_int = signal(SIGINT, interruptConsole);
    old_term = signal(SIGTERM, interruptConsole);
#else
    struct sigaction action, old_int, old_term, old_pipe;
    interrupted = 0;
    memset(&action, 0, sizeof(action));
    action.sa_handler = interruptConsole;
    sigemptyset(&action.sa_mask);
    /* No SA_RESTART: Ctrl+C must interrupt a blocked input read so we can stop
     * the backend normally, outside the signal handler. */
    if (sigaction(SIGINT, &action, &old_int)) return 1;
    if (sigaction(SIGTERM, &action, &old_term)) { sigaction(SIGINT, &old_int, NULL); return 1; }
    action.sa_handler = SIG_IGN;
    /* A closed output pipe is an I/O failure, not permission to exit without
     * removing capture rules. The console notices the stream error and stops. */
    if (sigaction(SIGPIPE, &action, &old_pipe)) {
        sigaction(SIGTERM, &old_term, NULL); sigaction(SIGINT, &old_int, NULL); return 1;
    }
#endif
    result = prototypeRunStreams(backend, argc, argv, stdin, stdout, stderr);
#ifdef _WIN32
    if (old_int != SIG_ERR) signal(SIGINT, old_int);
    if (old_term != SIG_ERR) signal(SIGTERM, old_term);
#else
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
    sigaction(SIGPIPE, &old_pipe, NULL);
#endif
    return result;
}
