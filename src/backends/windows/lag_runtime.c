// Connect the Lag engine to the packet capture worker.
#include <winsock2.h>
#include <mmsystem.h>
#include "lag_engine.h"

Module lagModule = {"Lag", "lag", &windowsLagEnabled,
    windowsLagStart, windowsLagStop, windowsLagProcess, 0, 0};
Module *modules[MODULE_CNT] = {&lagModule};
volatile short sendState = SEND_STATUS_NONE;
static BOOL timerStarted;
void startTimePeriod(void) {
    if (!timerStarted) timerStarted = timeBeginPeriod(TIMER_RESOLUTION) == TIMERR_NOERROR;
}
void endTimePeriod(void) {
    if (timerStarted) { timeEndPeriod(TIMER_RESOLUTION); timerStarted = FALSE; }
}
