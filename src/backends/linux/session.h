#pragma once
#include "lag_queue.h"
// These operations isolate setup/teardown ordering from Linux system calls.
// open cleans up its own partial failure. install reports proven ownership even
// when a command failed after applying its transaction. remove must check that
// ownership again, and must never delete somebody else's table.
typedef struct {
    bool (*open)(void *context, char *error);
    bool (*install)(void *context, bool *owned, char *error);
    bool (*remove)(void *context, char *error);
    bool (*drain)(void *context, char *error);
    void (*close)(void *context);
} LinuxSessionOps;
typedef struct {
    const LinuxSessionOps *ops;
    void *context;
    bool open, owned;
} LinuxSession;
bool linuxSessionStart(LinuxSession *session, char *error);
bool linuxSessionStop(LinuxSession *session, char *error);
