#pragma once
#include <stdio.h>
#include "core/network.h"

/* A small frontend for native backend testing. The caller owns the backend and
 * destroys it after this returns; the console always stops it before returning. */
int prototypeRun(NetworkBackend backend, int argc, char **argv);
int prototypeRunStreams(NetworkBackend backend, int argc, char **argv,
                        FILE *input, FILE *output, FILE *errors);
