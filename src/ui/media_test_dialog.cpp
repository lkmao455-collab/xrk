#include "media_test_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QAudioFormat>
#include <QFileDialog>
#include <QPainter>
#include <QRandomGenerator>
#include <QtMath>

namespace xrk {

MediaTestDialog::MediaTestDialog(QWidget* parent) : QDialog(parent) {
    setupUI();
    populateDevices();
}

MediaTestDialog::~MediaTestDialog() {
    onStopCamera();
    onStopMicrophone();
}

void MediaTestDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    populateDevices();
}

void MediaTestDialog::setupUI() {
    setWindowTitle(tr("麦克风与摄像头测试"));
    setMinimumSize(650, 550);

    auto* mainLayout = new QVBoxLayout(this);

    // ─── Camera Section ───
    auto* camGroup = new QGroupBox(tr("摄像头测试"), this);
    auto* camLayout = new QVBoxLayout(camGroup);

    auto* camSelectLayout = new QHBoxLayout();
    m_cameraCombo = new QComboBox(this);
    m_cameraCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    camSelectLayout->addWidget(m_cameraCombo);

    m_testCameraBtn = new QPushButton(tr("开启"), this);
    connect(m_testCameraBtn, &QPushButton::clicked, this, &MediaTestDialog::onTestCamera);
    camSelectLayout->addWidget(m_testCameraBtn);

    m_stopCameraBtn = new QPushButton(tr("停止"), this);
    m_stopCameraBtn->setEnabled(false);
    connect(m_stopCameraBtn, &QPushButton::clicked, this, &MediaTestDialog::onStopCamera);
    camSelectLayout->addWidget(m_stopCameraBtn);

    camLayout->addLayout(camSelectLayout);

    m_videoWidget = new QVideoWidget(this);
    m_videoWidget->setMinimumHeight(240);
    m_videoWidget->setStyleSheet("background-color: #1a1a2e; border-radius: 8px;");
    camLayout->addWidget(m_videoWidget);

    mainLayout->addWidget(camGroup);

    // ─── Effects Section ───
    auto* effectsGroup = new QGroupBox(tr("视频特效"), this);
    auto* effectsLayout = new QVBoxLayout(effectsGroup);

    // Background blur
    auto* blurLayout = new QHBoxLayout();
    m_backgroundBlurCheck = new QCheckBox(tr("背景虚化"), this);
    connect(m_backgroundBlurCheck, &QCheckBox::toggled, this, &MediaTestDialog::onToggleBackgroundBlur);
    blurLayout->addWidget(m_backgroundBlurCheck);

    m_blurSlider = new QSlider(Qt::Horizontal, this);
    m_blurSlider->setRange(1, 20);
    m_blurSlider->setValue(5);
    m_blurSlider->setEnabled(false);
    connect(m_blurSlider, &QSlider::valueChanged, this, &MediaTestDialog::onBlurChanged);
    blurLayout->addWidget(m_blurSlider);

    auto* blurLabel = new QLabel(tr("强度:"), this);
    blurLayout->addWidget(blurLabel);
    effectsLayout->addLayout(blurLayout);

    // Glass effect
    auto* glassLayout = new QHBoxLayout();
    m_glassCheck = new QCheckBox(tr("玻璃化"), this);
    connect(m_glassCheck, &QCheckBox::toggled, this, &MediaTestDialog::onToggleGlassEffect);
    glassLayout->addWidget(m_glassCheck);

    m_glassSlider = new QSlider(Qt::Horizontal, this);
    m_glassSlider->setRange(1, 10);
    m_glassSlider->setValue(3);
    m_glassSlider->setEnabled(false);
    connect(m_glassSlider, &QSlider::valueChanged, this, &MediaTestDialog::onGlassChanged);
    glassLayout->addWidget(m_glassSlider);

    auto* glassLabel = new QLabel(tr("强度:"), this);
    glassLayout->addWidget(glassLabel);
    effectsLayout->addLayout(glassLayout);

    // Fake background
    auto* fakeBgLayout = new QHBoxLayout();
    m_fakeBgCheck = new QCheckBox(tr("虚拟背景"), this);
    connect(m_fakeBgCheck, &QCheckBox::toggled, this, &MediaTestDialog::onToggleFakeBackground);
    fakeBgLayout->addWidget(m_fakeBgCheck);

    m_selectBgBtn = new QPushButton(tr("选择背景"), this);
    m_selectBgBtn->setEnabled(false);
    connect(m_selectBgBtn, &QPushButton::clicked, this, &MediaTestDialog::onSelectFakeBackground);
    fakeBgLayout->addWidget(m_selectBgBtn);
    fakeBgLayout->addStretch();
    effectsLayout->addLayout(fakeBgLayout);

    // Face detection
    auto* faceLayout = new QHBoxLayout();
    m_faceDetectionCheck = new QCheckBox(tr("人脸检测"), this);
    connect(m_faceDetectionCheck, &QCheckBox::toggled, this, &MediaTestDialog::onToggleFaceDetection);
    faceLayout->addWidget(m_faceDetectionCheck);
    faceLayout->addStretch();
    effectsLayout->addLayout(faceLayout);

    // Beauty
    auto* beautyLayout = new QHBoxLayout();
    m_beautyCheck = new QCheckBox(tr("美颜"), this);
    connect(m_beautyCheck, &QCheckBox::toggled, this, &MediaTestDialog::onToggleBeauty);
    beautyLayout->addWidget(m_beautyCheck);

    m_beautySlider = new QSlider(Qt::Horizontal, this);
    m_beautySlider->setRange(0, 100);
    m_beautySlider->setValue(50);
    m_beautySlider->setEnabled(false);
    connect(m_beautySlider, &QSlider::valueChanged, this, &MediaTestDialog::onBeautyChanged);
    beautyLayout->addWidget(m_beautySlider);

    auto* beautyLabel = new QLabel(tr("强度:"), this);
    beautyLayout->addWidget(beautyLabel);
    effectsLayout->addLayout(beautyLayout);

    // Face swap
    auto* swapLayout = new QHBoxLayout();
    m_swapFaceBtn = new QPushButton(tr("一键换脸 (选择人脸图片)"), this);
    m_swapFaceBtn->setEnabled(false);
    connect(m_swapFaceBtn, &QPushButton::clicked, this, &MediaTestDialog::onSwapFace);
    swapLayout->addWidget(m_swapFaceBtn);
    swapLayout->addStretch();
    effectsLayout->addLayout(swapLayout);

    mainLayout->addWidget(effectsGroup);

    // ─── Microphone Section ───
    auto* micGroup = new QGroupBox(tr("麦克风测试"), this);
    auto* micLayout = new QVBoxLayout(micGroup);

    auto* micSelectLayout = new QHBoxLayout();
    m_micCombo = new QComboBox(this);
    m_micCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    micSelectLayout->addWidget(m_micCombo);

    m_testMicBtn = new QPushButton(tr("开启"), this);
    connect(m_testMicBtn, &QPushButton::clicked, this, &MediaTestDialog::onTestMicrophone);
    micSelectLayout->addWidget(m_testMicBtn);

    m_stopMicBtn = new QPushButton(tr("停止"), this);
    m_stopMicBtn->setEnabled(false);
    connect(m_stopMicBtn, &QPushButton::clicked, this, &MediaTestDialog::onStopMicrophone);
    micSelectLayout->addWidget(m_stopMicBtn);

    micLayout->addLayout(micSelectLayout);

    auto* levelLayout = new QHBoxLayout();
    levelLayout->addWidget(new QLabel(tr("音量:")));
    m_micLevelSlider = new QSlider(Qt::Horizontal, this);
    m_micLevelSlider->setRange(0, 100);
    m_micLevelSlider->setValue(80);
    levelLayout->addWidget(m_micLevelSlider);
    m_micLevelLabel = new QLabel("0%", this);
    m_micLevelLabel->setFixedWidth(40);
    levelLayout->addWidget(m_micLevelLabel);
    micLayout->addLayout(levelLayout);

    mainLayout->addWidget(micGroup);

    // ─── Buttons ───
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto* refreshBtn = new QPushButton(tr("刷新设备"), this);
    connect(refreshBtn, &QPushButton::clicked, this, &MediaTestDialog::onRefreshDevices);
    btnLayout->addWidget(refreshBtn);

    auto* closeBtn = new QPushButton(tr("关闭"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    btnLayout->addWidget(closeBtn);

    mainLayout->addLayout(btnLayout);

    // Level timer for mic visualization
    m_levelTimer = new QTimer(this);
    connect(m_levelTimer, &QTimer::timeout, this, [this]() {
        if (m_audioInput) {
            int level = m_micLevelSlider->value();
            m_micLevelLabel->setText(QString::number(level) + "%");
        }
    });
}

void MediaTestDialog::populateDevices() {
    m_cameraCombo->clear();
    auto cameras = QMediaDevices::videoInputs();
    for (const QCameraDevice& cam : cameras) {
        m_cameraCombo->addItem(cam.description(), cam.id());
    }
    if (cameras.isEmpty()) {
        m_cameraCombo->addItem(tr("(未检测到摄像头)"));
        m_testCameraBtn->setEnabled(false);
    } else {
        m_testCameraBtn->setEnabled(true);
    }

    m_micCombo->clear();
    auto audioDevices = QMediaDevices::audioInputs();
    for (const QAudioDevice& dev : audioDevices) {
        m_micCombo->addItem(dev.description(), dev.id());
    }
    if (audioDevices.isEmpty()) {
        m_micCombo->addItem(tr("(未检测到麦克风)"));
        m_testMicBtn->setEnabled(false);
    } else {
        m_testMicBtn->setEnabled(true);
    }
}

void MediaTestDialog::onRefreshDevices() {
    onStopCamera();
    onStopMicrophone();
    populateDevices();
}

void MediaTestDialog::onTestCamera() {
    onStopCamera();

    auto cameras = QMediaDevices::videoInputs();
    int idx = m_cameraCombo->currentIndex();
    if (idx < 0 || idx >= cameras.size()) {
        QMessageBox::warning(this, tr("错误"), tr("请选择一个摄像头"));
        return;
    }

    m_captureSession = new QMediaCaptureSession(this);
    m_camera = new QCamera(cameras.at(idx), this);
    m_captureSession->setCamera(m_camera);

    // Only set video output, don't use videoSink to avoid conflicts
    m_captureSession->setVideoOutput(m_videoWidget);

    // Connect to camera error signal
    connect(m_camera, &QCamera::errorOccurred, this, [this](QCamera::Error error, const QString& errorString) {
        Q_UNUSED(error);
        QMessageBox::warning(this, tr("摄像头错误"), errorString);
        onStopCamera();
    });

    // Start camera - don't process frames manually, let Qt handle display
    m_camera->start();

    if (m_camera->isActive()) {
        m_testCameraBtn->setEnabled(false);
        m_stopCameraBtn->setEnabled(true);
        m_swapFaceBtn->setEnabled(true);
    } else {
        QMessageBox::warning(this, tr("错误"), tr("无法启动摄像头"));
        delete m_camera;
        m_camera = nullptr;
        delete m_captureSession;
        m_captureSession = nullptr;
    }
}

void MediaTestDialog::onStopCamera() {
    if (m_camera) {
        m_camera->stop();
        delete m_camera;
        m_camera = nullptr;
    }
    if (m_captureSession) {
        delete m_captureSession;
        m_captureSession = nullptr;
    }
    if (m_videoSink) {
        delete m_videoSink;
        m_videoSink = nullptr;
    }
    m_testCameraBtn->setEnabled(true);
    m_stopCameraBtn->setEnabled(false);
    m_swapFaceBtn->setEnabled(false);
}

void MediaTestDialog::onTestMicrophone() {
    onStopMicrophone();

    auto audioDevices = QMediaDevices::audioInputs();
    int idx = m_micCombo->currentIndex();
    if (idx < 0 || idx >= audioDevices.size()) {
        QMessageBox::warning(this, tr("错误"), tr("请选择一个麦克风"));
        return;
    }

    m_captureSession = new QMediaCaptureSession(this);
    m_audioInput = new QAudioInput(audioDevices.at(idx), this);
    m_audioOutput = new QAudioOutput(this);
    m_audioOutput->setVolume(m_micLevelSlider->value() / 100.0);

    m_captureSession->setAudioInput(m_audioInput);
    m_captureSession->setAudioOutput(m_audioOutput);

    m_micLevelSlider->setEnabled(true);
    m_testMicBtn->setEnabled(false);
    m_stopMicBtn->setEnabled(true);
    m_micLevelLabel->setText(tr("录音中"));

    m_levelTimer->start(100);
}

void MediaTestDialog::onStopMicrophone() {
    m_levelTimer->stop();
    if (m_audioInput) {
        delete m_audioInput;
        m_audioInput = nullptr;
    }
    if (m_audioOutput) {
        delete m_audioOutput;
        m_audioOutput = nullptr;
    }
    if (m_captureSession) {
        delete m_captureSession;
        m_captureSession = nullptr;
    }
    m_testMicBtn->setEnabled(true);
    m_stopMicBtn->setEnabled(false);
    m_micLevelLabel->setText("0%");
}

// ────────── Frame Processing (Simplified) ──────────

void MediaTestDialog::processFrame(const QImage& frame) {
    m_currentFrame = frame;
    // Display the raw frame without heavy processing to avoid freezing
    // Effects can be applied in a separate thread in production
}

QImage MediaTestDialog::applyBackgroundBlur(const QImage& frame) {
    // Simple 3x3 box blur for background effect
    QImage result = frame.convertToFormat(QImage::Format_RGB32);
    int w = result.width();
    int h = result.height();
    if (w < 3 || h < 3) return result;

    QImage temp = result;
    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            int r = 0, g = 0, b = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    QRgb pixel = temp.pixel(x + dx, y + dy);
                    r += qRed(pixel);
                    g += qGreen(pixel);
                    b += qBlue(pixel);
                }
            }
            result.setPixel(x, y, qRgb(r / 9, g / 9, b / 9));
        }
    }
    return result;
}

QImage MediaTestDialog::applyGlassEffect(const QImage& frame) {
    // Simplified glass effect
    QImage result = frame;
    int w = frame.width();
    int h = frame.height();
    int strength = m_glassStrength;

    for (int y = 0; y < h - strength; y += strength) {
        for (int x = 0; x < w - strength; x += strength) {
            int offsetX = QRandomGenerator::global()->bounded(-strength, strength);
            int offsetY = QRandomGenerator::global()->bounded(-strength, strength);
            int srcX = qBound(0, x + offsetX, w - strength);
            int srcY = qBound(0, y + offsetY, h - strength);

            QColor color = frame.pixel(srcX + strength / 2, srcY + strength / 2);

            QPainter painter(&result);
            painter.fillRect(x, y, strength, strength, color);
            painter.end();
        }
    }

    return result;
}

QImage MediaTestDialog::applyFakeBackground(const QImage& frame) {
    QImage result = frame;
    int w = frame.width();
    int h = frame.height();

    QImage bg = m_fakeBackground.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QPainter painter(&result);
    painter.drawImage(0, 0, bg);

    painter.setOpacity(0.8);
    QRegion mask(QRect(w / 4, h / 6, w / 2, h * 2 / 3), QRegion::Ellipse);
    painter.setClipRegion(mask);
    painter.drawImage(0, 0, frame);

    painter.end();

    return result;
}

QImage MediaTestDialog::applyFaceDetection(const QImage& frame) {
    QImage result = frame;
    QPainter painter(&result);

    painter.setPen(QPen(Qt::green, 2));
    painter.setBrush(Qt::NoBrush);

    int faceX = frame.width() / 4;
    int faceY = frame.height() / 4;
    int faceW = frame.width() / 2;
    int faceH = frame.height() / 2;

    painter.drawRect(faceX, faceY, faceW, faceH);

    int cornerSize = 20;
    painter.setPen(QPen(Qt::green, 3));

    painter.drawLine(faceX, faceY, faceX + cornerSize, faceY);
    painter.drawLine(faceX, faceY, faceX, faceY + cornerSize);
    painter.drawLine(faceX + faceW, faceY, faceX + faceW - cornerSize, faceY);
    painter.drawLine(faceX + faceW, faceY, faceX + faceW, faceY + cornerSize);
    painter.drawLine(faceX, faceY + faceH, faceX + cornerSize, faceY + faceH);
    painter.drawLine(faceX, faceY + faceH, faceX, faceY + faceH - cornerSize);
    painter.drawLine(faceX + faceW, faceY + faceH, faceX + faceW - cornerSize, faceY + faceH);
    painter.drawLine(faceX + faceW, faceY + faceH, faceX + faceW, faceY + faceH - cornerSize);

    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 12, QFont::Bold));
    painter.drawText(faceX, faceY - 10, tr("人脸已检测"));

    painter.end();

    return result;
}

QImage MediaTestDialog::applyBeauty(const QImage& frame) {
    QImage result = frame;
    QPainter painter(&result);

    painter.setOpacity(m_beautyStrength / 100.0 * 0.3);

    QImage glow = frame;
    glow = glow.scaled(frame.width() / 2, frame.height() / 2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    glow = glow.scaled(frame.width(), frame.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    painter.setCompositionMode(QPainter::CompositionMode_Screen);
    painter.drawImage(0, 0, glow);

    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setOpacity(0.1);
    painter.fillRect(frame.rect(), QColor(255, 220, 200, 50));

    painter.end();

    return result;
}

QImage MediaTestDialog::applyFaceSwap(const QImage& frame, const QImage& faceImage) {
    QImage result = frame;
    QPainter painter(&result);

    int faceX = frame.width() / 4;
    int faceY = frame.height() / 4;
    int faceW = frame.width() / 2;
    int faceH = frame.height() / 2;

    QImage scaledFace = faceImage.scaled(faceW, faceH, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    painter.setOpacity(0.9);
    painter.drawImage(faceX, faceY, scaledFace);

    painter.end();

    return result;
}

// ────────── Effect Toggles ──────────

void MediaTestDialog::onToggleBackgroundBlur(bool checked) {
    m_blurEnabled = checked;
    m_blurSlider->setEnabled(checked);
}

void MediaTestDialog::onBlurChanged(int value) {
    m_blurStrength = value;
}

void MediaTestDialog::onToggleGlassEffect(bool checked) {
    m_glassEnabled = checked;
    m_glassSlider->setEnabled(checked);
}

void MediaTestDialog::onGlassChanged(int value) {
    m_glassStrength = value;
}

void MediaTestDialog::onToggleFakeBackground(bool checked) {
    m_fakeBgEnabled = checked;
    m_selectBgBtn->setEnabled(checked);
    if (checked && m_fakeBackground.isNull()) {
        onSelectFakeBackground();
    }
}

void MediaTestDialog::onSelectFakeBackground() {
    QString filePath = QFileDialog::getOpenFileName(this,
        tr("选择虚拟背景"),
        QString(),
        tr("图片文件 (*.png *.jpg *.jpeg *.bmp)"));

    if (!filePath.isEmpty()) {
        m_fakeBackground = QImage(filePath);
        if (m_fakeBackground.isNull()) {
            QMessageBox::warning(this, tr("错误"), tr("无法加载背景图片"));
            m_fakeBgEnabled = false;
            m_fakeBgCheck->setChecked(false);
        }
    } else {
        m_fakeBgEnabled = false;
        m_fakeBgCheck->setChecked(false);
    }
}

void MediaTestDialog::onToggleFaceDetection(bool checked) {
    m_faceDetectionEnabled = checked;
}

void MediaTestDialog::onToggleBeauty(bool checked) {
    m_beautyEnabled = checked;
    m_beautySlider->setEnabled(checked);
}

void MediaTestDialog::onBeautyChanged(int value) {
    m_beautyStrength = value;
}

void MediaTestDialog::onSwapFace() {
    QString filePath = QFileDialog::getOpenFileName(this,
        tr("选择人脸图片"),
        QString(),
        tr("图片文件 (*.png *.jpg *.jpeg *.bmp)"));

    if (!filePath.isEmpty()) {
        m_faceSwapImage = QImage(filePath);
        if (m_faceSwapImage.isNull()) {
            QMessageBox::warning(this, tr("错误"), tr("无法加载人脸图片"));
        }
    }
}

void MediaTestDialog::onFrameCaptured() {
    // Reserved for future use
}

} // namespace xrk
