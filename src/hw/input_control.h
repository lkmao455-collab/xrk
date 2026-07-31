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
    
    void processMouseEvent(const MouseEvent& event);
    void processKeyEvent(const KeyEvent& event);

signals:
    void inputError(const QString& errorString);

private:
    void mouseMoveEvent(int x, int y);
    void mouseButtonEvent(MouseButton button, bool pressed);
    void mouseScrollEvent(int delta);
    void keyEvent(uint32_t keyCode, bool pressed);

    bool m_initialized = false;
#ifdef __linux__
    Display* m_display = nullptr;
    bool initDisplay();
#elif defined(__APPLE__)
    // macOS uses CoreGraphics directly
#endif
};

} // namespace xrk
