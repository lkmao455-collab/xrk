#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QElapsedTimer>
#include <QVBoxLayout>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include "core/types.h"

namespace xrk {

class CallWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal pulseScale READ pulseScale WRITE setPulseScale)
public:
    explicit CallWidget(QWidget* parent = nullptr);
    ~CallWidget();

    void showIncomingCall(const QString& callerName, const QString& callType, const QString& callId);
    void showOutgoingCall(const QString& calleeName, const QString& callType, const QString& callId);
    void showInCall(const QString& peerName, const QString& callType);
    void updateDuration(int seconds);
    void setMuted(bool muted);
    void setSpeakerOn(bool on);

    qreal pulseScale() const { return m_pulseScale; }
    void setPulseScale(qreal s) { m_pulseScale = s; m_pulseLabel->setStyleSheet(
        QString("font-size: %1px; color: #4ec9b0;").arg(80 * s)); }

signals:
    void callAccepted(const QString& callId);
    void callRejected(const QString& callId);
    void callEnded();
    void toggleMute();
    void toggleSpeaker();
    void switchToVideo();

private slots:
    void onAcceptClicked();
    void onRejectClicked();
    void onHangUpClicked();
    void onMuteClicked();
    void onSpeakerClicked();
    void onVideoSwitchClicked();
    void onDurationTimer();

private:
    void setupUI();
    void showControls(bool incoming);
    QString formatDuration(int seconds);

    enum class CallState { Idle, Incoming, Outgoing, InCall };
    CallState m_state = CallState::Idle;
    QString m_callId;
    QString m_callType;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_peerNameLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_durationLabel = nullptr;
    QLabel* m_pulseLabel = nullptr;

    QPushButton* m_acceptBtn = nullptr;
    QPushButton* m_rejectBtn = nullptr;
    QPushButton* m_hangUpBtn = nullptr;
    QPushButton* m_muteBtn = nullptr;
    QPushButton* m_speakerBtn = nullptr;
    QPushButton* m_videoBtn = nullptr;

    QTimer m_durationTimer;
    QElapsedTimer m_elapsedTimer;
    bool m_muted = false;
    bool m_speakerOn = false;
    qreal m_pulseScale = 1.0;
    QPropertyAnimation* m_pulseAnimation = nullptr;
};

} // namespace xrk
