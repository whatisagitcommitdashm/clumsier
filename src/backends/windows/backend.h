#pragma once
#include "core/network.h"
NetworkBackend windowsNetworkBackend(void);
// Exposed separately so translation can be tested without opening WinDivert.
bool windowsBuildFilter(const CaptureTarget *target, char filter[NATIVE_FILTER_SIZE], char *error);
