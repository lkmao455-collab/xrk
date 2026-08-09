#include "voice_playback_widget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyle>
#include <QBuffer>
#include <QPainter>
#include <QLinearGradient>

namespace xrk {

VoicePlaybackWidget::VoicePlaybackWidget(const QByteArray& voiceData, int durationSeconds, QWidget* parent)
    : QWidget(parent)
    , m_durationMs(durationSeconds * 1000)
{
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(1.0);

    setVoiceData(voiceData, durationSeconds);
    setupUI();

    connect(m_player, &QMediaPlayer::positionChanged, this, &VoicePlaybackWidget::onPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &VoicePlaybackWidget::onDurationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &VoicePlaybackWidget::onMediaStateChanged);
}

VoicePlaybackWidget::~VoicePlaybackWidget() {
    if (m_player) {
        m_player->stop();
    }
}

void VoicePlaybackWidget::setVoiceData(const QByteArray& data, int durationSeconds) {
    m_voiceData = data;
    m_durationMs = durationSeconds * 1000;
    if (m_player && !m_voiceData.isEmpty()) {
        auto* buffer = new QBuffer(&m_voiceData, m_player);
        buffer->open(QIODevice::ReadOnly);
        m_player->setSourceDevice(buffer);
    }
    // Compute waveform from PCM data
    computeWaveform(m_voiceData);
    if (m_timeLabel) {
        m_timeLabel->setText(formatTime(0) + " / " + formatTime(m_durationMs));
    }
    if (m_progressSlider) {
        m_progressSlider->setRange(0, m_durationMs);
        m_progressSlider->setValue(0);
    }
    update();
}

bool VoicePlaybackWidget::isPlaying() const {
    return m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
}

void VoicePlaybackWidget::play() {
    if (m_player) {
        m_player->play();
    }
}

void VoicePlaybackWidget::pause() {
    if (m_player) {
        m_player->pause();
    }
}

void VoicePlaybackWidget::stop() {
    if (m_player) {
        m_player->stop();
    }
}

void VoicePlaybackWidget::setupUI() {
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(4, 2, 4, 2);
    mainLayout->setSpacing(6);

    m_playPauseBtn = new QPushButton(this);
    m_playPauseBtn->setFixedSize(28, 28);
    m_playPauseBtn->setToolTip(tr("播放/暂停"));
    connect(m_playPauseBtn, &QPushButton::clicked, this, &VoicePlaybackWidget::onPlayPauseClicked);

    m_progressSlider = new QSlider(Qt::Horizontal, this);
    m_progressSlider->setRange(0, m_durationMs);
    m_progressSlider->setValue(0);
    m_progressSlider->setMinimumWidth(120);
    connect(m_progressSlider, &QSlider::sliderPressed, this, &VoicePlaybackWidget::onSliderPressed);
    connect(m_progressSlider, &QSlider::sliderReleased, this, &VoicePlaybackWidget::onSliderReleased);

    m_timeLabel = new QLabel(formatTime(0) + " / " + formatTime(m_durationMs), this);
    m_timeLabel->setStyleSheet("color: #999; font-size: 11px;");

    m_speedCombo = new QComboBox(this);
    m_speedCombo->addItems({"1x", "1.25x", "1.5x", "2x"});
    m_speedCombo->setFixedSize(55, 24);
    connect(m_speedCombo, &QComboBox::currentIndexChanged, this, &VoicePlaybackWidget::onSpeedChanged);

    mainLayout->addWidget(m_playPauseBtn);
    mainLayout->addWidget(m_progressSlider, 1);
    mainLayout->addWidget(m_timeLabel);
    mainLayout->addWidget(m_speedCombo);

    setFixedHeight(36);
    setStyleSheet(R"(
        VoicePlaybackWidget { background: transparent; }
        QPushButton { background: #4a9eff; border: none; border-radius: 14px; color: white; font-size: 12px; }
        QPushButton:hover { background: #3a8eef; }
        QSlider::groove:horizontal { height: 4px; background: #555; border-radius: 2px; }
        QSlider::handle:horizontal { width: 12px; height: 12px; margin: -4px 0; background: #4a9eff; border-radius: 6px; }
        QComboBox { background: #3a3a3a; color: #ddd; border: 1px solid #555; border-radius: 3px; font-size: 11px; }
    )");
}

void VoicePlaybackWidget::onPlayPauseClicked() {
    if (isPlaying()) {
        pause();
    } else {
        play();
    }
}

void VoicePlaybackWidget::onSpeedChanged(int index) {
    if (!m_player) return;
    static const float speeds[] = {1.0f, 1.25f, 1.5f, 2.0f};
    if (index >= 0 && index < 4) {
        m_player->setPlaybackRate(speeds[index]);
    }
}

void VoicePlaybackWidget::onPositionChanged(qint64 position) {
    if (!m_sliderDragging) {
        m_progressSlider->setValue(static_cast<int>(position));
    }
    m_timeLabel->setText(formatTime(position) + " / " + formatTime(m_durationMs));
}

void VoicePlaybackWidget::onDurationChanged(qint64 duration) {
    m_progressSlider->setRange(0, static_cast<int>(duration));
}

void VoicePlaybackWidget::onMediaStateChanged(QMediaPlayer::PlaybackState state) {
    if (state == QMediaPlayer::PlayingState) {
        m_playPauseBtn->setText(QString::fromUtf8("\u23F8"));
        emit playbackStarted();
    } else {
        m_playPauseBtn->setText(QString::fromUtf8("\u25B6"));
        if (state == QMediaPlayer::StoppedState) {
            m_progressSlider->setValue(0);
            emit playbackStopped();
        }
    }
}

void VoicePlaybackWidget::onSliderPressed() {
    m_sliderDragging = true;
}

void VoicePlaybackWidget::onSliderReleased() {
    m_sliderDragging = false;
    m_player->setPosition(m_progressSlider->value());
}

QString VoicePlaybackWidget::formatTime(qint64 ms) const {
    int totalSecs = static_cast<int>(ms / 1000);
    int mins = totalSecs / 60;
    int secs = totalSecs % 60;
    return QString("%1:%2").arg(mins).arg(secs, 2, 10, QChar('0'));
}

void VoicePlaybackWidget::computeWaveform(const QByteArray& pcmData) {
    m_waveformSamples.clear();
    if (pcmData.isEmpty()) return;

    // Assume 16-bit PCM, mono, 16kHz (common for voice)
    const int bytesPerSample = 2;
    const int sampleRate = 16000;
    const int numSamples = pcmData.size() / bytesPerSample;
    const int samplesPerPixel = qMax(1, numSamples / 200); // ~200 bars

    for (int i = 0; i < numSamples; i += samplesPerPixel) {
        float maxAmplitude = 0;
        for (int j = 0; j < samplesPerPixel && (i + j) < numSamples; ++j) {
            int16_t sample = static_cast<int16_t>(static_cast<uint16_t>(pcmData[i * bytesPerSample + j * bytesPerSample]) |
                                                  (static_cast<uint16_t>(pcmData[i * bytesPerSample + j * bytesPerSample + 1]) << 8));
            float amplitude = qAbs(sample) / 32768.0f;
            if (amplitude > maxAmplitude) maxAmplitude = amplitude;
        }
        m_waveformSamples.append(maxAmplitude);
    }
}

void VoicePlaybackWidget::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    if (m_waveformSamples.isEmpty()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw waveform in the widget area (between play button and time label)
    int waveX = 40;
    int waveWidth = width() - 160;
    int waveHeight = 24;
    int waveY = (height() - waveHeight) / 2;

    if (waveWidth <= 0) return;

    // Background
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(40, 40, 40));
    painter.drawRoundedRect(waveX - 2, waveY - 2, waveWidth + 4, waveHeight + 4, 4, 4);

    // Draw bars
    float barWidth = static_cast<float>(waveWidth) / m_waveformSamples.size();
    if (barWidth < 1) barWidth = 1;

    for (int i = 0; i < m_waveformSamples.size(); ++i) {
        float x = waveX + i * barWidth;
        float amplitude = m_waveformSamples[i];
        int barH = static_cast<int>(amplitude * waveHeight);
        if (barH < 1) barH = 1;

        int barY = waveY + (waveHeight - barH) / 2;

        // Color: played portion in blue, unplayed in gray
        if (m_durationMs > 0) {
            float progress = static_cast<float>(m_progressSlider->value()) / m_durationMs;
            float barProgress = static_cast<float>(i) / m_waveformSamples.size();
            if (barProgress <= progress) {
                painter.setBrush(QColor(74, 158, 255));
            } else {
                painter.setBrush(QColor(100, 100, 100));
            }
        } else {
            painter.setBrush(QColor(100, 100, 100));
        }

        painter.drawRoundedRect(static_cast<int>(x), barY, qMax(1, static_cast<int>(barWidth) - 1), barH, 1, 1);
    }
}

} // namespace xrk
