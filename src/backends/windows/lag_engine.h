#pragma once
#include "core/network.h"
#include "legacy/common.h"
// Private Windows bridge to the inherited module scheduler. Settings are copied
// under a Windows lock; packet processing never reads an IUP widget.
extern short windowsLagEnabled;
void windowsLagConfigure(const LagSettings *settings);
void windowsLagStart(void);
void windowsLagStop(PacketNode *head, PacketNode *tail);
short windowsLagProcess(PacketNode *head, PacketNode *tail);
