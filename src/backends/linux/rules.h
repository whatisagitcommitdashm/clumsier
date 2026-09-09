#pragma once
#include "../../core/network.h"
#define LINUX_RULES_SIZE 4096
#define LINUX_TABLE_SIZE 64
bool linuxBuildRules(const CaptureTarget *target, const char *table, uint16_t queue,
                     char rules[LINUX_RULES_SIZE], char error[NETWORK_ERROR_SIZE]);
