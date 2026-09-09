// Map the real controls without showing windows or capturing network traffic.
#include <assert.h>
#define main clumsierApplicationEntry
#include "../src/ui/main.c"
#undef main
static bool fakeRunning;
static bool testRunning(void *context) { (void)context; return fakeRunning; }
static bool testStart(void *ctx, const CaptureTarget *target, const LagSettings *lag, char *error) {
    (void)ctx; (void)lag; (void)error;
    assert(!strcmp(target->native_filter, "inbound")); fakeRunning = true; return true;
}
static bool testApply(void *ctx, const LagSettings *lag, char *error) { (void)ctx; (void)lag; (void)error; return true; }
static void testStop(void *context) { (void)context; fakeRunning = false; }

int main(int argc, char **argv) {
    const NetworkBackendOps ops = {testStart, testApply, testStop, testRunning};
    Ihandle *toggle, *scroll;
    Preset preset;
    AppController before;
    char error[NETWORK_ERROR_SIZE];
    int viewportWidth, viewportHeight, contentWidth, contentHeight;
    init(argc, argv);
    application.backend.ops = &ops;
    assert(IupMap(dialog) == IUP_NOERROR);
    toggle = IupGetHandle("clumsier_advanced_mode");
    assert(!advancedMode && IupGetChildCount(viewTabs) == 3);
    assert(IupGetInt(viewTabs, "VALUEPOS") == 0);
    assert(!strcmp(IupGetAttribute(topFrame, "FLOATING"), "YES"));
    uiPerformAction(ACTION_START_CAPTURE);
    assert(controllerIsRunning(&application) && !application.preset_loaded);
    uiPerformAction(ACTION_STOP_CAPTURE);
    assert(!controllerIsRunning(&application));
    before = application;
    uiAdvancedChanged(toggle, 1);
    assert(advancedMode && !strcmp(IupGetAttribute(topFrame, "FLOATING"), "NO"));
    IupSetInt(viewTabs, "VALUEPOS", 1);
    uiAdvancedChanged(toggle, 0);
    assert(!advancedMode && IupGetInt(viewTabs, "VALUEPOS") == 1);
    assert(!memcmp(&before, &application, sizeof(before)));
    presetDefault(&preset);
    assert(controllerLoadPreset(&application, &preset, true, 40, error));
    fakeRunning = true;
    before = application;
    uiAdvancedChanged(toggle, 1); uiAdvancedChanged(toggle, 0);
    assert(!memcmp(&before, &application, sizeof(before)));
    uiUseQuickControls(NULL);
    assert(!application.preset_loaded && controllerIsRunning(&application));
    assert(!memcmp(&before.lag, &application.lag, sizeof(before.lag)));
    assert(captureTargetsEqual(&before.target, &application.target));
    scroll = IupGetChild(dialog, 0);
    assert(!strcmp(IupGetClassName(scroll), "scrollbox"));
    IupSetAttribute(dialog, "RASTERSIZE", "700x600"); IupRefresh(dialog);
    IupGetIntInt(scroll, "CLIENTSIZE", &viewportWidth, &viewportHeight);
    IupGetIntInt(IupGetChild(scroll, 0), "RASTERSIZE", &contentWidth, &contentHeight);
    assert(viewportWidth > 0 && viewportWidth <= 700);
    assert(contentWidth <= viewportWidth || IupGetFloat(scroll, "XMAX") > IupGetFloat(scroll, "DX"));
    assert(contentHeight <= viewportHeight || IupGetFloat(scroll, "YMAX") > IupGetFloat(scroll, "DY"));
    printf("PASS layout at 700x600: viewport %dx%d, content %dx%d; overflow is scrollable\n", viewportWidth, viewportHeight, contentWidth, contentHeight);
    cleanup();
    puts("PASS Quick controls: default tab, start/stop without sequence, explicit takeover and view switches preserve accepted settings");
    return 0;
}
