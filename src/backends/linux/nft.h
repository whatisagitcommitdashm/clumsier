#pragma once
#include "../../core/network.h"
bool linuxNftRun(const char *input, char *output, size_t output_size, char *error);
// 1 = our table, 0 = absent, -1 = cannot prove ownership or command failed.
int linuxNftOwned(const char *table, char *error);
