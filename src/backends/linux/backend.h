#pragma once
#include "../../core/network.h"
typedef struct LinuxBackend LinuxBackend;
LinuxBackend *linuxBackendCreate(char error[NETWORK_ERROR_SIZE]);
NetworkBackend linuxBackendInterface(LinuxBackend *backend);
void linuxBackendDestroy(LinuxBackend *backend);
// Copies the last asynchronous failure (empty if none). Calls use the same
// serialized owner as NetworkBackendOps. Worker failures are also printed.
void linuxBackendLastError(LinuxBackend *backend, char error[NETWORK_ERROR_SIZE]);
