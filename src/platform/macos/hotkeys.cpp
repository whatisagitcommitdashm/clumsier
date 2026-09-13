#include "hotkeys.h"
#include <QFile>
#include <QSaveFile>
#include <QStringList>
#include <cstring>
extern "C" {
#include "core/actions.h"
}

namespace {
enum : unsigned int {
    PHYSICAL_LEFT_CONTROL = 220, PHYSICAL_RIGHT_CONTROL,
    PHYSICAL_LEFT_OPTION, PHYSICAL_RIGHT_OPTION,
    PHYSICAL_LEFT_SHIFT, PHYSICAL_RIGHT_SHIFT,
    PHYSICAL_LEFT_COMMAND, PHYSICAL_RIGHT_COMMAND
};
struct KeyName { unsigned int key; const char *name; };
const KeyName names[] = {
    {MAC_KEY_CONTROL,"Ctrl"}, {MAC_KEY_OPTION,"Alt"}, {MAC_KEY_SHIFT,"Shift"}, {MAC_KEY_COMMAND,"Meta"},
    {MAC_KEY_MOUSE1,"Mouse1"}, {MAC_KEY_MOUSE2,"Mouse2"}, {MAC_KEY_MOUSE3,"Mouse3"},
    {MAC_KEY_MOUSE4,"Mouse4"}, {MAC_KEY_MOUSE5,"Mouse5"},
    {MAC_KEY_BACKSPACE,"Backspace"}, {MAC_KEY_TAB,"Tab"}, {MAC_KEY_ENTER,"Enter"},
    {MAC_KEY_ESCAPE,"Escape"}, {MAC_KEY_SPACE,"Space"}, {MAC_KEY_CAPS_LOCK,"CapsLock"},
    {MAC_KEY_PAGE_UP,"PageUp"}, {MAC_KEY_PAGE_DOWN,"PageDown"}, {MAC_KEY_END,"End"},
    {MAC_KEY_HOME,"Home"}, {MAC_KEY_LEFT,"Left"}, {MAC_KEY_UP,"Up"},
    {MAC_KEY_RIGHT,"Right"}, {MAC_KEY_DOWN,"Down"}, {MAC_KEY_DELETE,"Delete"},
    {MAC_KEY_INSERT,"Insert"},
    {MAC_KEY_SEMICOLON,"Semicolon"}, {MAC_KEY_EQUALS,"Equals"}, {MAC_KEY_COMMA,"Comma"},
    {MAC_KEY_MINUS,"Minus"}, {MAC_KEY_PERIOD,"Period"}, {MAC_KEY_SLASH,"Slash"},
    {MAC_KEY_BACKTICK,"Backtick"}, {MAC_KEY_LEFT_BRACKET,"LeftBracket"},
    {MAC_KEY_BACKSLASH,"Backslash"}, {MAC_KEY_RIGHT_BRACKET,"RightBracket"},
    {MAC_KEY_QUOTE,"Quote"},
    {MAC_KEY_NUMPAD_DECIMAL,"NumpadDecimal"}, {MAC_KEY_NUMPAD_MULTIPLY,"NumpadMultiply"},
    {MAC_KEY_NUMPAD_ADD,"NumpadAdd"}, {MAC_KEY_NUMPAD_CLEAR,"NumpadClear"},
    {MAC_KEY_NUMPAD_DIVIDE,"NumpadDivide"}, {MAC_KEY_NUMPAD_ENTER,"NumpadEnter"},
    {MAC_KEY_NUMPAD_SUBTRACT,"NumpadSubtract"}, {MAC_KEY_NUMPAD_EQUALS,"NumpadEquals"}
};
const char *settingNames[ACTION_COUNT] = {"start", "stop", "toggle", "next_step", "previous_step", "reset_sequence"};

void setError(char *error, const char *text) {
    if (error) { std::strncpy(error, text, MAC_HOTKEY_ERROR_SIZE - 1); error[MAC_HOTKEY_ERROR_SIZE - 1] = 0; }
}
QByteArray keyName(unsigned int key) {
    for (const auto &entry : names) if (entry.key == key) return entry.name;
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) return QByteArray(1, char(key));
    if (key >= MAC_KEY_F1 && key <= MAC_KEY_F20) return "F" + QByteArray::number(key - MAC_KEY_F1 + 1);
    if (key >= MAC_KEY_NUMPAD0 && key <= MAC_KEY_NUMPAD9)
        return "Numpad" + QByteArray::number(key - MAC_KEY_NUMPAD0);
    return {};
}
unsigned int keyForName(const QByteArray &name) {
    for (unsigned int key = 1; key < HOTKEY_KEY_COUNT; ++key)
        if (!keyName(key).isEmpty() && keyName(key).compare(name, Qt::CaseInsensitive) == 0) return key;
    return 0;
}
unsigned int normalizePhysical(unsigned int key) {
    switch (key) {
        case PHYSICAL_LEFT_CONTROL: case PHYSICAL_RIGHT_CONTROL: return MAC_KEY_CONTROL;
        case PHYSICAL_LEFT_OPTION: case PHYSICAL_RIGHT_OPTION: return MAC_KEY_OPTION;
        case PHYSICAL_LEFT_SHIFT: case PHYSICAL_RIGHT_SHIFT: return MAC_KEY_SHIFT;
        case PHYSICAL_LEFT_COMMAND: case PHYSICAL_RIGHT_COMMAND: return MAC_KEY_COMMAND;
        default: return key;
    }
}
}

unsigned int macHotkeyFromKeyCode(unsigned int code) {
    static const unsigned int map[128] = {
        ['\x00']=MAC_KEY_A, ['\x01']=MAC_KEY_S, ['\x02']=MAC_KEY_D, ['\x03']=MAC_KEY_F,
        ['\x04']=MAC_KEY_H, ['\x05']=MAC_KEY_G, ['\x06']=MAC_KEY_Z, ['\x07']=MAC_KEY_X,
        ['\x08']=MAC_KEY_C, ['\x09']=MAC_KEY_V, ['\x0b']=MAC_KEY_B, ['\x0c']=MAC_KEY_Q,
        ['\x0d']=MAC_KEY_W, ['\x0e']=MAC_KEY_E, ['\x0f']=MAC_KEY_R, ['\x10']=MAC_KEY_Y,
        ['\x11']=MAC_KEY_T, ['\x12']='1', ['\x13']='2', ['\x14']='3', ['\x15']='4',
        ['\x16']='6', ['\x17']='5', ['\x18']=MAC_KEY_EQUALS, ['\x19']='9', ['\x1a']='7',
        ['\x1b']=MAC_KEY_MINUS, ['\x1c']='8', ['\x1d']='0', ['\x1e']=MAC_KEY_RIGHT_BRACKET,
        ['\x1f']=MAC_KEY_O, ['\x20']=MAC_KEY_U, ['\x21']=MAC_KEY_LEFT_BRACKET,
        ['\x22']=MAC_KEY_I, ['\x23']=MAC_KEY_P, ['\x24']=MAC_KEY_ENTER,
        ['\x25']=MAC_KEY_L, ['\x26']=MAC_KEY_J, ['\x27']=MAC_KEY_QUOTE,
        ['\x28']=MAC_KEY_K, ['\x29']=MAC_KEY_SEMICOLON, ['\x2a']=MAC_KEY_BACKSLASH,
        ['\x2b']=MAC_KEY_COMMA, ['\x2c']=MAC_KEY_SLASH, ['\x2d']=MAC_KEY_N,
        ['\x2e']=MAC_KEY_M, ['\x2f']=MAC_KEY_PERIOD, ['\x30']=MAC_KEY_TAB,
        ['\x31']=MAC_KEY_SPACE, ['\x32']=MAC_KEY_BACKTICK,
        ['\x33']=MAC_KEY_BACKSPACE, ['\x35']=MAC_KEY_ESCAPE, ['\x36']=MAC_KEY_COMMAND,
        ['\x37']=MAC_KEY_COMMAND, ['\x38']=MAC_KEY_SHIFT, ['\x39']=MAC_KEY_CAPS_LOCK,
        ['\x3a']=MAC_KEY_OPTION, ['\x3b']=MAC_KEY_CONTROL, ['\x3c']=MAC_KEY_SHIFT,
        ['\x3d']=MAC_KEY_OPTION, ['\x3e']=MAC_KEY_CONTROL,
        ['\x40']=MAC_KEY_F17, ['\x41']=MAC_KEY_NUMPAD_DECIMAL,
        ['\x43']=MAC_KEY_NUMPAD_MULTIPLY, ['\x45']=MAC_KEY_NUMPAD_ADD,
        ['\x47']=MAC_KEY_NUMPAD_CLEAR, ['\x4b']=MAC_KEY_NUMPAD_DIVIDE,
        ['\x4c']=MAC_KEY_NUMPAD_ENTER, ['\x4e']=MAC_KEY_NUMPAD_SUBTRACT,
        ['\x4f']=MAC_KEY_F18, ['\x50']=MAC_KEY_F19, ['\x51']=MAC_KEY_NUMPAD_EQUALS,
        ['\x52']=MAC_KEY_NUMPAD0, ['\x53']=MAC_KEY_NUMPAD1, ['\x54']=MAC_KEY_NUMPAD2,
        ['\x55']=MAC_KEY_NUMPAD3, ['\x56']=MAC_KEY_NUMPAD4, ['\x57']=MAC_KEY_NUMPAD5,
        ['\x58']=MAC_KEY_NUMPAD6, ['\x59']=MAC_KEY_NUMPAD7, ['\x5a']=MAC_KEY_F20,
        ['\x5b']=MAC_KEY_NUMPAD8, ['\x5c']=MAC_KEY_NUMPAD9,
        ['\x60']=MAC_KEY_F5, ['\x61']=MAC_KEY_F6, ['\x62']=MAC_KEY_F7, ['\x63']=MAC_KEY_F3,
        ['\x64']=MAC_KEY_F8, ['\x65']=MAC_KEY_F9, ['\x67']=MAC_KEY_F11, ['\x69']=MAC_KEY_F13,
        ['\x6a']=MAC_KEY_F16, ['\x6b']=MAC_KEY_F14, ['\x6d']=MAC_KEY_F10, ['\x6f']=MAC_KEY_F12,
        ['\x71']=MAC_KEY_F15, ['\x72']=MAC_KEY_INSERT, ['\x73']=MAC_KEY_HOME, ['\x74']=MAC_KEY_PAGE_UP,
        ['\x75']=MAC_KEY_DELETE, ['\x76']=MAC_KEY_F4, ['\x77']=MAC_KEY_END, ['\x78']=MAC_KEY_F2,
        ['\x79']=MAC_KEY_PAGE_DOWN, ['\x7a']=MAC_KEY_F1, ['\x7b']=MAC_KEY_LEFT,
        ['\x7c']=MAC_KEY_RIGHT, ['\x7d']=MAC_KEY_DOWN, ['\x7e']=MAC_KEY_UP
    };
    return code < 128 ? map[code] : 0;
}

unsigned int macHotkeyFromMouseButton(unsigned int button) {
    static const unsigned int buttons[] = {MAC_KEY_MOUSE1, MAC_KEY_MOUSE2, MAC_KEY_MOUSE3, MAC_KEY_MOUSE4, MAC_KEY_MOUSE5};
    return button < sizeof(buttons)/sizeof(buttons[0]) ? buttons[button] : 0;
}

unsigned int macHotkeyPhysicalKey(unsigned int code) {
    switch (code) {
        case 0x3b: return PHYSICAL_LEFT_CONTROL;
        case 0x3e: return PHYSICAL_RIGHT_CONTROL;
        case 0x3a: return PHYSICAL_LEFT_OPTION;
        case 0x3d: return PHYSICAL_RIGHT_OPTION;
        case 0x38: return PHYSICAL_LEFT_SHIFT;
        case 0x3c: return PHYSICAL_RIGHT_SHIFT;
        case 0x37: return PHYSICAL_LEFT_COMMAND;
        case 0x36: return PHYSICAL_RIGHT_COMMAND;
        default: return macHotkeyFromKeyCode(code);
    }
}

void macHotkeyDefaults(HotkeySettings *settings) {
    std::memset(settings, 0, sizeof(*settings));
    settings->bindings[ACTION_START_CAPTURE].keys[MAC_KEY_F5] = 1;
    settings->bindings[ACTION_STOP_CAPTURE].keys[MAC_KEY_F6] = 1;
    settings->bindings[ACTION_TOGGLE_CAPTURE].keys[MAC_KEY_F7] = 1;
}

bool macHotkeyParse(const char *text, HotkeyBinding *binding, char *error) {
    HotkeyBinding parsed{};
    const QString input = QString::fromUtf8(text ? text : "").trimmed();
    if (input.isEmpty() || input.compare("None", Qt::CaseInsensitive) == 0) { *binding = parsed; return true; }
    for (const QString &part : input.split('+')) {
        const unsigned int key = keyForName(part.trimmed().toUtf8());
        if (!key || parsed.keys[key]) { setError(error, "Invalid or repeated key name. Use the recorder to choose a combination."); return false; }
        parsed.keys[key] = 1;
    }
    *binding = parsed;
    return true;
}

QByteArray macHotkeyFormat(HotkeyBinding binding) {
    QList<QByteArray> parts;
    const unsigned int modifiers[] = {MAC_KEY_CONTROL, MAC_KEY_OPTION, MAC_KEY_SHIFT, MAC_KEY_COMMAND};
    for (unsigned int key : modifiers) { if (binding.keys[key]) parts.append(keyName(key)); binding.keys[key] = 0; }
    for (unsigned int key = 1; key < HOTKEY_KEY_COUNT; ++key)
        if (binding.keys[key]) parts.append(keyName(key));
    return parts.isEmpty() ? QByteArray("None") : parts.join(" + ");
}

bool macHotkeyValidate(const HotkeySettings *settings, char *error) {
    for (int i = 0; i < ACTION_COUNT; ++i) {
        const HotkeyBinding *binding = &settings->bindings[i];
        if (binding->keys[0]) { setError(error, "Invalid hotkey settings."); return false; }
        for (unsigned int key = 1; key < HOTKEY_KEY_COUNT; ++key)
            if (binding->keys[key] > 1 || (binding->keys[key] && keyName(key).isEmpty())) {
                setError(error, "Invalid hotkey settings."); return false;
            }
        if (hotkeyEmpty(binding)) continue;
        for (int j = 0; j < i; ++j) {
            const HotkeyBinding *other = &settings->bindings[j];
            if (!hotkeyEmpty(other) && (hotkeyContains(binding, other) || hotkeyContains(other, binding))) {
                const QByteArray message = QByteArray(actionName(AppAction(j))) + " and " + actionName(AppAction(i))
                    + " overlap: one combination contains the other. Choose distinct combinations.";
                setError(error, message.constData()); return false;
            }
        }
    }
    return true;
}

bool macHotkeyLoad(const QString &path, HotkeySettings *settings, char *error) {
    QFile file(path);
    if (!file.exists()) { macHotkeyDefaults(settings); return true; }
    if (!file.open(QIODevice::ReadOnly) || file.size() > MAC_HOTKEY_ERROR_SIZE * ACTION_COUNT * 4) {
        setError(error, "Cannot read hotkey settings; defaults are shown."); return false;
    }
    const QByteArray contents = file.readAll();
    if (contents.contains('\0')) { setError(error, "Invalid hotkeys.ini; defaults are shown. Apply & Save explicitly to replace it."); return false; }
    HotkeySettings loaded{}; unsigned int seen = 0; int version = 0;
    for (QByteArray line : contents.split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty()) continue;
        const qsizetype equals = line.indexOf('=');
        if (equals <= 0) goto invalid;
        const QByteArray key = line.left(equals), value = line.mid(equals + 1);
        if (key == "version") { if (version || (value != "1" && value != "2" && value != "3")) goto invalid; version = value.toInt(); continue; }
        int action = -1;
        for (int i = 0; i < ACTION_COUNT; ++i) if (key == settingNames[i]) { action = i; break; }
        if (action < 0 || (seen & (1u << action)) || !macHotkeyParse(value.constData(), &loaded.bindings[action], error)) goto invalid;
        seen |= 1u << action;
    }
    if (!version || seen != (version < 3 ? 7u : (1u << ACTION_COUNT) - 1u) || !macHotkeyValidate(&loaded, error)) goto invalid;
    *settings = loaded; return true;
invalid:
    setError(error, "Invalid hotkeys.ini; defaults are shown. Apply & Save explicitly to replace it."); return false;
}

bool macHotkeySave(const QString &path, const HotkeySettings *settings, char *error) {
    if (!macHotkeyValidate(settings, error)) return false;
    QByteArray contents("version=3\n");
    for (int i = 0; i < ACTION_COUNT; ++i)
        contents += QByteArray(settingNames[i]) + '=' + macHotkeyFormat(settings->bindings[i]) + '\n';
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        setError(error, "Could not save hotkeys. The previous bindings are still active."); return false;
    }
    return true;
}

MacHotkeyDispatcher::MacHotkeyDispatcher() {
    matcher_.normalize = normalizePhysical;
}

void MacHotkeyDispatcher::synchronize() {
    std::memset(matcher_.physical, 0, sizeof(matcher_.physical));
    std::memset(&matcher_.down, 0, sizeof(matcher_.down));
    hotkeyMatcherCancel(&matcher_);
}

void MacHotkeyDispatcher::apply(const HotkeySettings &settings) {
    synchronize();
    matcher_.settings = settings;
}

void MacHotkeyDispatcher::input(unsigned int physicalKey, bool down) {
    const bool wasRecording = matcher_.recording;
    const unsigned int actions = hotkeyMatcherInput(&matcher_, physicalKey, down);
    if (wasRecording && recordCallback_) {
        auto callback = recordCallback_;
        if (!matcher_.recording) recordCallback_ = {};
        callback(matcher_.recorded, !matcher_.recording);
    }
    if (!pauseDepth_ && actionCallback_)
        for (int action = 0; action < ACTION_COUNT; ++action)
            if (actions & (1u << action)) actionCallback_(AppAction(action));
}

void MacHotkeyDispatcher::reset() { synchronize(); }

void MacHotkeyDispatcher::pause() {
    ++pauseDepth_;
    synchronize();
}

void MacHotkeyDispatcher::resume() {
    if (pauseDepth_) --pauseDepth_;
    synchronize();
}

void MacHotkeyDispatcher::record(RecordCallback callback) {
    synchronize();
    hotkeyMatcherRecord(&matcher_);
    recordCallback_ = std::move(callback);
}

void MacHotkeyDispatcher::cancelRecord() {
    recordCallback_ = {};
    synchronize();
}
