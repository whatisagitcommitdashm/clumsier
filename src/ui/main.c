#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <Windows.h>
#include <conio.h>
#include "iup.h"
#include "common.h"
#include "core/controller.h"
#include "backends/windows/backend.h"
#include "lag_controls.h"
#include "hotkeys.h"
#include "sequence_controls.h"

// ! the order decides which module get processed first
Module* modules[MODULE_CNT] = {
    &lagModule,
    &dropModule,
    &throttleModule,
    &dupModule,
    &oodModule,
    &tamperModule,
    &resetModule,
	&bandwidthModule,
};

volatile short sendState = SEND_STATUS_NONE;

// global iup handlers
static Ihandle *dialog, *topFrame, *bottomFrame; 
static Ihandle *statusLabel;
static Ihandle *filterText, *filterButton;
Ihandle *filterSelectList;
// timer to update icons
static Ihandle *stateIcon;
static Ihandle *timer;
static Ihandle *timeout = NULL;
static Ihandle *hotkeyInputs[ACTION_COUNT], *hotkeyStatus;
static HotkeySettings hotkeySettings;
static wchar_t hotkeyPath[MAX_PATH];
static BOOL hotkeysInitialized;
static AppController application;
static bool advancedMode;
static Ihandle *viewTabs, *simpleCapture, *simpleStart;

void showStatus(const char *line);
static int uiOnDialogShow(Ihandle *ih, int state);
static int uiToggleCaptureCb(Ihandle *ih);
static void uiPerformAction(AppAction action);
static void uiConfigurationChanged(void);
static int uiAdvancedChanged(Ihandle *ih, int state);
static int uiUseQuickControls(Ihandle *ih);

static void uiShowDirections(Ihandle *control, bool visible) {
    Ihandle *child;
    const char *title = IupGetAttribute(control, "TITLE");
    if (!strcmp(IupGetClassName(control), "toggle") && title &&
        (!strcmp(title, "Inbound") || !strcmp(title, "Outbound"))) {
        IupSetAttribute(control, "FLOATING", visible ? "NO" : "YES");
        IupSetAttribute(control, "VISIBLE", visible ? "YES" : "NO");
    }
    for (child = IupGetChild(control, 0); child; child = IupGetBrother(child)) uiShowDirections(child, visible);
}
static int uiStartCb(Ihandle *ih);
static int uiTimerCb(Ihandle *ih);
static int uiTimeoutCb(Ihandle *ih);
static int uiListSelectCb(Ihandle *ih, char *text, int item, int state);
static int uiFilterTextCb(Ihandle *ih);
static void uiSetupModule(Module *module, Ihandle *parent);

// serializing config files using a stupid custom format
#define CONFIG_FILE "config.txt"
#define CONFIG_MAX_RECORDS 64
#define CONFIG_BUF_SIZE 4096
typedef struct {
    char* filterName;
    char* filterValue;
} filterRecord;
UINT filtersSize;
filterRecord filters[CONFIG_MAX_RECORDS] = {0};
char configBuf[CONFIG_BUF_SIZE+2]; // add some padding to write \n
BOOL parameterized = 0; // parameterized flag, means reading args from command line

// loading up filters and fill in
void loadConfig() {
    char path[MSG_BUFSIZE];
    char *p;
    FILE *f;
    GetModuleFileName(NULL, path, MSG_BUFSIZE);
    LOG("Executable path: %s", path);
    p = strrchr(path, '\\');
    if (p == NULL) p = strrchr(path, '/'); // holy shit
    strcpy(p+1, CONFIG_FILE);
    LOG("Config path: %s", path);
    f = fopen(path, "r");
    if (f) {
        size_t len;
        char *current, *last;
        len = fread(configBuf, sizeof(char), CONFIG_BUF_SIZE, f);
        if (len == CONFIG_BUF_SIZE) {
            LOG("Config file is larger than %d bytes, get truncated.", CONFIG_BUF_SIZE);
        }
        // always patch in a newline at the end to ease parsing
        configBuf[len] = '\n';
        configBuf[len+1] = '\0';

        // parse out the kv pairs. isn't quite safe
        filtersSize = 0;
        last = current = configBuf;
        do {
            // eat up empty lines
EAT_SPACE:  while (isspace(*current)) { ++current; }
            if (*current == '#') {
                current = strchr(current, '\n');
                if (!current) break;
                current = current + 1;
                goto EAT_SPACE;
            }

            // now we can start
            last = current;
            current = strchr(last, ':');
            if (!current) break;
            *current = '\0';
            filters[filtersSize].filterName = last;
            current += 1;
            while (isspace(*current)) { ++current; } // eat potential space after :
            last = current;
            current = strchr(last, '\n');
            if (!current) break;
            filters[filtersSize].filterValue = last;
            *current = '\0';
            if (*(current-1) == '\r') *(current-1) = 0;
            last = current = current + 1;
            ++filtersSize;
        } while (last && last - configBuf < CONFIG_BUF_SIZE);
        LOG("Loaded %u records.", filtersSize);
    }

    if (!f || filtersSize == 0)
    {
        LOG("Failed to load from config. Fill in a simple one.");
        // config is missing or ill-formed. fill in some simple ones
        filters[filtersSize].filterName = "loopback packets";
        filters[filtersSize].filterValue = "outbound and ip.DstAddr >= 127.0.0.1 and ip.DstAddr <= 127.255.255.255";
        filtersSize = 1;
    }
}

static void uiShowHotkeySettings(const HotkeySettings *settings) {
    int i;
    for (i = 0; i < ACTION_COUNT; ++i) {
        char text[HOTKEY_TEXT_SIZE];
        hotkeyFormat(settings->bindings[i], text);
        IupStoreAttribute(hotkeyInputs[i], "VALUE", text);
    }
}

static int uiApplyHotkeysCb(Ihandle *ih) {
    // The fields are a draft. Record, Clear, and Show defaults only edit them;
    // active bindings change after the entire draft validates and saves.
    HotkeySettings proposed;
    char error[HOTKEY_ERROR_SIZE];
    int i;
    UNREFERENCED_PARAMETER(ih);
    for (i = 0; i < ACTION_COUNT; ++i) {
        if (!hotkeyParse(IupGetAttribute(hotkeyInputs[i], "VALUE"), &proposed.bindings[i], error)) {
            IupStoreAttribute(hotkeyStatus, "TITLE", error);
            return IUP_DEFAULT;
        }
    }
    if (!hotkeySettingsPath(hotkeyPath, error) ||
        !hotkeysApply(&proposed, hotkeyPath, error)) {
        IupStoreAttribute(hotkeyStatus, "TITLE", error);
        return IUP_DEFAULT;
    }
    hotkeySettings = proposed;
    uiShowHotkeySettings(&hotkeySettings);
    IupSetAttribute(hotkeyStatus, "TITLE", "Hotkeys applied and saved.");
    return IUP_DEFAULT;
}

static int uiDefaultHotkeysCb(Ihandle *ih) {
    HotkeySettings defaults;
    UNREFERENCED_PARAMETER(ih);
    hotkeyDefaults(&defaults);
    uiShowHotkeySettings(&defaults);
    IupSetAttribute(hotkeyStatus, "TITLE", "Defaults shown. Choose Apply & Save to activate them.");
    return IUP_DEFAULT;
}

static Ihandle *recordDialog, *recordPreview;
static HotkeyBinding recordedBinding;
static BOOL recordingComplete;

static int uiIgnoreRecordingKey(Ihandle *ih, int key) {
    UNREFERENCED_PARAMETER(ih);
    UNREFERENCED_PARAMETER(key);
    // Keyboard input belongs to the recorder, not dialog accelerators. This
    // affects only this application's dialog; the input hooks still forward it.
    return IUP_IGNORE;
}

static int uiCancelRecordingCb(Ihandle *ih) {
    UNREFERENCED_PARAMETER(ih);
    recordingComplete = FALSE;
    hotkeysRecordCancel();
    IupHide(recordDialog);
    return IUP_DEFAULT;
}

static void uiRecordingChanged(const HotkeyBinding *binding, BOOL finished) {
    char text[HOTKEY_TEXT_SIZE];
    hotkeyFormat(*binding, text);
    IupStoreAttribute(recordPreview, "TITLE", text);
    if (finished) {
        // The matcher owns binding. Keep a copy before closing the modal loop.
        recordedBinding = *binding;
        recordingComplete = TRUE;
        IupHide(recordDialog);
    }
}

static int uiCancelRecordingMouse(Ihandle *ih, int button, int pressed, int x, int y, char *status) {
    UNREFERENCED_PARAMETER(x); UNREFERENCED_PARAMETER(y); UNREFERENCED_PARAMETER(status);
    // Cancel on press, before releasing this click could complete a Mouse1 binding.
    if (button == IUP_BUTTON1 && pressed) return uiCancelRecordingCb(ih);
    return IUP_DEFAULT;
}

static int uiRecordHotkeyCb(Ihandle *ih) {
    int index = IupGetInt(ih, "_HOTKEY_ACTION");
    Ihandle *cancel;
    char text[HOTKEY_TEXT_SIZE];
    if (!hotkeysInitialized || recordDialog) return IUP_DEFAULT;
    recordingComplete = FALSE;
    recordPreview = IupLabel("Release any held keys, then press your combination.");
    IupSetAttribute(recordPreview, "WORDWRAP", "YES");
    IupSetAttribute(recordPreview, "SIZE", "300x70");
    cancel = IupButton("Cancel", NULL);
    IupSetAttribute(cancel, "CANFOCUS", "NO");
    IupSetCallback(cancel, "ACTION", uiCancelRecordingCb);
    IupSetCallback(cancel, "BUTTON_CB", (Icallback)uiCancelRecordingMouse);
    recordDialog = IupDialog(IupVbox(
        IupLabel("Hold the keys / mouse buttons together, then release to finish.\n"
                 "Escape is recordable. Click Cancel to discard."), recordPreview, cancel, NULL));
    IupSetAttribute(recordDialog, "TITLE", "Record hotkey");
    IupSetAttribute(recordDialog, "MARGIN", "10x10");
    IupSetAttribute(recordDialog, "GAP", "8");
    IupSetAttribute(recordDialog, "RESIZE", "NO");
    IupSetAttribute(recordDialog, "MENUBOX", "NO");
    IupSetAttributeHandle(recordDialog, "PARENTDIALOG", dialog);
    IupSetCallback(recordDialog, "K_ANY", (Icallback)uiIgnoreRecordingKey);
    IupSetCallback(recordDialog, "CLOSE_CB", uiCancelRecordingCb);
    hotkeysRecordBegin(uiRecordingChanged);
    // Popup runs a nested event loop: previews keep arriving, but the main
    // dialog can't be edited. Hiding the recorder lets this call return.
    IupPopup(recordDialog, IUP_CENTERPARENT, IUP_CENTERPARENT);
    hotkeysRecordCancel();
    IupDestroy(recordDialog);
    recordDialog = recordPreview = NULL;
    if (recordingComplete) {
        hotkeyFormat(recordedBinding, text);
        IupStoreAttribute(hotkeyInputs[index], "VALUE", text);
        IupSetAttribute(hotkeyStatus, "TITLE", "Recorded. Choose Apply & Save to activate the edited bindings.");
    } else IupSetAttribute(hotkeyStatus, "TITLE", "Recording canceled. Previous binding kept.");
    return IUP_DEFAULT;
}

static int uiHotkeyFieldClick(Ihandle *ih, int button, int pressed, int x, int y, char *status) {
    UNREFERENCED_PARAMETER(x); UNREFERENCED_PARAMETER(y); UNREFERENCED_PARAMETER(status);
    // Let the native text control finish releasing mouse capture before opening
    // a modal dialog, or it can keep receiving clicks meant for the recorder.
    if (button == IUP_BUTTON1 && !pressed)
        IupPostMessage(dialog, NULL, IupGetInt(ih, "_HOTKEY_ACTION"), 0, NULL);
    return IUP_DEFAULT;
}

static int uiRecordRequestCb(Ihandle *ih, const char *text, int index, double number, void *data) {
    UNREFERENCED_PARAMETER(ih); UNREFERENCED_PARAMETER(text);
    UNREFERENCED_PARAMETER(number); UNREFERENCED_PARAMETER(data);
    if (index >= 0 && index < ACTION_COUNT) return uiRecordHotkeyCb(hotkeyInputs[index]);
    return IUP_DEFAULT;
}

static int uiClearHotkeyCb(Ihandle *ih) {
    int index = IupGetInt(ih, "_HOTKEY_ACTION");
    IupSetAttribute(hotkeyInputs[index], "VALUE", "None");
    IupSetAttribute(hotkeyStatus, "TITLE", "Binding cleared in the editor. Choose Apply & Save to activate.");
    return IUP_DEFAULT;
}

static Ihandle *uiCreateHotkeyPanel(void) {
    Ihandle *rows = IupVbox(NULL), *frame, *apply, *defaults;
    int i;
    for (i = 0; i < ACTION_COUNT; ++i) {
        Ihandle *label = IupLabel(actionName((AppAction)i));
        Ihandle *record = IupButton("Record", NULL), *clear = IupButton("Clear", NULL);
        IupSetAttribute(label, "SIZE", "75x");
        hotkeyInputs[i] = IupText(NULL);
        IupSetAttribute(hotkeyInputs[i], "VISIBLECOLUMNS", "24");
        IupSetInt(hotkeyInputs[i], "NC", HOTKEY_TEXT_SIZE - 1);
        IupSetAttribute(hotkeyInputs[i], "READONLY", "YES");
        IupSetAttribute(hotkeyInputs[i], "TIP", "Click to record a keyboard or mouse combination.");
        IupSetInt(hotkeyInputs[i], "_HOTKEY_ACTION", i);
        IupSetInt(record, "_HOTKEY_ACTION", i);
        IupSetInt(clear, "_HOTKEY_ACTION", i);
        IupSetCallback(hotkeyInputs[i], "BUTTON_CB", (Icallback)uiHotkeyFieldClick);
        IupSetCallback(record, "ACTION", uiRecordHotkeyCb);
        IupSetCallback(clear, "ACTION", uiClearHotkeyCb);
        IupAppend(rows, IupHbox(label, hotkeyInputs[i], record, clear, NULL));
    }
    IupAppend(rows, IupLabel("Click a binding to record. Keys and mouse buttons also reach your game."));
    apply = IupButton("Apply & Save", NULL);
    defaults = IupButton("Show defaults", NULL);
    IupSetCallback(apply, "ACTION", uiApplyHotkeysCb);
    IupSetCallback(defaults, "ACTION", uiDefaultHotkeysCb);
    IupAppend(rows, IupHbox(apply, defaults, NULL));
    hotkeyStatus = IupLabel("Hotkeys are not active yet.");
    IupSetAttribute(hotkeyStatus, "WORDWRAP", "YES");
    IupSetAttribute(hotkeyStatus, "EXPAND", "HORIZONTAL");
    IupSetAttribute(hotkeyStatus, "SIZE", "0x48");
    IupAppend(rows, hotkeyStatus);
    IupSetAttribute(rows, "MARGIN", "4x4");
    IupSetAttribute(rows, "GAP", "4");
    frame = IupFrame(rows);
    IupSetAttribute(frame, "TITLE", "Global hotkeys");
    IupSetAttribute(frame, "EXPAND", "HORIZONTAL");
    hotkeyDefaults(&hotkeySettings);
    uiShowHotkeySettings(&hotkeySettings);
    return frame;
}

static void uiInitializeHotkeys(void) {
    char error[HOTKEY_ERROR_SIZE], notice[HOTKEY_ERROR_SIZE] = {0};
    if (hotkeysInitialized) return;
    if (!hotkeysOpen(uiPerformAction, error)) {
        IupStoreAttribute(hotkeyStatus, "TITLE", error);
        return;
    }
    hotkeysInitialized = TRUE;
    if (!hotkeySettingsPath(hotkeyPath, notice) ||
        !hotkeyLoad(hotkeyPath, &hotkeySettings, notice)) {
        hotkeyDefaults(&hotkeySettings);
    }
    uiShowHotkeySettings(&hotkeySettings);
    if (!hotkeysApply(&hotkeySettings, NULL, error)) {
        char message[HOTKEY_ERROR_SIZE + 64];
        sprintf(message, "No global hotkeys are active. %s", error);
        IupStoreAttribute(hotkeyStatus, "TITLE", message);
    } else {
        IupStoreAttribute(hotkeyStatus, "TITLE", notice[0] ? notice : "Hotkeys active. Edit a binding and choose Apply & Save.");
    }
}

void init(int argc, char* argv[]) {
    UINT ix;
    Ihandle *topVbox, *bottomVbox, *dialogVBox, *controlHbox, *tabs, *sequences, *hotkeyPanel;
    Ihandle *noneIcon, *doingIcon, *errorIcon;
    char* arg_value = NULL;

    controllerInit(&application, windowsNetworkBackend());
    lagUIUseController(&application);

    // fill in config
    loadConfig();

    // iup inits
    IupOpen(&argc, &argv);
    IupSetGlobal("UTF8MODE", "YES");
    IupSetGlobal("UTF8MODE_FILE", "YES");

    // this is so easy to get wrong so it's pretty worth noting in the program
    statusLabel = IupLabel("NOTICE: When capturing localhost (loopback) packets, you CAN'T include inbound criteria.\n"
        "Filters like 'udp' need to be 'udp and outbound' to work. See readme for more info.");
    IupSetAttribute(statusLabel, "EXPAND", "HORIZONTAL");
    IupSetAttribute(statusLabel, "PADDING", "8x8");
    IupSetAttributes(statusLabel, "WORDWRAP=YES, SIZE=0x38");

    topFrame = IupFrame(
        topVbox = IupVbox(
            filterText = IupText(NULL),
            controlHbox = IupHbox(
                stateIcon = IupLabel(NULL),
                filterButton = IupButton("Start", NULL),
                IupFill(),
                IupLabel("Traffic filters:  "),
                filterSelectList = IupList(NULL),
                NULL
            ),
            NULL
        )
    );

    // parse arguments and set globals *before* setting up UI.
    // arguments can be read and set after callbacks are setup
    // FIXME as Release is built as WindowedApp, stdout/stderr won't show
    LOG("argc: %d", argc);
    if (argc > 1) {
        if (!parseArgs(argc, argv)) {
            fprintf(stderr, "invalid argument count. ensure you're using options as \"--drop on\"");
            exit(-1); // fail fast.
        }
        parameterized = 1;
    }

    IupSetAttribute(topFrame, "TITLE", "Filtering");
    IupSetAttribute(topFrame, "EXPAND", "HORIZONTAL");
    IupSetAttribute(filterText, "EXPAND", "HORIZONTAL");
    IupSetCallback(filterText, "VALUECHANGED_CB", (Icallback)uiFilterTextCb);
    IupSetAttribute(filterButton, "PADDING", "8x");
    IupSetCallback(filterButton, "ACTION", uiToggleCaptureCb);
    IupSetAttribute(topVbox, "NCMARGIN", "4x4");
    IupSetAttribute(topVbox, "NCGAP", "4x2");
    IupSetAttribute(controlHbox, "ALIGNMENT", "ACENTER");

    // setup state icon
    IupSetAttribute(stateIcon, "IMAGE", "none_icon");
    IupSetAttribute(stateIcon, "PADDING", "4x");

    // fill in options and setup callback
    IupSetAttribute(filterSelectList, "VISIBLECOLUMNS", "24");
    IupSetAttribute(filterSelectList, "DROPDOWN", "YES");
    for (ix = 0; ix < filtersSize; ++ix) {
        char ixBuf[4];
        sprintf(ixBuf, "%d", ix+1); // ! staring from 1, following lua indexing
        IupStoreAttribute(filterSelectList, ixBuf, filters[ix].filterName);
    }
    IupSetAttribute(filterSelectList, "VALUE", "1");
    IupSetCallback(filterSelectList, "ACTION", (Icallback)uiListSelectCb);
    // set filter text value since the callback won't take effect before main loop starts
    IupSetAttribute(filterText, "VALUE", parameterized ? filters[0].filterValue : "inbound");
    if (!parameterized) IupSetInt(filterSelectList, "VALUE", 0);

    // functionalities frame 
    bottomFrame = IupFrame(
        bottomVbox = IupVbox(
            NULL
        )
    );
    IupSetAttribute(bottomFrame, "TITLE", "Functions");
    IupSetAttribute(bottomVbox, "NCMARGIN", "4x4");
    IupSetAttribute(bottomVbox, "NCGAP", "4x2");

    // create icons
    noneIcon = IupImage(8, 8, icon8x8);
    doingIcon = IupImage(8, 8, icon8x8);
    errorIcon = IupImage(8, 8, icon8x8);
    IupSetAttribute(noneIcon, "0", "BGCOLOR");
    IupSetAttribute(noneIcon, "1", "224 224 224");
    IupSetAttribute(doingIcon, "0", "BGCOLOR");
    IupSetAttribute(doingIcon, "1", "109 170 44");
    IupSetAttribute(errorIcon, "0", "BGCOLOR");
    IupSetAttribute(errorIcon, "1", "208 70 72");
    IupSetHandle("none_icon", noneIcon);
    IupSetHandle("doing_icon", doingIcon);
    IupSetHandle("error_icon", errorIcon);

    // setup module uis
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        uiSetupModule(*(modules+ix), bottomVbox);
    }

    sequences = sequenceUICreate(&application, uiPerformAction, uiConfigurationChanged);
    hotkeyPanel = uiCreateHotkeyPanel();
    IupSetAttribute(hotkeyPanel, "TABTITLE", "Hotkeys");
    {
        Ihandle *useQuick = IupButton("Use quick controls", NULL);
        Ihandle *help = IupLabel("Turn an effect on, set its amount, then Start. No sequence is needed.\nIf a sequence is active, choose Use quick controls to take over its current settings.");
        Ihandle *quick;
        IupSetAttributes(help, "WORDWRAP=YES, EXPAND=HORIZONTAL, SIZE=0x40");
        IupSetCallback(useQuick, "ACTION", uiUseQuickControls);
        quick = IupVbox(help, useQuick, bottomFrame, NULL);
        IupSetAttributes(quick, "TABTITLE=Quick controls, MARGIN=5x5, GAP=6, EXPAND=HORIZONTAL");
        tabs = IupTabs(quick, sequences, hotkeyPanel, NULL);
    }
    viewTabs = tabs;
    IupSetAttribute(tabs, "EXPAND", "HORIZONTAL");
    simpleStart = IupButton("Start", NULL);
    IupSetCallback(simpleStart, "ACTION", uiToggleCaptureCb);
    simpleCapture = IupHbox(simpleStart, IupLabel("Start / Stop also works with your hotkeys."), NULL);
    {
        Ihandle *advanced = IupToggle("Advanced mode", NULL);
        IupSetCallback(advanced, "ACTION", (Icallback)uiAdvancedChanged);
        IupSetHandle("clumsier_advanced_mode", advanced);
    }
    // Keep the active step visible even when viewing another tab.
    dialog = IupDialog(IupScrollBox(
        dialogVBox = IupVbox(
            IupHbox(IupLabel("Clumsier"), IupFill(), IupGetHandle("clumsier_advanced_mode"), NULL),
            simpleCapture,
            topFrame,
            sequenceUIStatus(),
            tabs,
            statusLabel,
            NULL
        )
    ));
    IupSetAttribute(IupGetChild(dialog, 0), "EXPAND", "YES");
    IupSetAttribute(dialogVBox, "EXPAND", "YES");

    IupSetAttribute(dialog, "TITLE", "clumsy " CLUMSY_VERSION);
    IupSetAttribute(dialog, "SIZE", "530x");
    IupSetAttribute(dialog, "RESIZE", "YES");
    IupSetAttribute(dialog, "SHRINK", "YES");
    IupSetCallback(dialog, "SHOW_CB", (Icallback)uiOnDialogShow);
    IupSetCallback(dialog, "POSTMESSAGE_CB", (Icallback)uiRecordRequestCb);
    // Command-line users intentionally requested the inherited manual controls.
    uiAdvancedChanged(IupGetHandle("clumsier_advanced_mode"), parameterized != 0);
    showStatus("Quick controls are ready. Start uses incoming traffic by default; Advanced mode lets you change it.");


    // global layout settings to affect childrens
    IupSetAttribute(dialogVBox, "ALIGNMENT", "ACENTER");
    IupSetAttribute(dialogVBox, "NCMARGIN", "4x4");
    IupSetAttribute(dialogVBox, "NCGAP", "4x2");

    // setup timer
    timer = IupTimer();
    IupSetAttribute(timer, "TIME", STR(ICON_UPDATE_MS));
    IupSetCallback(timer, "ACTION_CB", uiTimerCb);

    // setup timeout of program
    arg_value = IupGetGlobal("timeout");
    if(arg_value != NULL)
    {
        char valueBuf[16];
        sprintf(valueBuf, "%s000", arg_value);  // convert from seconds to milliseconds

        timeout = IupTimer();
        IupStoreAttribute(timeout, "TIME", valueBuf);
        IupSetCallback(timeout, "ACTION_CB", uiTimeoutCb);
        IupSetAttribute(timeout, "RUN", "YES");
    }

}

void startup() {
    // initialize seed
    srand((unsigned int)time(NULL));

    // kickoff event loops
    IupShowXY(dialog, IUP_CENTER, IUP_CENTER);
    IupMainLoop();
    // ! main loop won't return until program exit
}

void cleanup() {
    hotkeysClose();
    controllerShutdown(&application);
    IupDestroy(timer);
    if (timeout) {
        IupDestroy(timeout);
    }

    IupClose();
    endTimePeriod(); // try close if not closing
}

// ui logics
void showStatus(const char *line) {
    IupStoreAttribute(statusLabel, "TITLE", line); 
}


// in fact only 32bit binary would run on 64 bit os
// if this happens pop out message box and exit
static BOOL check32RunningOn64(HWND hWnd) {
    BOOL is64ret;
    // consider IsWow64Process return value
    if (IsWow64Process(GetCurrentProcess(), &is64ret) && is64ret) {
        MessageBox(hWnd, (LPCSTR)"You're running 32bit clumsy on 64bit Windows, which wouldn't work. Please use the 64bit clumsy version.",
            (LPCSTR)"Aborting", MB_OK);
        return TRUE;
    }
    return FALSE;
}

static BOOL checkIsRunning() {
    //It will be closed and destroyed when programm terminates (according to MSDN).
    HANDLE hStartEvent = CreateEventW(NULL, FALSE, FALSE, L"Global\\CLUMSY_IS_RUNNING_EVENT_NAME");

    if (hStartEvent == NULL)
        return TRUE;

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hStartEvent);
        hStartEvent = NULL;
        return TRUE;
    }

    return FALSE;
}


static int uiOnDialogShow(Ihandle *ih, int state) {
    // only need to process on show
    HWND hWnd;
    BOOL exit;
    HICON icon;
    HINSTANCE hInstance;
    if (state != IUP_SHOW) return IUP_DEFAULT;
    hWnd = (HWND)IupGetAttribute(ih, "HWND");
    hInstance = GetModuleHandle(NULL);

    // set application icon
    icon = LoadIcon(hInstance, "CLUMSY_ICON");
    SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
    SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);

    exit = checkIsRunning();
    if (exit) {
        MessageBox(hWnd, (LPCSTR)"Theres' already an instance of clumsy running.",
            (LPCSTR)"Aborting", MB_OK);
        return IUP_CLOSE;
    }

#ifdef _WIN32
    exit = check32RunningOn64(hWnd);
    if (exit) {
        return IUP_CLOSE;
    }
#endif

    // try elevate and decides whether to exit
    exit = tryElevate(hWnd, parameterized);

    if (!exit) { uiInitializeHotkeys(); sequenceUIInitialize(); }

    if (!exit && parameterized) {
        setFromParameter(filterText, "VALUE", "filter");
        LOG("is parameterized, start filtering upon execution.");
        uiStartCb(filterButton);
    }

    return exit ? IUP_CLOSE : IUP_DEFAULT;
}

static void uiPerformAction(AppAction action) {
    // Buttons and hotkeys arrive here on the UI thread. Keep capture operations
    // in this path so future controls get the same Start/Stop/Toggle behavior.
    char error[MSG_BUFSIZE] = {0};
    CaptureTarget target = {0};
    // The inherited text box is explicitly a Windows-native filter. Portable
    // presets will use target.traffic instead of borrowing this syntax.
    if (!controllerIsRunning(&application) && !application.preset_loaded &&
        (action == ACTION_START_CAPTURE || action == ACTION_TOGGLE_CAPTURE)) {
        const char *filter = IupGetAttribute(filterText, "VALUE");
        if (!filter || !*filter || strlen(filter) >= sizeof(target.native_filter)) {
            showStatus("Enter a Windows filter shorter than 1024 characters.");
            return;
        }
        strcpy(target.native_backend, "windivert");
        strcpy(target.native_filter, filter);
        if (!controllerSetTarget(&application, &target, error)) { showStatus(error); return; }
    }
    BOOL active;
    int i;
    if (!controllerExecute(&application, action, error)) {
        showStatus(error);
        return;
    }
    active = controllerIsRunning(&application);
    uiConfigurationChanged();
    IupSetAttribute(filterButton, "TITLE", active ? "Stop" : "Start");
    IupSetAttribute(simpleStart, "TITLE", active ? "Stop" : "Start");
    IupSetAttribute(timer, "RUN", active ? "YES" : "NO");
    if (!active) {
        for (i = 0; i < MODULE_CNT; ++i) {
            modules[i]->processTriggered = 0;
            IupSetAttribute(modules[i]->iconHandle, "IMAGE", "none_icon");
        }
        sendState = SEND_STATUS_NONE;
        IupSetAttribute(stateIcon, "IMAGE", "none_icon");
    }
    showStatus(active ? "Capture running." : "Capture stopped.");
}

static int uiAdvancedChanged(Ihandle *ih, int state) {
    advancedMode = state != 0;
    IupSetInt(ih, "VALUE", state);
    IupSetAttribute(topFrame, "FLOATING", advancedMode ? "NO" : "YES");
    IupSetAttribute(topFrame, "VISIBLE", advancedMode ? "YES" : "NO");
    IupSetAttribute(simpleCapture, "FLOATING", advancedMode ? "YES" : "NO");
    IupSetAttribute(simpleCapture, "VISIBLE", advancedMode ? "NO" : "YES");
    uiShowDirections(bottomFrame, advancedMode);
    sequenceUISetAdvanced(advancedMode);
    if (dialog) IupRefresh(dialog);
    return IUP_DEFAULT;
}

static int uiUseQuickControls(Ihandle *ih) {
    UNREFERENCED_PARAMETER(ih);
    // Taking over is explicit. Merely looking at another tab never unloads a
    // sequence, changes traffic selection, or starts/stops capture.
    controllerUnloadPreset(&application);
    uiConfigurationChanged();
    showStatus("Quick controls now own the current settings. Capture state and delay are unchanged.");
    return IUP_DEFAULT;
}

static void uiConfigurationChanged(void) {
    bool editable = !application.preset_loaded && !controllerIsRunning(&application);
    char filter[NATIVE_FILTER_SIZE], error[NETWORK_ERROR_SIZE];
    if (application.target.native_filter[0] || application.preset_loaded) {
        if (windowsBuildFilter(&application.target, filter, error)) IupStoreAttribute(filterText, "VALUE", filter);
    }
    IupSetAttribute(filterText, "ACTIVE", editable ? "YES" : "NO");
    IupSetAttribute(filterSelectList, "ACTIVE", editable ? "YES" : "NO");
    lagUIRefresh();
    // A loaded v1 preset owns Lag and excludes the other effects. Unload makes
    // manual editing available again without changing the accepted settings.
    IupSetAttribute(bottomFrame, "ACTIVE", application.preset_loaded ? "NO" : "YES");
    sequenceUIRefresh();
}

static int uiStartCb(Ihandle *ih) {
    UNREFERENCED_PARAMETER(ih);
    uiPerformAction(ACTION_START_CAPTURE);
    return IUP_DEFAULT;
}

static int uiToggleCaptureCb(Ihandle *ih) {
    UNREFERENCED_PARAMETER(ih);
    uiPerformAction(ACTION_TOGGLE_CAPTURE);
    return IUP_DEFAULT;
}
static int uiToggleControls(Ihandle *ih, int state) {
    Ihandle *controls = (Ihandle*)IupGetAttribute(ih, CONTROLS_HANDLE);
    short *target = (short*)IupGetAttribute(ih, SYNCED_VALUE);
    int controlsActive = IupGetInt(controls, "ACTIVE");
    if (controlsActive && !state) {
        IupSetAttribute(controls, "ACTIVE", "NO");
        InterlockedExchange16(target, I2S(state));
    } else if (!controlsActive && state) {
        IupSetAttribute(controls, "ACTIVE", "YES");
        InterlockedExchange16(target, I2S(state));
    }

    return IUP_DEFAULT;
}

static int uiTimerCb(Ihandle *ih) {
    int ix;
    UNREFERENCED_PARAMETER(ih);
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        if (modules[ix]->processTriggered) {
            IupSetAttribute(modules[ix]->iconHandle, "IMAGE", "doing_icon");
            InterlockedAnd16(&(modules[ix]->processTriggered), 0);
        } else {
            IupSetAttribute(modules[ix]->iconHandle, "IMAGE", "none_icon");
        }
    }

    // update global send status icon
    switch (sendState)
    {
    case SEND_STATUS_NONE:
        IupSetAttribute(stateIcon, "IMAGE", "none_icon");
        break;
    case SEND_STATUS_SEND:
        IupSetAttribute(stateIcon, "IMAGE", "doing_icon");
        InterlockedAnd16(&sendState, SEND_STATUS_NONE);
        break;
    case SEND_STATUS_FAIL:
        IupSetAttribute(stateIcon, "IMAGE", "error_icon");
        InterlockedAnd16(&sendState, SEND_STATUS_NONE);
        break;
    }

    return IUP_DEFAULT;
}

static int uiTimeoutCb(Ihandle *ih) {
    UNREFERENCED_PARAMETER(ih);
    return IUP_CLOSE;
 }

static int uiListSelectCb(Ihandle *ih, char *text, int item, int state) {
    UNREFERENCED_PARAMETER(text);
    UNREFERENCED_PARAMETER(ih);
    if (state == 1) {
        IupSetAttribute(filterText, "VALUE", filters[item-1].filterValue);
    }
    return IUP_DEFAULT;
}

static int uiFilterTextCb(Ihandle *ih)  {
    UNREFERENCED_PARAMETER(ih);
    // unselect list
    IupSetAttribute(filterSelectList, "VALUE", "0");
    return IUP_DEFAULT;
}

static void uiSetupModule(Module *module, Ihandle *parent) {
    Ihandle *groupBox, *toggle, *controls, *icon;
    groupBox = IupHbox(
        icon = IupLabel(NULL),
        toggle = IupToggle(module->displayName, NULL),
        IupFill(),
        controls = module->setupUIFunc(),
        NULL
    );
    IupSetAttribute(groupBox, "EXPAND", "HORIZONTAL");
    IupSetAttribute(groupBox, "ALIGNMENT", "ACENTER");
    IupSetAttribute(controls, "ALIGNMENT", "ACENTER");
    IupAppend(parent, groupBox);

    // set controls as attribute to toggle and enable toggle callback
    IupSetCallback(toggle, "ACTION", (Icallback)uiToggleControls);
    IupSetAttribute(toggle, CONTROLS_HANDLE, (char*)controls);
    IupSetAttribute(toggle, SYNCED_VALUE, (char*)module->enabledFlag);
    IupSetAttribute(controls, "ACTIVE", "NO"); // startup as inactive
    IupSetAttribute(controls, "NCGAP", "4"); // startup as inactive

    // set default icon
    IupSetAttribute(icon, "IMAGE", "none_icon");
    IupSetAttribute(icon, "PADDING", "4x");
    module->iconHandle = icon;

    if (module == &lagModule) lagUIBindToggle(toggle, controls);

    // parameterize toggle
    if (parameterized) {
        setFromParameter(toggle, "VALUE", module->shortName);
    }
}

int main(int argc, char* argv[]) {
    LOG("Is Run As Admin: %d", IsRunAsAdmin());
    LOG("Is Elevated: %d", IsElevated());
    init(argc, argv);
    startup();
    cleanup();
    return 0;
}
