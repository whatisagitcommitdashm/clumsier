#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hotkeys.h"
#include "hotkey_matcher.h"

// Observe forwarding without injecting input into the user's desktop.
static int forwarded, posted, installs, removals, failInstall;
static WPARAM postedInput;
static LRESULT testForward(HHOOK hook, int code, WPARAM message, LPARAM data) {
    (void)hook; (void)code; (void)message; (void)data;
    ++forwarded;
    return 123;
}
static BOOL testPost(HWND window, UINT message, WPARAM data, LPARAM generation) {
    (void)window; (void)message; (void)generation;
    ++posted; postedInput = data;
    return TRUE;
}
static HHOOK testInstall(int kind, HOOKPROC callback, HINSTANCE instance, DWORD thread) {
    (void)kind; (void)callback; (void)instance; (void)thread;
    if (++installs == failInstall) { SetLastError(ERROR_ACCESS_DENIED); return NULL; }
    return (HHOOK)(INT_PTR)installs;
}
static BOOL testRemove(HHOOK hook) { assert(hook); ++removals; return TRUE; }
#define CallNextHookEx testForward
#define PostMessageW testPost
#define SetWindowsHookExW testInstall
#define UnhookWindowsHookEx testRemove
#include "../src/platform/windows/hotkeys.c"
#undef CallNextHookEx
#undef PostMessageW
#undef SetWindowsHookExW
#undef UnhookWindowsHookEx

static char error[HOTKEY_ERROR_SIZE];
static int dispatched[ACTION_COUNT], recordings, finishedRecordings;
static HotkeyBinding lastRecorded;
static void recordAction(AppAction action) { ++dispatched[action]; }
static void recordBinding(const HotkeyBinding *binding, BOOL finished) {
    ++recordings; finishedRecordings += finished;
    lastRecorded = *binding;
}
static HotkeyBinding parse(const char *text) {
    HotkeyBinding binding;
    assert(hotkeyParse(text, &binding, error));
    return binding;
}
static void writeText(const wchar_t *path, const char *text) {
    FILE *file = _wfopen(path, L"wb"); assert(file);
    assert(fwrite(text, 1, strlen(text), file) == strlen(text));
    assert(!fclose(file));
}
static void testParsing(void) {
    HotkeySettings settings = {0};
    HotkeyBinding a, b;
    char text[HOTKEY_TEXT_SIZE];
    int key;
    hotkeyDefaults(&settings);
    assert(settings.bindings[0].keys[VK_F5] && settings.bindings[1].keys[VK_F6] && settings.bindings[2].keys[VK_F7]);
    a = parse("ctrl + w + e + Mouse4 + 7");
    hotkeyFormat(a, text); b = parse(text); assert(!memcmp(&a, &b, sizeof(a)));
    assert(parse("W").keys['W']); assert(parse("7").keys['7']);
    assert(parse("F12").keys[VK_F12]); assert(parse("Escape").keys[VK_ESCAPE]);
    assert(parse("Numpad0 + LeftBracket").keys[VK_NUMPAD0]);
    assert(!hotkeyParse("Q+Q", &b, error));
    assert(!hotkeyParse("Ctrl+", &b, error));
    assert(!hotkeyParse("None+Q", &b, error));
    assert(!hotkeyParse("F25", &b, error));
    settings.bindings[0] = parse("Q"); settings.bindings[1] = parse("Q+E");
    assert(!hotkeyValidate(&settings, error));
    settings.bindings[0] = parse("Q+E"); settings.bindings[1] = parse("Q");
    assert(!hotkeyValidate(&settings, error));
    settings.bindings[1] = settings.bindings[0]; assert(!hotkeyValidate(&settings, error));
    settings.bindings[1] = parse("Q+R"); assert(hotkeyValidate(&settings, error));
    // Exercise the entire representable key space, not a four-key special case.
    memset(&a, 0, sizeof(a));
    for (key = 1; key < HOTKEY_KEY_COUNT; ++key) a.keys[hotkeyNormalize(key)] = 1;
    hotkeyFormat(a, text); b = parse(text); assert(!memcmp(&a, &b, sizeof(a)));
    puts("PASS bare keys, mouse and long chords, formatting, duplicates and subset conflicts");
}
static void testMatching(void) {
    HotkeyMatcher state = {0};
    state.normalize = hotkeyNormalize;
    HotkeySettings settings = {0};
    settings.bindings[0] = parse("Q+E");
    settings.bindings[1] = parse("Ctrl+Mouse4");
    settings.bindings[2] = parse("W");
    hotkeyMatcherApply(&state, &settings);
    assert(hotkeyMatcherInput(&state, 'W', TRUE) == 4);
    assert(!hotkeyMatcherInput(&state, 'W', TRUE));
    assert(!hotkeyMatcherInput(&state, 'E', TRUE));
    assert(hotkeyMatcherInput(&state, 'Q', TRUE) == 1); // Extra W is allowed.
    assert(!hotkeyMatcherInput(&state, 'Q', FALSE));
    assert(hotkeyMatcherInput(&state, 'Q', TRUE) == 1);
    assert(!hotkeyMatcherInput(&state, VK_LCONTROL, TRUE));
    assert(hotkeyMatcherInput(&state, VK_XBUTTON1, TRUE) == 2);
    assert(!hotkeyMatcherInput(&state, VK_RCONTROL, TRUE));
    assert(!hotkeyMatcherInput(&state, VK_LCONTROL, FALSE));
    assert(state.down.keys[VK_CONTROL]);
    assert(!hotkeyMatcherInput(&state, VK_XBUTTON1, TRUE));
    assert(!hotkeyMatcherInput(&state, VK_XBUTTON1, FALSE));
    assert(hotkeyMatcherInput(&state, VK_XBUTTON1, TRUE) == 2);
    hotkeyMatcherApply(&state, &settings);
    assert(!hotkeyMatcherInput(&state, 'W', FALSE));
    assert(!hotkeyMatcherInput(&state, 'W', TRUE)); // Editing waits for release.
    for (int key = 1; key < HOTKEY_KEY_COUNT; ++key) hotkeyMatcherInput(&state, key, FALSE);
    assert(hotkeyMatcherInput(&state, 'W', TRUE) == 4);
    puts("PASS order-independent matching, extra movement keys, repeat suppression and modifier release");
}
static void testRecording(void) {
    HotkeyMatcher state = {0};
    state.normalize = hotkeyNormalize;
    HotkeySettings settings = {0};
    settings.bindings[0] = parse("Q"); hotkeyMatcherApply(&state, &settings);
    hotkeyMatcherInput(&state, VK_LBUTTON, TRUE);
    hotkeyMatcherRecord(&state);
    assert(!hotkeyMatcherInput(&state, VK_LBUTTON, FALSE)); // Initiating click excluded.
    assert(!hotkeyMatcherInput(&state, 'Q', TRUE)); // Bound action suspended.
    hotkeyMatcherInput(&state, 'E', TRUE);
    hotkeyMatcherInput(&state, VK_XBUTTON2, TRUE);
    hotkeyMatcherInput(&state, 'Q', FALSE); // First release freezes the chord.
    hotkeyMatcherInput(&state, 'R', TRUE); // A subsequent press is not a sequence.
    hotkeyMatcherInput(&state, 'E', FALSE);
    hotkeyMatcherInput(&state, VK_XBUTTON2, FALSE);
    assert(state.recording);
    hotkeyMatcherInput(&state, 'R', FALSE);
    assert(!state.recording && state.recorded.keys['Q'] && state.recorded.keys['E']);
    assert(state.recorded.keys[VK_XBUTTON2] && !state.recorded.keys['R'] && !state.recorded.keys[VK_LBUTTON]);
    hotkeyMatcherRecord(&state);
    hotkeyMatcherInput(&state, 'Q', TRUE);
    hotkeyMatcherCancel(&state);
    assert(!hotkeyMatcherInput(&state, 'Q', TRUE));
    assert(!hotkeyMatcherInput(&state, 'Q', FALSE));
    assert(hotkeyMatcherInput(&state, 'Q', TRUE) == 1);
    puts("PASS recording, initiating-click exclusion, first-release boundary, cancellation and suppression");
}
static void testPersistence(void) {
    wchar_t directory[MAX_PATH], path[MAX_PATH], badPath[MAX_PATH];
    HotkeySettings settings = {0}, loaded, previous;
    assert(GetTempPathW(MAX_PATH, directory)); assert(GetTempFileNameW(directory, L"chk", 0, path));
    assert(DeleteFileW(path)); assert(hotkeyLoad(path, &loaded, error));
    assert(loaded.bindings[2].keys[VK_F7]);
    writeText(path, "version=1\nstart=Ctrl+Alt+S\nstop=F6\ntoggle=F7\n");
    assert(hotkeyLoad(path, &loaded, error));
    assert(loaded.bindings[0].keys[VK_CONTROL] && loaded.bindings[0].keys['S']);
    settings.bindings[0] = parse("Q+E+Mouse4"); settings.bindings[1] = parse("7");
    assert(hotkeySave(path, &settings, error)); assert(hotkeyLoad(path, &loaded, error));
    assert(!memcmp(&settings, &loaded, sizeof(settings)));
    previous = loaded;
    writeText(path, "version=99\nstart=W\nstop=F6\ntoggle=F7\n");
    assert(!hotkeyLoad(path, &loaded, error)); assert(!memcmp(&loaded, &previous, sizeof(loaded)));
    writeText(path, "version=2\nstart=Q\nstop=Q+E\ntoggle=F7\n");
    assert(!hotkeyLoad(path, &loaded, error));
    writeText(path, "version=2\nstart=Q\nstop=F6\n"); assert(!hotkeyLoad(path, &loaded, error));
    assert(hotkeySave(path, &settings, error));
    swprintf(badPath, MAX_PATH, L"%ls\\settings.ini", path);
    assert(hotkeysApply(&settings, NULL, error)); previous = matcher.settings;
    settings.bindings[0] = parse("A+B+C");
    assert(!hotkeysApply(&settings, badPath, error));
    assert(!memcmp(&matcher.settings, &previous, sizeof(previous)));
    assert(hotkeyLoad(path, &loaded, error)); assert(!memcmp(&loaded, &previous, sizeof(loaded)));
    assert(DeleteFileW(path));
    puts("PASS version-1 migration, version-2 round trip, malformed files and save-failure rollback");
}
static void testForwarding(void) {
    KBDLLHOOKSTRUCT key = {0};
    MSLLHOOKSTRUCT mouse = {0};
    int before;
    key.vkCode = 'W';
    before = posted;
    assert(keyboardHook(HC_ACTION, WM_KEYDOWN, (LPARAM)&key) == 123);
    assert(posted == before + 1 && LOWORD(postedInput) == 'W' && HIWORD(postedInput));
    assert(keyboardHook(HC_ACTION, WM_KEYUP, (LPARAM)&key) == 123);
    assert(!HIWORD(postedInput));
    assert(keyboardHook(HC_ACTION, WM_SYSKEYDOWN, (LPARAM)&key) == 123);
    assert(keyboardHook(-1, WM_KEYDOWN, 0) == 123);
    mouse.mouseData = XBUTTON2 << 16;
    assert(mouseHook(HC_ACTION, WM_XBUTTONDOWN, (LPARAM)&mouse) == 123);
    assert(LOWORD(postedInput) == VK_XBUTTON2 && HIWORD(postedInput));
    assert(mouseHook(HC_ACTION, WM_XBUTTONUP, (LPARAM)&mouse) == 123);
    assert(!HIWORD(postedInput));
    before = posted;
    assert(mouseHook(HC_ACTION, WM_MOUSEWHEEL, (LPARAM)&mouse) == 123);
    assert(posted == before && forwarded == 7);
    puts("PASS keyboard/mouse down and up always forwarded; wheel left untouched");
}
int main(void) {
    HotkeySettings settings;
    LONG oldGeneration;
    testParsing(); testMatching(); testRecording();
    failInstall = 2;
    assert(!hotkeysOpen(recordAction, error)); assert(removals == 1 && !inputThread && !messageWindow);
    failInstall = 0;
    assert(hotkeysOpen(recordAction, error));
    testPersistence(); testForwarding();
    hotkeyDefaults(&settings); assert(hotkeysApply(&settings, NULL, error));
    memset(matcher.physical, 0, sizeof(matcher.physical)); memset(&matcher.down, 0, sizeof(matcher.down));
    matcher.waitForRelease = FALSE;
    SendMessageW(messageWindow, INPUT_MESSAGE, MAKEWPARAM(VK_F7, TRUE), inputGeneration);
    assert(dispatched[ACTION_TOGGLE_CAPTURE] == 1);
    oldGeneration = inputGeneration;
    hotkeysRecordBegin(recordBinding);
    memset(matcher.physical, 0, sizeof(matcher.physical)); memset(&matcher.down, 0, sizeof(matcher.down));
    matcher.waitForRelease = FALSE;
    SendMessageW(messageWindow, INPUT_MESSAGE, MAKEWPARAM(VK_F7, TRUE), oldGeneration);
    assert(!recordings && dispatched[ACTION_TOGGLE_CAPTURE] == 1);
    SendMessageW(messageWindow, INPUT_MESSAGE, MAKEWPARAM('W', TRUE), inputGeneration);
    SendMessageW(messageWindow, INPUT_MESSAGE, MAKEWPARAM('W', FALSE), inputGeneration);
    assert(finishedRecordings == 1 && lastRecorded.keys['W']);
    hotkeysRecordCancel(); hotkeysClose();
    assert(removals == 3 && !inputThread && !messageWindow);
    assert(hotkeysOpen(recordAction, error)); hotkeysClose(); assert(removals == 5);
    puts("PASS partial hook failure, retry, UI dispatch, stale-event rejection, record delivery and shutdown");
    return 0;
}
