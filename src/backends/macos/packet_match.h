#pragma once
#include "core/network.h"
typedef struct { TrafficSelector traffic; unsigned char address[16]; int address_bytes; } MacPacketMatch;
bool macPacketMatchPrepare(const CaptureTarget *target, MacPacketMatch *match, char *error);
/* Apple packet filters receive layer-2 frames. Unknown/truncated frames pass
 * through; we never guess a port from fragments or an unrecognized header. */
bool macPacketMatches(const MacPacketMatch *match, const unsigned char *frame, size_t length, bool outbound);
