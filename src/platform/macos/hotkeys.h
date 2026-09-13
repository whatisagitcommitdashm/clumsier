#pragma once
#include <QByteArray>
#include <QString>
#include <functional>
#include <memory>
class QObject;
extern "C" {
#include "core/input.h"
#include "core/hotkey_matcher.h"
}

#define MAC_HOTKEY_ERROR_SIZE 256

// Canonical IDs are stable file-format values, not macOS virtual key codes.
// Letters and digits use ASCII; named keys occupy otherwise-unused byte values.
enum MacHotkeyKey : unsigned int {
    MAC_KEY_CONTROL = 1, MAC_KEY_OPTION = 2, MAC_KEY_SHIFT = 3, MAC_KEY_COMMAND = 4,
    MAC_KEY_MOUSE1 = 5, MAC_KEY_MOUSE2 = 6, MAC_KEY_MOUSE3 = 7,
    MAC_KEY_MOUSE4 = 8, MAC_KEY_MOUSE5 = 9,
    MAC_KEY_BACKSPACE = 16, MAC_KEY_TAB = 17, MAC_KEY_ENTER = 18,
    MAC_KEY_ESCAPE = 19, MAC_KEY_SPACE = 20, MAC_KEY_CAPS_LOCK = 21,
    MAC_KEY_PAGE_UP = 22, MAC_KEY_PAGE_DOWN = 23, MAC_KEY_END = 24,
    MAC_KEY_HOME = 25, MAC_KEY_LEFT = 26, MAC_KEY_UP = 27,
    MAC_KEY_RIGHT = 28, MAC_KEY_DOWN = 29, MAC_KEY_DELETE = 30,
    MAC_KEY_INSERT = 31,
    MAC_KEY_A = 'A', MAC_KEY_B = 'B', MAC_KEY_C = 'C', MAC_KEY_D = 'D',
    MAC_KEY_E = 'E', MAC_KEY_F = 'F', MAC_KEY_G = 'G', MAC_KEY_H = 'H',
    MAC_KEY_I = 'I', MAC_KEY_J = 'J', MAC_KEY_K = 'K', MAC_KEY_L = 'L',
    MAC_KEY_M = 'M', MAC_KEY_N = 'N', MAC_KEY_O = 'O', MAC_KEY_P = 'P',
    MAC_KEY_Q = 'Q', MAC_KEY_R = 'R', MAC_KEY_S = 'S', MAC_KEY_T = 'T',
    MAC_KEY_U = 'U', MAC_KEY_V = 'V', MAC_KEY_W = 'W', MAC_KEY_X = 'X',
    MAC_KEY_Y = 'Y', MAC_KEY_Z = 'Z',
    MAC_KEY_SEMICOLON = 96, MAC_KEY_EQUALS = 97, MAC_KEY_COMMA = 98,
    MAC_KEY_MINUS = 99, MAC_KEY_PERIOD = 100, MAC_KEY_SLASH = 101,
    MAC_KEY_BACKTICK = 102, MAC_KEY_LEFT_BRACKET = 103,
    MAC_KEY_BACKSLASH = 104, MAC_KEY_RIGHT_BRACKET = 105, MAC_KEY_QUOTE = 106,
    MAC_KEY_F1 = 128, MAC_KEY_F2 = 129, MAC_KEY_F3 = 130, MAC_KEY_F4 = 131,
    MAC_KEY_F5 = 132, MAC_KEY_F6 = 133, MAC_KEY_F7 = 134, MAC_KEY_F8 = 135,
    MAC_KEY_F9 = 136, MAC_KEY_F10 = 137, MAC_KEY_F11 = 138, MAC_KEY_F12 = 139,
    MAC_KEY_F13 = 140, MAC_KEY_F14 = 141, MAC_KEY_F15 = 142, MAC_KEY_F16 = 143,
    MAC_KEY_F17 = 144, MAC_KEY_F18 = 145, MAC_KEY_F19 = 146, MAC_KEY_F20 = 147,
    MAC_KEY_NUMPAD0 = 160, MAC_KEY_NUMPAD1 = 161, MAC_KEY_NUMPAD2 = 162,
    MAC_KEY_NUMPAD3 = 163, MAC_KEY_NUMPAD4 = 164, MAC_KEY_NUMPAD5 = 165,
    MAC_KEY_NUMPAD6 = 166, MAC_KEY_NUMPAD7 = 167, MAC_KEY_NUMPAD8 = 168,
    MAC_KEY_NUMPAD9 = 169, MAC_KEY_NUMPAD_DECIMAL = 170,
    MAC_KEY_NUMPAD_MULTIPLY = 171, MAC_KEY_NUMPAD_ADD = 172,
    MAC_KEY_NUMPAD_CLEAR = 173, MAC_KEY_NUMPAD_DIVIDE = 174,
    MAC_KEY_NUMPAD_ENTER = 175, MAC_KEY_NUMPAD_SUBTRACT = 176,
    MAC_KEY_NUMPAD_EQUALS = 177
};

unsigned int macHotkeyFromKeyCode(unsigned int keyCode);
unsigned int macHotkeyFromMouseButton(unsigned int buttonNumber);
void macHotkeyDefaults(HotkeySettings *settings);
bool macHotkeyParse(const char *text, HotkeyBinding *binding, char error[MAC_HOTKEY_ERROR_SIZE]);
QByteArray macHotkeyFormat(HotkeyBinding binding);
bool macHotkeyValidate(const HotkeySettings *settings, char error[MAC_HOTKEY_ERROR_SIZE]);
bool macHotkeyLoad(const QString &path, HotkeySettings *settings, char error[MAC_HOTKEY_ERROR_SIZE]);
bool macHotkeySave(const QString &path, const HotkeySettings *settings, char error[MAC_HOTKEY_ERROR_SIZE]);

// Converts a native virtual key code into the matcher's physical namespace.
// Left/right modifiers remain distinct until the matcher normalizes them.
unsigned int macHotkeyPhysicalKey(unsigned int keyCode);

class MacHotkeyDispatcher {
public:
    using ActionCallback = std::function<void(AppAction)>;
    using RecordCallback = std::function<void(const HotkeyBinding &, bool)>;
    MacHotkeyDispatcher();
    void apply(const HotkeySettings &settings);
    void setActionCallback(ActionCallback callback) { actionCallback_ = std::move(callback); }
    void input(unsigned int physicalKey, bool down);
    void reset();
    void pause();
    void resume();
    void record(RecordCallback callback);
    void cancelRecord();
private:
    HotkeyMatcher matcher_{};
    ActionCallback actionCallback_;
    RecordCallback recordCallback_;
    unsigned int pauseDepth_ = 0;
    void synchronize();
};

// Owns a passive session event tap on a dedicated run-loop thread. Native
// callbacks only copy key/button state; matching and application callbacks are
// delivered through deliveryContext on its Qt thread.
class MacHotkeyListener {
public:
    using ActionCallback = MacHotkeyDispatcher::ActionCallback;
    using RecordCallback = MacHotkeyDispatcher::RecordCallback;
    explicit MacHotkeyListener(QObject *deliveryContext);
    ~MacHotkeyListener();
    MacHotkeyListener(const MacHotkeyListener &) = delete;
    MacHotkeyListener &operator=(const MacHotkeyListener &) = delete;

    bool open(ActionCallback callback, bool requestPermission,
        char error[MAC_HOTKEY_ERROR_SIZE]);
    void close();
    bool isOpen() const;
    bool apply(const HotkeySettings &settings, const QString &path,
        char error[MAC_HOTKEY_ERROR_SIZE]);
    void pause();
    void resume();
    void record(RecordCallback callback);
    void cancelRecord();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
