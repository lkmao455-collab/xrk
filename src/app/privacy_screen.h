#pragma once

#include <QObject>
#include <QWidget>
#include <QLabel>

namespace xrk {

class PrivacyScreen : public QObject {
    Q_OBJECT
public:
    explicit PrivacyScreen(QObject* parent = nullptr);
    ~PrivacyScreen();

    void show();
    void hide();
    bool isVisible() const;

    // Local "lock this console now" mode: shows the overlay and blocks all
    // local physical input, then auto-unlocks after `seconds`. This avoids a
    // permanent lockout, since BlockInput prevents any local input (including
    // the unlock gesture) while active — a remote client can also unlock early
    // via the normal privacy-screen-off path. The remote privacy path uses
    // show()/hide(); this is a separate, time-bounded local lock.
    void showLocal(int seconds = 60);

private:
    void setupOverlay();
    void updateLocalCountdown();

    QWidget* m_overlay = nullptr;
    QLabel* m_label = nullptr;
    bool m_visible = false;
    bool m_localMode = false;
    int m_remaining = 0;
    QTimer* m_tickTimer = nullptr;
};

} // namespace xrk
