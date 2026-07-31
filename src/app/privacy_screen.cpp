#include "privacy_screen.h"
#include <QScreen>
#include <QGuiApplication>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#endif

namespace xrk {

PrivacyScreen::PrivacyScreen(QObject* parent) : QObject(parent) {
}

PrivacyScreen::~PrivacyScreen() {
    hide();
    if (m_overlay) {
        delete m_overlay;
        m_overlay = nullptr;
    }
}

void PrivacyScreen::show() {
    if (m_visible) return;
    
    if (!m_overlay) {
        setupOverlay();
    }
    
    m_overlay->showFullScreen();

#ifdef Q_OS_WIN
    if (HWND hwnd = reinterpret_cast<HWND>(m_overlay->winId())) {
        SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    }
#endif

    m_overlay->raise();
    m_visible = true;
}

void PrivacyScreen::hide() {
    if (!m_visible) return;
    
    if (m_overlay) {
        m_overlay->hide();
    }
    m_visible = false;
}

bool PrivacyScreen::isVisible() const {
    return m_visible;
}

void PrivacyScreen::setupOverlay() {
    m_overlay = new QWidget(nullptr,
        Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
        Qt::Tool | Qt::BypassWindowManagerHint | Qt::WindowTransparentForInput);
    
    m_overlay->setStyleSheet(
        "background-color: #000000;"
    );
    m_overlay->setCursor(Qt::BlankCursor);

    QVBoxLayout* layout = new QVBoxLayout(m_overlay);
    layout->setAlignment(Qt::AlignCenter);

    m_label = new QLabel(m_overlay);
    m_label->setText(
        "<div style='text-align: center; color: white; font-family: Segoe UI;'>"
        "<h1 style='font-size: 48px; color: #e74c3c;'>●</h1>"
        "<h2 style='font-size: 32px;'>远程控制进行中</h2>"
        "<p style='font-size: 18px; color: #888;'>屏幕已被锁定</p>"
        "<p style='font-size: 14px; color: #555;'>Remote control in progress</p>"
        "</div>"
    );
    m_label->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_label);

    QRect screenGeom;
    for (QScreen* screen : QGuiApplication::screens()) {
        screenGeom = screenGeom.united(screen->geometry());
    }
    m_overlay->setGeometry(screenGeom);
}

} // namespace xrk
