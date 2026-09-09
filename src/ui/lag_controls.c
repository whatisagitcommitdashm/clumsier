#include <stdlib.h>
#include "lag_controls.h"
#include "backends/windows/lag_engine.h"

static AppController *app;
static Ihandle *inboundCheckbox, *outboundCheckbox, *timeInput;
static Ihandle *enabledToggle, *lagControls;

void lagUIUseController(AppController *controller) { app = controller; }

static int applyDraft(const LagSettings *draft) {
    char error[NETWORK_ERROR_SIZE];
    if (controllerSetLag(app, draft, error)) return 1;
    showStatus(error);
    return 0;
}

static int directionChanged(Ihandle *ih, int state) {
    LagSettings draft = app->lag;
    if (ih == inboundCheckbox) draft.inbound = state != 0;
    else draft.outbound = state != 0;
    if (!applyDraft(&draft)) IupSetInt(ih, "VALUE", ih == inboundCheckbox ? app->lag.inbound : app->lag.outbound);
    return IUP_DEFAULT;
}

static int delayChanged(Ihandle *ih) {
    const char *text = IupGetAttribute(ih, "VALUE");
    char *end;
    long delay;
    LagSettings draft = app->lag;
    if (!text || !*text) return IUP_DEFAULT; // Let the user finish editing.
    delay = strtol(text, &end, 10);
    if (*end || delay < 0 || delay > LAG_MAX_MS) {
        showStatus("Lag delay must be between 0 and 15000 ms.");
        IupSetInt(ih, "VALUE", (int)app->lag.inbound_ms);
        return IUP_DEFAULT;
    }
    // Keep the familiar shared knob. The model/backend already support separate
    // direction values for a future preset editor or redesigned interface.
    draft.inbound_ms = draft.outbound_ms = (uint32_t)delay;
    if (!applyDraft(&draft)) IupSetInt(ih, "VALUE", (int)app->lag.inbound_ms);
    return IUP_DEFAULT;
}

static int enabledChanged(Ihandle *ih, int state) {
    LagSettings draft = app->lag;
    Ihandle *controls = (Ihandle*)IupGetAttribute(ih, CONTROLS_HANDLE);
    draft.enabled = state != 0;
    if (applyDraft(&draft)) IupSetAttribute(controls, "ACTIVE", state ? "YES" : "NO");
    else IupSetInt(ih, "VALUE", app->lag.enabled);
    return IUP_DEFAULT;
}

void lagUIBindToggle(Ihandle *toggle, Ihandle *controls) {
    enabledToggle = toggle;
    lagControls = controls;
    IupSetCallback(toggle, "ACTION", (Icallback)enabledChanged);
    IupSetInt(toggle, "VALUE", app->lag.enabled);
    IupSetAttribute(controls, "ACTIVE", app->lag.enabled ? "YES" : "NO");
}

void lagUIRefresh(void) {
    IupSetInt(enabledToggle, "VALUE", app->lag.enabled);
    IupSetInt(inboundCheckbox, "VALUE", app->lag.inbound);
    IupSetInt(outboundCheckbox, "VALUE", app->lag.outbound);
    IupSetInt(timeInput, "VALUE", (int)app->lag.inbound_ms);
    IupSetAttribute(lagControls, "ACTIVE", app->lag.enabled ? "YES" : "NO");
}

static Ihandle *setupUI(void) {
    Ihandle *controls = IupHbox(
        inboundCheckbox = IupToggle("Inbound", NULL),
        outboundCheckbox = IupToggle("Outbound", NULL),
        IupLabel("Delay(ms):"), timeInput = IupText(NULL), NULL);
    IupSetAttribute(timeInput, "VISIBLECOLUMNS", "4");
    IupSetInt(timeInput, "VALUE", (int)app->lag.inbound_ms);
    IupSetInt(inboundCheckbox, "VALUE", app->lag.inbound);
    IupSetInt(outboundCheckbox, "VALUE", app->lag.outbound);
    IupSetCallback(timeInput, "VALUECHANGED_CB", delayChanged);
    IupSetCallback(inboundCheckbox, "ACTION", (Icallback)directionChanged);
    IupSetCallback(outboundCheckbox, "ACTION", (Icallback)directionChanged);
    if (parameterized) {
        setFromParameter(inboundCheckbox, "VALUE", "lag-inbound");
        setFromParameter(outboundCheckbox, "VALUE", "lag-outbound");
        setFromParameter(timeInput, "VALUE", "lag-time");
    }
    return controls;
}

// Transitional adapter for the inherited Windows effects panel. The backend
// owns the queue/enable flag; this file only supplies the widgets and wiring.
Module lagModule = {
    "Lag", "lag", &windowsLagEnabled, setupUI,
    windowsLagStart, windowsLagStop, windowsLagProcess, 0, 0, NULL
};
