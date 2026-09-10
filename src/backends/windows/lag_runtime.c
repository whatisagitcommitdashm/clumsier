// Adapter for running the existing packet scheduler without the IUP frontend.
#ifdef CLUMSIER_LAG_ONLY
#include <winsock2.h>
#include <mmsystem.h>
#include "lag_engine.h"

Module lagModule = {"Lag", "lag", &windowsLagEnabled, NULL,
    windowsLagStart, windowsLagStop, windowsLagProcess, 0, 0, NULL};
Module *modules[MODULE_CNT] = {&lagModule};
volatile short sendState = SEND_STATUS_NONE;
static BOOL timerStarted;
void startTimePeriod(void) {
    if (!timerStarted) timerStarted = timeBeginPeriod(TIMER_RESOLUTION) == TIMERR_NOERROR;
}
void endTimePeriod(void) {
    if (timerStarted) { timeEndPeriod(TIMER_RESOLUTION); timerStarted = FALSE; }
}
#endif
