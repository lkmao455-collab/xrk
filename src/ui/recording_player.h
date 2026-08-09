#pragma once

#include <QDialog>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QTimer>
#include <QImage>
#include <QFile>
#include <QPainter>

namespace xrk {

class RecordingPlayer : public QDialog {
    Q_OBJECT
public:
    explicit RecordingPlayer(const QString& aviPath, QWidget* parent = nullptr);
    ~RecordingPlayer();

    void openFile(const QString& path);

signals:
    void closed();

private slots:
    void onPlayPauseClicked();
    void onStopClicked();
    void onFrameTimer();
    void onSliderPressed();
    void onSliderReleased();
    void onSpeedChanged();

private:
    void setupUI();
    bool parseAviIndex();
    bool readNextFrame(QImage& outImage);
    void updateTimeLabel();
    void applyDarkTheme();

    QString m_aviPath;
    QFile m_file;
    int m_width = 0;
    int m_height = 0;
    int m_fps = 30;
    bool m_playing = false;
    int m_currentFrame = 0;
    int m_totalFrames = 0;
    QList<qint64> m_frameOffsets;
    QList<uint32_t> m_frameSizes;

    QPushButton* m_playPauseBtn = nullptr;
    QPushButton* m_stopBtn = nullptr;
    QPushButton* m_speedBtn = nullptr;
    QSlider* m_slider = nullptr;
    QLabel* m_displayLabel = nullptr;
    QLabel* m_timeLabel = nullptr;
    QTimer* m_frameTimer = nullptr;
    bool m_sliderDragging = false;
    float m_speed = 1.0f;
};

} // namespace xrk
