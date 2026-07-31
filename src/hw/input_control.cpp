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
        LOG_ERROR("Linux: Cannot open X display for input");
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
    
#ifdef __linux__
    if (m_display) {
        XCloseDisplay(m_display);
        m_display = nullptr;
    }
#endif

    m_initialized = false;
    LOG_INFO("InputControl shutdown");
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
                 (event.pressed ? " DOWN" : " UP"));
    }

    keyEvent(event.keyCode, event.pressed);
}

void InputControl::mouseMoveEvent(int x, int y) {
#ifdef _WIN32
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = static_cast<LONG>(x * 65535 / GetSystemMetrics(SM_CXSCREEN));
    input.mi.dy = static_cast<LONG>(y * 65535 / GetSystemMetrics(SM_CYSCREEN));
    input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
    input.mi.time = 0;
    input.mi.dwExtraInfo = 0;
    SendInput(1, &input, sizeof(INPUT));
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
    DWORD flags = 0;
    switch (button) {
        case MouseButton::LEFT:
            flags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            break;
        case MouseButton::RIGHT:
            flags = pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
            break;
        case MouseButton::MIDDLE:
            flags = pressed ? 0x0020 : 0x0040;
            break;
    }
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    input.mi.time = 0;
    SendInput(1, &input, sizeof(INPUT));
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
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(delta);
    input.mi.time = 0;
    SendInput(1, &input, sizeof(INPUT));
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

void InputControl::keyEvent(uint32_t keyCode, bool pressed) {
#ifdef _WIN32
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(keyCode);
    input.ki.dwFlags = pressed ? 0 : KEYEVENTF_KEYUP;
    input.ki.time = 0;
    SendInput(1, &input, sizeof(INPUT));
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

} // namespace xrk
