#include "privacy_screen.h"
#include "core/logger.h"
#include <QScreen>
#include <QGuiApplication>
#include <QVBoxLayout>
#include <QTimer>

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
    m_localMode = false;

    if (!m_overlay) {
        setupOverlay();
    }

    m_overlay->showFullScreen();

#ifdef Q_OS_WIN
    if (HWND hwnd = reinterpret_cast<HWND>(m_overlay->winId())) {
        SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    }
    // Block all local physical keyboard/mouse input to the console so nobody
    // at the controlled machine can operate it while a remote session is
    // active. BlockInput is automatically released if the process exits.
    if (!BlockInput(TRUE)) {
        LOG_WARNING("PrivacyScreen: BlockInput failed");
    }
#endif

    m_overlay->raise();
    m_visible = true;
}

void PrivacyScreen::showLocal(int seconds) {
    if (m_visible) {
        // Re-arm the countdown if already locked locally.
        if (m_localMode) {
            m_remaining = seconds;
            updateLocalCountdown();
        }
        return;
    }
    m_localMode = true;
    m_remaining = qMax(1, seconds);

    if (!m_overlay) {
        setupOverlay();
    }

    m_overlay->showFullScreen();

#ifdef Q_OS_WIN
    if (HWND hwnd = reinterpret_cast<HWND>(m_overlay->winId())) {
        SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    }
    // Block all local physical input. Because BlockInput also blocks any local
    // unlock gesture, the lock is time-bounded (auto-unlock below) and can be
    // released early by a remote client via the privacy-screen-off path.
    if (!BlockInput(TRUE)) {
        LOG_WARNING("PrivacyScreen: BlockInput failed");
    }
#endif

    updateLocalCountdown();
    m_overlay->raise();
    m_visible = true;

    if (!m_tickTimer) {
        m_tickTimer = new QTimer(this);
        connect(m_tickTimer, &QTimer::timeout, this, [this]() {
            --m_remaining;
            if (m_remaining <= 0) {
                hide();
            } else {
                updateLocalCountdown();
            }
        });
    }
    m_tickTimer->start(1000);
}

void PrivacyScreen::updateLocalCountdown() {
    if (m_label) {
        m_label->setText(
            "<div style='text-align: center; color: white; font-family: Segoe UI;'>"
            "<h1 style='font-size: 48px; color: #e74c3c;'>●</h1>"
            "<h2 style='font-size: 32px;'>本机屏幕已锁定</h2>"
            "<p style='font-size: 18px; color: #888;'>本地键鼠输入已屏蔽</p>"
            "<p style='font-size: 18px; color: #aaa;'>" +
            QString::number(m_remaining) + " 秒后自动解锁</p>"
            "<p style='font-size: 14px; color: #555;'>远程端也可提前解锁</p>"
            "</div>"
        );
    }
}

void PrivacyScreen::hide() {
    if (!m_visible) return;

    if (m_tickTimer) {
        m_tickTimer->stop();
    }
    m_localMode = false;

#ifdef Q_OS_WIN
    // Restore local input before hiding the overlay.
    BlockInput(FALSE);
#endif

    if (m_overlay) {
        m_overlay->hide();
    }
    m_visible = false;
}

bool PrivacyScreen::isVisible() const {
    return m_visible;
}

void PrivacyScreen::setupOverlay() {
    // NOTE: deliberately NOT WindowTransparentForInput — the overlay must
    // consume input (combined with BlockInput below) so the local console is
    // locked, not pass input through to the desktop behind it.
    m_overlay = new QWidget(nullptr,
        Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
        Qt::Tool | Qt::BypassWindowManagerHint |
        Qt::WindowDoesNotAcceptFocus);

    m_overlay->setAttribute(Qt::WA_ShowWithoutActivating, true);
    m_overlay->setFocusPolicy(Qt::NoFocus);
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
