#pragma once

#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include <QTimer>
#include <QByteArray>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVector>

namespace xrk {

class VoicePlaybackWidget : public QWidget {
    Q_OBJECT
public:
    explicit VoicePlaybackWidget(const QByteArray& voiceData, int durationSeconds, QWidget* parent = nullptr);
    ~VoicePlaybackWidget();

    void setVoiceData(const QByteArray& data, int durationSeconds);
    bool isPlaying() const;

signals:
    void playbackStarted();
    void playbackStopped();

public slots:
    void play();
    void pause();
    void stop();

private slots:
    void onPlayPauseClicked();
    void onSpeedChanged(int index);
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onMediaStateChanged(QMediaPlayer::PlaybackState state);
    void onSliderPressed();
    void onSliderReleased();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUI();
    void computeWaveform(const QByteArray& pcmData);
    QString formatTime(qint64 ms) const;

    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audioOutput = nullptr;
    QByteArray m_voiceData;
    int m_durationMs = 0;
    bool m_sliderDragging = false;

    QPushButton* m_playPauseBtn = nullptr;
    QSlider* m_progressSlider = nullptr;
    QLabel* m_timeLabel = nullptr;
    QComboBox* m_speedCombo = nullptr;

    // Waveform
    QVector<float> m_waveformSamples;
    int m_currentSampleIndex = 0;
};

} // namespace xrk
