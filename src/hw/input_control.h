#pragma once

#include <QObject>
#include <QPoint>
#include "core/types.h"

#ifdef __linux__
#include <X11/Xlib.h>
#endif

namespace xrk {

class InputControl : public QObject {
    Q_OBJECT
public:
    explicit InputControl(QObject* parent = nullptr);
    ~InputControl();

    bool initialize();
    void shutdown();
    
    void simulateMouseMove(int x, int y);
    void simulateMouseButton(MouseButton button, bool pressed);
    void simulateMouseClick(MouseButton button, int x, int y);
    void simulateMouseDoubleClick(MouseButton button, int x, int y);
    void simulateMouseScroll(int delta, int x, int y);
    
    void simulateKeyPress(uint32_t keyCode);
    void simulateKeyRelease(uint32_t keyCode);
    void simulateKeyCombo(uint32_t keyCode, uint32_t modifiers);
    // Inject the exact characters via KEYEVENTF_UNICODE (layout/IME independent).
    // This is the path used for typed text coming from the controller.
    void simulateText(const QString& text);
    
    void processMouseEvent(const MouseEvent& event);
    void processKeyEvent(const KeyEvent& event);

    // --- Injection-logic helpers (platform neutral, unit-testable) ---
    // Whether a VK needs the KEYEVENTF_EXTENDEDKEY flag on Windows.
    static bool isExtendedKey(uint32_t keyCode);
    // Decompose typed text into UTF-16 code units (grouping surrogate pairs),
    // the exact units injected via KEYEVENTF_UNICODE.
    static QVector<uint16_t> unicodeUnits(const QString& text);

    // Mouse injection helpers (platform neutral, unit-testable). They fully
    // determine the INPUT[] that the Windows mouse methods build.
    static uint32_t mouseButtonFlags(MouseButton button, bool pressed);
    static QPoint mouseNormalizedPos(int x, int y); // 0..65535 absolute coords
    static uint32_t mouseScrollFlags();
    static int mouseScrollData(int delta);

signals:
    void inputError(const QString& errorString);

private:
    void mouseMoveEvent(int x, int y);
    void mouseButtonEvent(MouseButton button, bool pressed);
    void mouseScrollEvent(int delta);
    void keyEvent(uint32_t keyCode, bool pressed, const QString& text = QString());

    bool m_initialized = false;
#ifdef __linux__
    Display* m_display = nullptr;
    bool initDisplay();
#elif defined(__APPLE__)
    // macOS uses CoreGraphics directly
#endif
};

} // namespace xrk
