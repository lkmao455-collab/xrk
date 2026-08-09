#include "recording_player.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QDataStream>
#include <QStyle>
#include <QMessageBox>

namespace xrk {

RecordingPlayer::RecordingPlayer(const QString& aviPath, QWidget* parent)
    : QDialog(parent)
    , m_aviPath(aviPath)
{
    setWindowTitle(tr("会话录像回放"));
    setMinimumSize(800, 600);
    setupUI();
    if (!aviPath.isEmpty()) {
        openFile(aviPath);
    }
}

RecordingPlayer::~RecordingPlayer() {
    if (m_file.isOpen()) m_file.close();
}

void RecordingPlayer::openFile(const QString& path) {
    m_aviPath = path;
    m_playing = false;
    m_currentFrame = 0;
    m_frameOffsets.clear();
    m_frameSizes.clear();

    if (m_file.isOpen()) m_file.close();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("错误"), tr("无法打开文件: %1").arg(path));
        return;
    }

    if (!parseAviIndex()) {
        QMessageBox::warning(this, tr("错误"), tr("无效的AVI文件格式"));
        m_file.close();
        return;
    }

    m_totalFrames = m_frameOffsets.size();
    m_slider->setRange(0, m_totalFrames - 1);
    m_slider->setValue(0);
    m_frameTimer->setInterval(1000 / m_fps);
    updateTimeLabel();
}

bool RecordingPlayer::parseAviIndex() {
    // Minimal AVI parser: skip RIFF header, find movi list, collect JPEG frames
    char fourcc[5] = {};
    m_file.seek(0);

    // Read RIFF header
    if (m_file.read(fourcc, 4) != 4 || QByteArray(fourcc, 4) != "RIFF") return false;
    m_file.read(fourcc, 4); // file size
    if (m_file.read(fourcc, 4) != 4 || QByteArray(fourcc, 4) != "AVI ") return false;

    // Scan for LIST movi
    while (!m_file.atEnd()) {
        if (m_file.read(fourcc, 4) != 4) break;
        QByteArray tag(fourcc, 4);
        uint32_t chunkSize = 0;
        m_file.read(reinterpret_cast<char*>(&chunkSize), 4);

        if (tag == "LIST") {
            char listType[4];
            if (m_file.read(listType, 4) != 4) break;
            if (QByteArray(listType, 4) == "movi") {
                // Parse movi content
                qint64 moviEnd = m_file.pos() + chunkSize - 4;
                while (m_file.pos() < moviEnd) {
                    qint64 frameStart = m_file.pos();
                    if (m_file.read(fourcc, 4) != 4) break;
                    uint32_t frameSize = 0;
                    if (m_file.read(reinterpret_cast<char*>(&frameSize), 4) != 4) break;
                    QByteArray frameTag(fourcc, 4);
                    if (frameTag.endsWith("dc")) { // compressed frame (JPEG)
                        m_frameOffsets.append(m_file.pos());
                        m_frameSizes.append(frameSize);
                    }
                    m_file.seek(m_file.pos() + frameSize);
                    // Align to 2-byte boundary
                    if (m_file.pos() % 2 != 0) m_file.seek(m_file.pos() + 1);
                }
                return !m_frameOffsets.isEmpty();
            } else {
                m_file.seek(m_file.pos() + chunkSize - 4);
            }
        } else {
            m_file.seek(m_file.pos() + chunkSize);
            if (chunkSize % 2 != 0) m_file.seek(m_file.pos() + 1);
        }
    }
    return false;
}

bool RecordingPlayer::readNextFrame(QImage& outImage) {
    if (m_currentFrame >= m_frameOffsets.size()) return false;
    m_file.seek(m_frameOffsets[m_currentFrame]);
    QByteArray frameData = m_file.read(m_frameSizes[m_currentFrame]);
    return outImage.loadFromData(frameData, "JPEG");
}

void RecordingPlayer::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);

    m_displayLabel = new QLabel(this);
    m_displayLabel->setMinimumSize(640, 480);
    m_displayLabel->setAlignment(Qt::AlignCenter);
    m_displayLabel->setStyleSheet("background: #1a1a1a;");
    mainLayout->addWidget(m_displayLabel, 1);

    auto* controlLayout = new QHBoxLayout();
    m_playPauseBtn = new QPushButton(QString::fromUtf8("\u25B6"), this);
    m_playPauseBtn->setFixedSize(36, 36);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &RecordingPlayer::onPlayPauseClicked);

    m_stopBtn = new QPushButton(QString::fromUtf8("\u23F9"), this);
    m_stopBtn->setFixedSize(36, 36);
    connect(m_stopBtn, &QPushButton::clicked, this, &RecordingPlayer::onStopClicked);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 0);
    connect(m_slider, &QSlider::sliderPressed, this, &RecordingPlayer::onSliderPressed);
    connect(m_slider, &QSlider::sliderReleased, this, &RecordingPlayer::onSliderReleased);

    m_timeLabel = new QLabel("00:00 / 00:00", this);
    m_timeLabel->setStyleSheet("color: #999; font-size: 11px;");

    m_speedBtn = new QPushButton("1x", this);
    m_speedBtn->setFixedSize(40, 28);
    connect(m_speedBtn, &QPushButton::clicked, this, &RecordingPlayer::onSpeedChanged);

    controlLayout->addWidget(m_playPauseBtn);
    controlLayout->addWidget(m_stopBtn);
    controlLayout->addWidget(m_slider, 1);
    controlLayout->addWidget(m_timeLabel);
    controlLayout->addWidget(m_speedBtn);
    mainLayout->addLayout(controlLayout);

    m_frameTimer = new QTimer(this);
    connect(m_frameTimer, &QTimer::timeout, this, &RecordingPlayer::onFrameTimer);

    applyDarkTheme();
}

void RecordingPlayer::onPlayPauseClicked() {
    if (m_playing) {
        m_frameTimer->stop();
        m_playing = false;
        m_playPauseBtn->setText(QString::fromUtf8("\u25B6"));
    } else {
        m_frameTimer->start();
        m_playing = true;
        m_playPauseBtn->setText(QString::fromUtf8("\u23F8"));
    }
}

void RecordingPlayer::onStopClicked() {
    m_frameTimer->stop();
    m_playing = false;
    m_currentFrame = 0;
    m_slider->setValue(0);
    m_playPauseBtn->setText(QString::fromUtf8("\u25B6"));
    QImage img;
    if (readNextFrame(img)) {
        m_displayLabel->setPixmap(QPixmap::fromImage(img).scaled(
            m_displayLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    updateTimeLabel();
}

void RecordingPlayer::onFrameTimer() {
    m_currentFrame++;
    if (m_currentFrame >= m_totalFrames) {
        m_frameTimer->stop();
        m_playing = false;
        m_playPauseBtn->setText(QString::fromUtf8("\u25B6"));
        return;
    }
    QImage img;
    if (readNextFrame(img)) {
        m_displayLabel->setPixmap(QPixmap::fromImage(img).scaled(
            m_displayLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    if (!m_sliderDragging) {
        m_slider->setValue(m_currentFrame);
    }
    updateTimeLabel();
}

void RecordingPlayer::onSliderPressed() { m_sliderDragging = true; }
void RecordingPlayer::onSliderReleased() {
    m_sliderDragging = false;
    m_currentFrame = m_slider->value();
    QImage img;
    if (readNextFrame(img)) {
        m_displayLabel->setPixmap(QPixmap::fromImage(img).scaled(
            m_displayLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    updateTimeLabel();
}

void RecordingPlayer::onSpeedChanged() {
    static const float speeds[] = {0.5f, 1.0f, 1.5f, 2.0f};
    static const QStringList labels = {"0.5x", "1x", "1.5x", "2x"};
    static int idx = 1;
    idx = (idx + 1) % 4;
    m_speed = speeds[idx];
    m_speedBtn->setText(labels[idx]);
    m_frameTimer->setInterval(static_cast<int>(1000 / (m_fps * m_speed)));
}

void RecordingPlayer::updateTimeLabel() {
    auto fmt = [](int secs) -> QString {
        return QString("%1:%2").arg(secs / 60, 2, 10, QChar('0')).arg(secs % 60, 2, 10, QChar('0'));
    };
    int current = m_totalFrames > 0 ? static_cast<int>(m_currentFrame * 1000 / m_fps / 1000) : 0;
    int total = m_totalFrames > 0 ? static_cast<int>(m_totalFrames * 1000 / m_fps / 1000) : 0;
    m_timeLabel->setText(fmt(current) + " / " + fmt(total));
}

void RecordingPlayer::applyDarkTheme() {
    setStyleSheet(R"(
        RecordingPlayer { background: #2b2b2b; color: #ddd; }
        QPushButton { background: #3a3a3a; color: #ddd; border: 1px solid #555; border-radius: 4px; font-size: 16px; }
        QPushButton:hover { background: #4a4a4a; }
        QSlider::groove:horizontal { height: 4px; background: #555; border-radius: 2px; }
        QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; background: #4a9eff; border-radius: 7px; }
    )");
}

} // namespace xrk
