#include "call_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QPropertyAnimation>

namespace xrk {

CallWidget::CallWidget(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setFixedSize(320, 440);
    setStyleSheet("background-color: #1e1e2e;");
    setupUI();

    m_pulseAnimation = new QPropertyAnimation(this, "pulseScale", this);
    m_pulseAnimation->setDuration(1200);
    m_pulseAnimation->setStartValue(1.0);
    m_pulseAnimation->setEndValue(1.3);
    m_pulseAnimation->setEasingCurve(QEasingCurve::InOutSine);
    m_pulseAnimation->setLoopCount(-1);

    connect(&m_durationTimer, &QTimer::timeout, this, &CallWidget::onDurationTimer);
}

CallWidget::~CallWidget() {}

void CallWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 32, 24, 24);
    mainLayout->setSpacing(16);

    m_pulseLabel = new QLabel(this);
    m_pulseLabel->setAlignment(Qt::AlignCenter);
    m_pulseLabel->setStyleSheet("font-size: 80px; color: #4ec9b0;");
    m_pulseLabel->setMinimumHeight(100);
    mainLayout->addWidget(m_pulseLabel);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet("font-size: 14px; color: #888;");
    mainLayout->addWidget(m_titleLabel);

    m_peerNameLabel = new QLabel(this);
    m_peerNameLabel->setAlignment(Qt::AlignCenter);
    m_peerNameLabel->setStyleSheet("font-size: 22px; font-weight: bold; color: #e0e0e0;");
    mainLayout->addWidget(m_peerNameLabel);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("font-size: 13px; color: #4ec9b0;");
    mainLayout->addWidget(m_statusLabel);

    m_durationLabel = new QLabel(this);
    m_durationLabel->setAlignment(Qt::AlignCenter);
    m_durationLabel->setStyleSheet("font-size: 18px; color: #d4d4d4;");
    m_durationLabel->hide();
    mainLayout->addWidget(m_durationLabel);

    mainLayout->addStretch();

    QHBoxLayout* actionLayout = new QHBoxLayout();
    actionLayout->setSpacing(16);
    actionLayout->setAlignment(Qt::AlignCenter);

    m_muteBtn = new QPushButton(tr("Mute"), this);
    m_speakerBtn = new QPushButton(tr("Speaker"), this);
    m_videoBtn = new QPushButton(tr("Video"), this);

    QString roundBtnStyle = "QPushButton { width: 56px; height: 56px; border-radius: 28px; "
                            "font-size: 11px; color: white; border: none; }";
    m_muteBtn->setStyleSheet(roundBtnStyle + "QPushButton { background: #555; }"
                              "QPushButton:hover { background: #666; }");
    m_speakerBtn->setStyleSheet(roundBtnStyle + "QPushButton { background: #555; }"
                                "QPushButton:hover { background: #666; }");
    m_videoBtn->setStyleSheet(roundBtnStyle + "QPushButton { background: #6a3d9a; }"
                              "QPushButton:hover { background: #7e57c2; }");

    m_muteBtn->hide();
    m_speakerBtn->hide();
    m_videoBtn->hide();

    actionLayout->addWidget(m_muteBtn);
    actionLayout->addWidget(m_speakerBtn);
    actionLayout->addWidget(m_videoBtn);
    mainLayout->addLayout(actionLayout);

    QHBoxLayout* callLayout = new QHBoxLayout();
    callLayout->setSpacing(24);
    callLayout->setAlignment(Qt::AlignCenter);

    m_acceptBtn = new QPushButton(tr("Accept"), this);
    m_rejectBtn = new QPushButton(tr("Reject"), this);
    m_hangUpBtn = new QPushButton(tr("Hang Up"), this);

    QString acceptStyle = "QPushButton { width: 64px; height: 64px; border-radius: 32px; "
                          "font-size: 13px; color: white; background: #388a34; border: none; }"
                          "QPushButton:hover { background: #45a741; }";
    QString rejectStyle = "QPushButton { width: 64px; height: 64px; border-radius: 32px; "
                          "font-size: 13px; color: white; background: #c53030; border: none; }"
                          "QPushButton:hover { background: #e04040; }";

    m_acceptBtn->setStyleSheet(acceptStyle);
    m_rejectBtn->setStyleSheet(rejectStyle);
    m_hangUpBtn->setStyleSheet(rejectStyle);

    callLayout->addWidget(m_acceptBtn);
    callLayout->addWidget(m_rejectBtn);
    callLayout->addWidget(m_hangUpBtn);
    m_hangUpBtn->hide();
    mainLayout->addLayout(callLayout);

    connect(m_acceptBtn, &QPushButton::clicked, this, &CallWidget::onAcceptClicked);
    connect(m_rejectBtn, &QPushButton::clicked, this, &CallWidget::onRejectClicked);
    connect(m_hangUpBtn, &QPushButton::clicked, this, &CallWidget::onHangUpClicked);
    connect(m_muteBtn, &QPushButton::clicked, this, &CallWidget::onMuteClicked);
    connect(m_speakerBtn, &QPushButton::clicked, this, &CallWidget::onSpeakerClicked);
    connect(m_videoBtn, &QPushButton::clicked, this, &CallWidget::onVideoSwitchClicked);
}

void CallWidget::showIncomingCall(const QString& callerName, const QString& callType, const QString& callId) {
    m_state = CallState::Incoming;
    m_callId = callId;
    m_callType = callType;

    m_pulseLabel->setText(callType == "video" ? "\xf0\x9f\x93\xa5" : "\xf0\x9f\x93\xbb");
    m_titleLabel->setText(tr("Incoming %1 call").arg(callType));
    m_peerNameLabel->setText(callerName);
    m_statusLabel->setText(tr("Ringing..."));

    showControls(true);
    m_pulseAnimation->start();
    show();
    raise();
    activateWindow();
}

void CallWidget::showOutgoingCall(const QString& calleeName, const QString& callType, const QString& callId) {
    m_state = CallState::Outgoing;
    m_callId = callId;
    m_callType = callType;

    m_pulseLabel->setText(callType == "video" ? "\xf0\x9f\x93\xa5" : "\xf0\x9f\x93\xbb");
    m_titleLabel->setText(tr("Calling..."));
    m_peerNameLabel->setText(calleeName);
    m_statusLabel->setText(tr("Waiting for answer"));

    showControls(false);
    m_pulseAnimation->start();
    show();
    raise();
    activateWindow();
}

void CallWidget::showInCall(const QString& peerName, const QString& callType) {
    m_state = CallState::InCall;
    m_callType = callType;

    m_pulseLabel->setText(callType == "video" ? "\xf0\x9f\x93\xa5" : "\xf0\x9f\x93\xbb");
    m_titleLabel->setText(tr("In %1 call").arg(callType));
    m_peerNameLabel->setText(peerName);
    m_statusLabel->hide();
    m_durationLabel->show();

    m_acceptBtn->hide();
    m_rejectBtn->hide();
    m_hangUpBtn->show();
    m_muteBtn->show();
    m_speakerBtn->show();
    if (callType == "video") m_videoBtn->show();

    m_pulseAnimation->stop();
    m_pulseLabel->setStyleSheet("font-size: 60px; color: #4ec9b0;");
    m_elapsedTimer.start();
    m_durationTimer.start(1000);
}

void CallWidget::updateDuration(int seconds) {
    m_durationLabel->setText(formatDuration(seconds));
}

void CallWidget::setMuted(bool muted) {
    m_muted = muted;
    m_muteBtn->setStyleSheet(QString("QPushButton { width: 56px; height: 56px; border-radius: 28px; "
        "font-size: 11px; color: white; border: none; background: %1; }"
        "QPushButton:hover { background: %2; }")
        .arg(muted ? "#e04040" : "#555")
        .arg(muted ? "#c53030" : "#666"));
}

void CallWidget::setSpeakerOn(bool on) {
    m_speakerOn = on;
    m_speakerBtn->setStyleSheet(QString("QPushButton { width: 56px; height: 56px; border-radius: 28px; "
        "font-size: 11px; color: white; border: none; background: %1; }"
        "QPushButton:hover { background: %2; }")
        .arg(on ? "#0e639c" : "#555")
        .arg(on ? "#1177bb" : "#666"));
}

void CallWidget::showControls(bool incoming) {
    m_acceptBtn->setVisible(incoming);
    m_rejectBtn->setVisible(incoming);
    m_hangUpBtn->hide();
    m_muteBtn->hide();
    m_speakerBtn->hide();
    m_videoBtn->hide();
    m_statusLabel->show();
    m_durationLabel->hide();
}

void CallWidget::onAcceptClicked() {
    m_pulseAnimation->stop();
    emit callAccepted(m_callId);
}

void CallWidget::onRejectClicked() {
    m_pulseAnimation->stop();
    emit callRejected(m_callId);
    hide();
}

void CallWidget::onHangUpClicked() {
    m_durationTimer.stop();
    emit callEnded();
    hide();
}

void CallWidget::onMuteClicked() {
    setMuted(!m_muted);
    emit toggleMute();
}

void CallWidget::onSpeakerClicked() {
    setSpeakerOn(!m_speakerOn);
    emit toggleSpeaker();
}

void CallWidget::onVideoSwitchClicked() {
    emit switchToVideo();
}

void CallWidget::onDurationTimer() {
    int secs = m_elapsedTimer.elapsed() / 1000;
    m_durationLabel->setText(formatDuration(secs));
}

QString CallWidget::formatDuration(int seconds) {
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}

} // namespace xrk
