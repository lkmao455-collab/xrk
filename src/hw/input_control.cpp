#include "input_control.h"
#include "core/logger.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef min
#undef max
#endif

#ifdef __linux__
#include <X11/extensions/XTest.h>
#endif

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
#endif

namespace xrk {

static QVector<INPUT> buildKeyInputs(uint32_t keyCode, bool pressed,
                                     const QString& text);
static QVector<INPUT> buildMouseMoveInputs(int x, int y);
static QVector<INPUT> buildMouseButtonInputs(MouseButton button, bool pressed);
static QVector<INPUT> buildMouseScrollInputs(int delta);

bool InputControl::isExtendedKey(uint32_t keyCode) {
#ifdef _WIN32
    // Keys whose hardware scan code carries the 0xE0 prefix must be injected
    // with KEYEVENTF_EXTENDEDKEY, otherwise they are delivered as the wrong
    // (non-extended) key. This is a fixed set, so a table is more reliable than
    // MapVirtualKey (which depends on the loaded keyboard layout and can return
    // 0 in headless / service environments).
    switch (keyCode) {
        case VK_RCONTROL:   // 0xA3
        case VK_RMENU:      // 0xA5
        case VK_LWIN:       // 0x5B
        case VK_RWIN:       // 0x5C
        case VK_APPS:       // 0x5D
        case VK_LEFT:       // 0x25
        case VK_UP:         // 0x26
        case VK_RIGHT:      // 0x27
        case VK_DOWN:       // 0x28
        case VK_HOME:       // 0x24
        case VK_END:        // 0x23
        case VK_PRIOR:      // 0x21 (PageUp)
        case VK_NEXT:       // 0x22 (PageDown)
        case VK_INSERT:     // 0x2D
        case VK_DELETE:     // 0x2E
        case VK_NUMLOCK:    // 0x90
        case VK_DIVIDE:     // 0x6F (numpad /)
        case VK_SNAPSHOT:   // 0x2C (PrintScreen)
            return true;
        default:
            return false;
    }
#else
    Q_UNUSED(keyCode);
    return false;
#endif
}

QVector<uint16_t> InputControl::unicodeUnits(const QString& text) {
    QVector<uint16_t> units;
    for (int i = 0; i < text.size(); ++i) {
        QChar c = text.at(i);
        if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            units.append(c.unicode());
            units.append(text.at(++i).unicode());
        } else if (c.isLowSurrogate()) {
            continue; // stray low surrogate, skip
        } else {
            units.append(c.unicode());
        }
    }
    return units;
}

uint32_t InputControl::mouseButtonFlags(MouseButton button, bool pressed) {
    switch (button) {
        case MouseButton::LEFT:   return pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        case MouseButton::RIGHT:  return pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        case MouseButton::MIDDLE: return pressed ? 0x0020 : 0x0040;
    }
    return 0;
}

QPoint InputControl::mouseNormalizedPos(int x, int y) {
#ifdef _WIN32
    int cx = GetSystemMetrics(SM_CXSCREEN);
    int cy = GetSystemMetrics(SM_CYSCREEN);
    int dx = (cx > 0) ? static_cast<int>(x * 65535 / cx) : 0;
    int dy = (cy > 0) ? static_cast<int>(y * 65535 / cy) : 0;
    return QPoint(dx, dy);
#else
    Q_UNUSED(x);
    Q_UNUSED(y);
    return QPoint(0, 0);
#endif
}

uint32_t InputControl::mouseScrollFlags() {
    return MOUSEEVENTF_WHEEL;
}

int InputControl::mouseScrollData(int delta) {
    return delta;
}

InputControl::InputControl(QObject* parent) : QObject(parent) {
}

InputControl::~InputControl() {
    shutdown();
}

bool InputControl::initialize() {
    if (m_initialized) {
        return true;
    }
    
#ifdef __linux__
    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        QString msg = tr("Linux: Cannot open X display for input");
        LOG_ERROR(msg);
        emit inputError(msg);
        return false;
    }
#endif

    m_initialized = true;
    LOG_INFO("InputControl initialized");
    return true;
}

void InputControl::shutdown() {
    if (!m_initialized) {
        return;
    }

#ifdef _WIN32
    // Failsafe: never leave the local console locked if we are torn down while
    // a silent session had blocked input (plan §2.2c risk: process crash/stop).
    if (m_localInputBlocked) {
        BlockInput(FALSE);
        m_localInputBlocked = false;
    }
#endif

#ifdef __linux__
    if (m_display) {
        XCloseDisplay(m_display);
        m_display = nullptr;
    }
#endif

    m_initialized = false;
    LOG_INFO("InputControl shutdown");
}

void InputControl::setLocalInputBlocked(bool blocked) {
#ifdef _WIN32
    if (blocked == m_localInputBlocked) {
        return; // idempotent
    }
    if (!BlockInput(blocked ? TRUE : FALSE)) {
        // BlockInput can fail under UAC/secure desktop or without privileges.
        // Treated as best-effort; not surfaced to the user as a fatal error.
        LOG_WARNING("InputControl: BlockInput(" + QString(blocked ? "TRUE" : "FALSE") +
                    ") failed (error " + QString::number(GetLastError()) + ")");
        return;
    }
    m_localInputBlocked = blocked;
#elif defined(__linux__)
    // X11 has no global input lock equivalent; best-effort no-op for now.
    m_localInputBlocked = blocked;
#elif defined(__APPLE__)
    m_localInputBlocked = blocked;
#else
    m_localInputBlocked = blocked;
#endif
}

void InputControl::simulateMouseMove(int x, int y) {
    if (!m_initialized) return;
    mouseMoveEvent(x, y);
}

void InputControl::simulateMouseButton(MouseButton button, bool pressed) {
    if (!m_initialized) return;
    mouseButtonEvent(button, pressed);
}

void InputControl::simulateMouseClick(MouseButton button, int x, int y) {
    if (!m_initialized) return;
    mouseMoveEvent(x, y);
    mouseButtonEvent(button, true);
    mouseButtonEvent(button, false);
}

void InputControl::simulateMouseDoubleClick(MouseButton button, int x, int y) {
    if (!m_initialized) return;
    mouseMoveEvent(x, y);
    mouseButtonEvent(button, true);
    mouseButtonEvent(button, false);
    mouseButtonEvent(button, true);
    mouseButtonEvent(button, false);
}

void InputControl::simulateMouseScroll(int delta, int x, int y) {
    if (!m_initialized) return;
    mouseMoveEvent(x, y);
    mouseScrollEvent(delta);
}

void InputControl::simulateKeyPress(uint32_t keyCode) {
    if (!m_initialized) return;
    keyEvent(keyCode, true);
}

void InputControl::simulateKeyRelease(uint32_t keyCode) {
    if (!m_initialized) return;
    keyEvent(keyCode, false);
}

void InputControl::simulateText(const QString& text) {
    if (!m_initialized) return;
    keyEvent(0, true, text);
    keyEvent(0, false, text);
}

void InputControl::simulateKeyCombo(uint32_t keyCode, uint32_t modifiers) {
    if (!m_initialized) return;
    
    if (modifiers & 0x0001) keyEvent(0x11, true);  // Ctrl
    if (modifiers & 0x0002) keyEvent(0x10, true);  // Shift
    if (modifiers & 0x0004) keyEvent(0x12, true);  // Alt
    if (modifiers & 0x0008) keyEvent(0x5B, true);  // Win
    
    keyEvent(keyCode, true);
    keyEvent(keyCode, false);
    
    if (modifiers & 0x0001) keyEvent(0x11, false);
    if (modifiers & 0x0002) keyEvent(0x10, false);
    if (modifiers & 0x0004) keyEvent(0x12, false);
    if (modifiers & 0x0008) keyEvent(0x5B, false);
}

void InputControl::processMouseEvent(const MouseEvent& event) {
    if (!m_initialized) return;
    
    switch (event.action) {
        case MouseAction::MOVE:
            mouseMoveEvent(event.x, event.y);
            break;
        case MouseAction::CLICK:
            mouseMoveEvent(event.x, event.y);
            mouseButtonEvent(event.button, true);
            mouseButtonEvent(event.button, false);
            break;
        case MouseAction::DOUBLECLICK:
            simulateMouseDoubleClick(event.button, event.x, event.y);
            break;
        case MouseAction::PRESS:
            mouseMoveEvent(event.x, event.y);
            mouseButtonEvent(event.button, true);
            break;
        case MouseAction::RELEASE:
            mouseMoveEvent(event.x, event.y);
            mouseButtonEvent(event.button, false);
            break;
        case MouseAction::SCROLL:
            mouseMoveEvent(event.x, event.y);
            mouseScrollEvent(event.delta);
            break;
    }
}

void InputControl::processKeyEvent(const KeyEvent& event) {
    if (!m_initialized) return;

    static int keyEventCount = 0;
    keyEventCount++;
    if (keyEventCount <= 20 || keyEventCount % 500 == 0) {
        LOG_INFO("[Input] key#" + QString::number(keyEventCount) +
                 " code=0x" + QString::number(event.keyCode, 16) +
                 (event.pressed ? " DOWN" : " UP") +
                 " text=\"" + event.text + "\"");
    }

    keyEvent(event.keyCode, event.pressed, event.text);
}

void InputControl::mouseMoveEvent(int x, int y) {
#ifdef _WIN32
    QVector<INPUT> inputs = buildMouseMoveInputs(x, y);
    if (!inputs.isEmpty()) {
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }
#elif defined(__linux__)
    if (m_display) {
        XTestFakeMotionEvent(m_display, -1, x, y, CurrentTime);
        XFlush(m_display);
    }
#elif defined(__APPLE__)
    CGEventRef event = CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved,
                                                CGPointMake(x, y), 0);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
#endif
}

void InputControl::mouseButtonEvent(MouseButton button, bool pressed) {
#ifdef _WIN32
    QVector<INPUT> inputs = buildMouseButtonInputs(button, pressed);
    if (!inputs.isEmpty()) {
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }
#elif defined(__linux__)
    if (!m_display) return;
    int buttonId = 1;
    switch (button) {
        case MouseButton::LEFT:   buttonId = 1; break;
        case MouseButton::RIGHT:  buttonId = 3; break;
        case MouseButton::MIDDLE: buttonId = 2; break;
    }
    XTestFakeButtonEvent(m_display, buttonId, pressed ? True : False, CurrentTime);
    XFlush(m_display);
#elif defined(__APPLE__)
    CGEventType type;
    CGMouseButton mb = kCGMouseButtonLeft;
    switch (button) {
        case MouseButton::LEFT:
            type = pressed ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
            mb = kCGMouseButtonLeft;
            break;
        case MouseButton::RIGHT:
            type = pressed ? kCGEventRightMouseDown : kCGEventRightMouseUp;
            mb = kCGMouseButtonRight;
            break;
        case MouseButton::MIDDLE:
            type = pressed ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
            mb = kCGMouseButtonCenter;
            break;
    }
    CGEventRef event = CGEventCreateMouseEvent(nullptr, type, CGPointMake(0, 0), mb);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
#endif
}

void InputControl::mouseScrollEvent(int delta) {
#ifdef _WIN32
    QVector<INPUT> inputs = buildMouseScrollInputs(delta);
    if (!inputs.isEmpty()) {
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }
#elif defined(__linux__)
    if (!m_display) return;
    int button = (delta > 0) ? 4 : 5;
    int steps = qAbs(delta) / 120;
    for (int i = 0; i < steps; ++i) {
        XTestFakeButtonEvent(m_display, button, True, CurrentTime);
        XTestFakeButtonEvent(m_display, button, False, CurrentTime);
    }
    XFlush(m_display);
#elif defined(__APPLE__)
    CGEventRef event = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1,
                                                      static_cast<int32_t>(-delta / 120));
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
#endif
}

void InputControl::keyEvent(uint32_t keyCode, bool pressed, const QString& text) {
#ifdef _WIN32
    QVector<INPUT> inputs = buildKeyInputs(keyCode, pressed, text);
    if (!inputs.isEmpty()) {
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }
#elif defined(__linux__)
    if (!m_display) return;
    KeyCode xcode = XKeysymToKeycode(m_display, static_cast<KeySym>(keyCode));
    if (xcode) {
        XTestFakeKeyEvent(m_display, xcode, pressed ? True : False, CurrentTime);
        XFlush(m_display);
    }
#elif defined(__APPLE__)
    CGEventRef event = CGEventCreateKeyboardEvent(nullptr,
                                                   static_cast<CGKeyCode>(keyCode),
                                                   pressed);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
#endif
}

// Build the INPUT array that keyEvent() feeds to SendInput, without performing
// the injection. Static helper (Windows only) so the public header need not pull
// in <windows.h> (which would leak KEY_EVENT/MOUSE_EVENT macros into host.cpp).
static QVector<INPUT> buildKeyInputs(uint32_t keyCode, bool pressed,
                                     const QString& text) {
    QVector<INPUT> inputs;
#ifdef _WIN32
    // When the controller supplied the actual typed character(s), inject them
    // directly via KEYEVENTF_UNICODE. wScan then carries the UTF-16 code unit,
    // so the resulting text is independent of the host's keyboard layout / IME
    // and never gets scrambled by layout mismatch. This is the preferred path.
    QVector<uint16_t> units = InputControl::unicodeUnits(text);
    if (!text.isEmpty() && !units.isEmpty()) {
        inputs.reserve(units.size() * 2);
        DWORD downFlags = KEYEVENTF_UNICODE;
        DWORD upFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        // Down, then up (reverse order so surrogate pairs end correctly:
        // down(H), down(L), up(L), up(H)).
        for (uint16_t u : units) {
            INPUT in{};
            in.type = INPUT_KEYBOARD;
            in.ki.wScan = u;
            in.ki.dwFlags = downFlags;
            in.ki.time = 0;
            inputs.append(in);
        }
        for (int i = units.size() - 1; i >= 0; --i) {
            INPUT in{};
            in.type = INPUT_KEYBOARD;
            in.ki.wScan = units.at(i);
            in.ki.dwFlags = upFlags;
            in.ki.time = 0;
            inputs.append(in);
        }
        return inputs;
    }

    // VK-based injection for non-character keys (modifiers, F-keys, arrows...).
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(keyCode);
    if (InputControl::isExtendedKey(keyCode)) input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    input.ki.dwFlags |= (pressed ? 0 : KEYEVENTF_KEYUP);
    input.ki.time = 0;
    inputs.append(input);
#else
    Q_UNUSED(keyCode);
    Q_UNUSED(pressed);
    Q_UNUSED(text);
#endif
    return inputs;
}

// Mirror of buildKeyInputs for mouse: assemble the INPUT[] that the mouse
// methods feed to SendInput, without performing the injection (so the logic is
// unit-testable). Uses the public static helpers mouseButtonFlags / etc.
static QVector<INPUT> buildMouseMoveInputs(int x, int y) {
    QVector<INPUT> inputs;
#ifdef _WIN32
    INPUT input{};
    input.type = INPUT_MOUSE;
    QPoint p = InputControl::mouseNormalizedPos(x, y);
    input.mi.dx = p.x();
    input.mi.dy = p.y();
    input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
    input.mi.time = 0;
    input.mi.dwExtraInfo = 0;
    inputs.append(input);
#else
    Q_UNUSED(x);
    Q_UNUSED(y);
#endif
    return inputs;
}

static QVector<INPUT> buildMouseButtonInputs(MouseButton button, bool pressed) {
    QVector<INPUT> inputs;
#ifdef _WIN32
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = InputControl::mouseButtonFlags(button, pressed);
    input.mi.time = 0;
    inputs.append(input);
#else
    Q_UNUSED(button);
    Q_UNUSED(pressed);
#endif
    return inputs;
}

static QVector<INPUT> buildMouseScrollInputs(int delta) {
    QVector<INPUT> inputs;
#ifdef _WIN32
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = InputControl::mouseScrollFlags();
    input.mi.mouseData = static_cast<DWORD>(InputControl::mouseScrollData(delta));
    input.mi.time = 0;
    inputs.append(input);
#else
    Q_UNUSED(delta);
#endif
    return inputs;
}

} // namespace xrk
