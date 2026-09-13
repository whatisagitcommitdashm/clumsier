#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>
#include <cstring>
#include "platform/macos/hotkeys.h"

static bool check(bool condition, const char *message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    bool ok = true;
    char error[MAC_HOTKEY_ERROR_SIZE]{};
    HotkeyBinding binding{}, parsed{};
    HotkeySettings settings{}, loaded{};

    ok &= check(macHotkeyFromKeyCode(0) == MAC_KEY_A, "ANSI A maps to canonical A");
    ok &= check(macHotkeyFromKeyCode(3) == MAC_KEY_F, "ANSI F maps to canonical F");
    ok &= check(macHotkeyFromKeyCode(122) == MAC_KEY_F1, "F1 maps to canonical F1");
    ok &= check(macHotkeyFromKeyCode(24) == MAC_KEY_EQUALS, "Equals maps to a stable punctuation ID");
    ok &= check(macHotkeyFromKeyCode(39) == MAC_KEY_QUOTE, "Quote maps to a stable punctuation ID");
    ok &= check(macHotkeyFromKeyCode(82) == MAC_KEY_NUMPAD0, "Keypad zero maps separately from the top row");
    ok &= check(macHotkeyFromKeyCode(64) == MAC_KEY_F17, "F17 maps to canonical F17");
    ok &= check(macHotkeyFromKeyCode(114) == MAC_KEY_INSERT, "Help key maps to the Insert binding name");
    ok &= check(macHotkeyFromKeyCode(59) == MAC_KEY_CONTROL, "Left Control is normalized");
    ok &= check(macHotkeyFromKeyCode(62) == MAC_KEY_CONTROL, "Right Control is normalized");
    ok &= check(macHotkeyFromMouseButton(0) == MAC_KEY_MOUSE1, "Left mouse maps to Mouse1");
    ok &= check(macHotkeyFromMouseButton(3) == MAC_KEY_MOUSE4, "First extra mouse button maps to Mouse4");
    ok &= check(macHotkeyFromMouseButton(5) == 0, "Unsupported extra mouse buttons are ignored");
    bool used[HOTKEY_KEY_COUNT]{};
    const unsigned int canonical[] = {
        MAC_KEY_CONTROL, MAC_KEY_OPTION, MAC_KEY_SHIFT, MAC_KEY_COMMAND,
        MAC_KEY_MOUSE1, MAC_KEY_MOUSE2, MAC_KEY_MOUSE3, MAC_KEY_MOUSE4, MAC_KEY_MOUSE5,
        MAC_KEY_BACKSPACE, MAC_KEY_TAB, MAC_KEY_ENTER, MAC_KEY_ESCAPE, MAC_KEY_SPACE,
        MAC_KEY_CAPS_LOCK, MAC_KEY_PAGE_UP, MAC_KEY_PAGE_DOWN, MAC_KEY_END, MAC_KEY_HOME,
        MAC_KEY_LEFT, MAC_KEY_UP, MAC_KEY_RIGHT, MAC_KEY_DOWN, MAC_KEY_DELETE, MAC_KEY_INSERT,
        MAC_KEY_SEMICOLON, MAC_KEY_EQUALS, MAC_KEY_COMMA, MAC_KEY_MINUS, MAC_KEY_PERIOD,
        MAC_KEY_SLASH, MAC_KEY_BACKTICK, MAC_KEY_LEFT_BRACKET, MAC_KEY_BACKSLASH,
        MAC_KEY_RIGHT_BRACKET, MAC_KEY_QUOTE
    };
    for (unsigned int key : canonical) {
        ok &= check(key > 0 && key < HOTKEY_KEY_COUNT && !used[key], "Named canonical IDs are unique");
        if (key < HOTKEY_KEY_COUNT) used[key] = true;
    }
    for (unsigned int key = '0'; key <= '9'; ++key) {
        ok &= check(!used[key], "Digit canonical IDs do not collide"); used[key] = true;
    }
    for (unsigned int key = 'A'; key <= 'Z'; ++key) {
        ok &= check(!used[key], "Letter canonical IDs do not collide"); used[key] = true;
    }
    for (unsigned int key = MAC_KEY_F1; key <= MAC_KEY_F20; ++key) {
        ok &= check(!used[key], "Function-key canonical IDs do not collide"); used[key] = true;
    }
    for (unsigned int key = MAC_KEY_NUMPAD0; key <= MAC_KEY_NUMPAD_EQUALS; ++key) {
        ok &= check(!used[key], "Keypad canonical IDs do not collide"); used[key] = true;
    }

    ok &= check(macHotkeyParse("Ctrl + Q + Mouse4", &binding, error), "Chord parses");
    ok &= check(binding.keys[MAC_KEY_CONTROL] && binding.keys[MAC_KEY_Q] && binding.keys[MAC_KEY_MOUSE4],
        "Parsed chord uses canonical IDs");
    const QByteArray formatted = macHotkeyFormat(binding);
    ok &= check(formatted == "Ctrl + Mouse4 + Q", "Chord formatting is stable");
    ok &= check(macHotkeyParse(formatted.constData(), &parsed, error)
        && !std::memcmp(&binding, &parsed, sizeof(binding)), "Formatted chord round trips");
    HotkeyBinding extended{};
    ok &= check(macHotkeyParse("Semicolon + Numpad7 + F17 + Insert", &extended, error)
        && macHotkeyFormat(extended) == "Insert + Semicolon + F17 + Numpad7",
        "Punctuation, extended function, keypad and Insert names round trip");

    macHotkeyDefaults(&settings);
    ok &= check(settings.bindings[ACTION_START_CAPTURE].keys[MAC_KEY_F5]
        && settings.bindings[ACTION_STOP_CAPTURE].keys[MAC_KEY_F6]
        && settings.bindings[ACTION_TOGGLE_CAPTURE].keys[MAC_KEY_F7], "Defaults match Windows");
    settings.bindings[ACTION_NEXT_STEP] = binding;
    QTemporaryDir directory;
    const QString path = directory.filePath("hotkeys.ini");
    ok &= check(macHotkeySave(path, &settings, error), "Settings save atomically");
    ok &= check(macHotkeyLoad(path, &loaded, error)
        && !std::memcmp(&settings, &loaded, sizeof(settings)), "Settings round trip");

    HotkeySettings conflicts{};
    ok &= check(macHotkeyParse("Q", &conflicts.bindings[0], error)
        && macHotkeyParse("Q+E", &conflicts.bindings[1], error)
        && !macHotkeyValidate(&conflicts, error), "Subset conflicts are rejected");

    MacHotkeyDispatcher dispatcher;
    HotkeySettings runtime{};
    macHotkeyParse("Ctrl+Q", &runtime.bindings[ACTION_START_CAPTURE], error);
    dispatcher.apply(runtime);
    unsigned int fired = 0;
    dispatcher.setActionCallback([&](AppAction action) { fired |= 1u << action; });
    dispatcher.input(macHotkeyPhysicalKey(59), true); // left Ctrl
    dispatcher.input(macHotkeyPhysicalKey(12), true); // Q
    dispatcher.input(macHotkeyPhysicalKey(12), true); // OS repeat
    ok &= check(fired == (1u << ACTION_START_CAPTURE), "Chord fires once without repeat");
    dispatcher.input(macHotkeyPhysicalKey(12), false);
    dispatcher.input(macHotkeyPhysicalKey(12), true);
    ok &= check(fired == (1u << ACTION_START_CAPTURE), "Repeated callback bit remains stable");
    int actionCount = 0;
    dispatcher.setActionCallback([&](AppAction) { ++actionCount; });
    dispatcher.input(macHotkeyPhysicalKey(12), false);
    dispatcher.input(macHotkeyPhysicalKey(12), true);
    ok &= check(actionCount == 1, "Release rearms a held-modifier chord");
    dispatcher.pause();
    dispatcher.input(macHotkeyPhysicalKey(12), false);
    dispatcher.input(macHotkeyPhysicalKey(12), true);
    ok &= check(actionCount == 1, "Paused dispatcher suppresses actions");
    dispatcher.resume();
    dispatcher.input(macHotkeyPhysicalKey(12), false);
    dispatcher.input(macHotkeyPhysicalKey(59), false);
    dispatcher.input(macHotkeyPhysicalKey(59), true);
    dispatcher.input(macHotkeyPhysicalKey(12), false);
    dispatcher.input(macHotkeyPhysicalKey(62), true); // right Ctrl too
    dispatcher.input(macHotkeyPhysicalKey(59), false);
    dispatcher.input(macHotkeyPhysicalKey(12), true);
    ok &= check(actionCount == 2, "Right modifier remains held after left modifier release");

    HotkeyBinding recorded{}; bool recordingFinished = false;
    dispatcher.record([&](const HotkeyBinding &value, bool finished) {
        recorded = value; recordingFinished = finished;
    });
    dispatcher.input(macHotkeyPhysicalKey(12), false);
    dispatcher.input(macHotkeyPhysicalKey(62), false);
    dispatcher.input(macHotkeyPhysicalKey(0), true);  // A
    dispatcher.input(macHotkeyPhysicalKey(3), true);  // F
    dispatcher.input(macHotkeyPhysicalKey(0), false);
    dispatcher.input(macHotkeyPhysicalKey(3), false);
    ok &= check(recordingFinished && recorded.keys[MAC_KEY_A] && recorded.keys[MAC_KEY_F],
        "Recording captures a chord and finishes after all releases");

    if (ok) std::puts("PASS macOS key mapping, binding format, validation and persistence");
    return ok ? 0 : 1;
}
