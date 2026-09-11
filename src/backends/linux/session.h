#pragma once
#include "lag_queue.h"
// These operations isolate setup/teardown ordering from Linux system calls.
// open cleans up its own partial failure. install reports proven ownership even
// when a command failed after applying its transaction. quiesce and remove must
// check that ownership again and must never change somebody else's table.
typedef struct {
    bool (*open)(void *context, char *error);
    bool (*install)(void *context, bool *owned, char *error);
    bool (*remove)(void *context, char *error);
    bool (*drain)(void *context, char *error);
    void (*close)(void *context);
    // Stop new queueing while retaining base hooks until held packets drain.
    bool (*quiesce)(void *context, char *error);
} LinuxSessionOps;
typedef struct {
    const LinuxSessionOps *ops;
    void *context;
    bool open, owned;
} LinuxSession;
bool linuxSessionStart(LinuxSession *session, char *error);
bool linuxSessionStop(LinuxSession *session, char *error);
