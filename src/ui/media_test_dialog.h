#pragma once

#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QVideoWidget>
#include <QAudioInput>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QAudioOutput>
#include <QTimer>
#include <QImage>
#include <QPixmap>
#include <QCheckBox>

namespace xrk {

class MediaTestDialog : public QDialog {
    Q_OBJECT
public:
    explicit MediaTestDialog(QWidget* parent = nullptr);
    ~MediaTestDialog();

private slots:
    void onTestCamera();
    void onTestMicrophone();
    void onStopCamera();
    void onStopMicrophone();
    void onRefreshDevices();
    void onFrameCaptured();

    // Effect slots
    void onBlurChanged(int value);
    void onGlassChanged(int value);
    void onBeautyChanged(int value);
    void onToggleBackgroundBlur(bool checked);
    void onToggleGlassEffect(bool checked);
    void onToggleFakeBackground(bool checked);
    void onToggleFaceDetection(bool checked);
    void onToggleBeauty(bool checked);
    void onSwapFace();
    void onSelectFakeBackground();

private:
    void setupUI();
    void populateDevices();
    void processFrame(const QImage& frame);
    QImage applyBackgroundBlur(const QImage& frame);
    QImage applyGlassEffect(const QImage& frame);
    QImage applyFakeBackground(const QImage& frame);
    QImage applyFaceDetection(const QImage& frame);
    QImage applyBeauty(const QImage& frame);
    QImage applyFaceSwap(const QImage& frame, const QImage& faceImage);
    void showEvent(QShowEvent* event) override;

    // Camera
    QComboBox* m_cameraCombo = nullptr;
    QPushButton* m_testCameraBtn = nullptr;
    QPushButton* m_stopCameraBtn = nullptr;
    QVideoWidget* m_videoWidget = nullptr;
    QCamera* m_camera = nullptr;
    QMediaCaptureSession* m_captureSession = nullptr;
    QVideoSink* m_videoSink = nullptr;

    // Microphone
    QComboBox* m_micCombo = nullptr;
    QPushButton* m_testMicBtn = nullptr;
    QPushButton* m_stopMicBtn = nullptr;
    QLabel* m_micLevelLabel = nullptr;
    QSlider* m_micLevelSlider = nullptr;
    QAudioInput* m_audioInput = nullptr;
    QAudioOutput* m_audioOutput = nullptr;

    // Effects
    QCheckBox* m_backgroundBlurCheck = nullptr;
    QSlider* m_blurSlider = nullptr;
    QCheckBox* m_glassCheck = nullptr;
    QSlider* m_glassSlider = nullptr;
    QCheckBox* m_fakeBgCheck = nullptr;
    QPushButton* m_selectBgBtn = nullptr;
    QCheckBox* m_faceDetectionCheck = nullptr;
    QCheckBox* m_beautyCheck = nullptr;
    QSlider* m_beautySlider = nullptr;
    QPushButton* m_swapFaceBtn = nullptr;

    // State
    bool m_blurEnabled = false;
    int m_blurStrength = 5;
    bool m_glassEnabled = false;
    int m_glassStrength = 3;
    bool m_fakeBgEnabled = false;
    bool m_faceDetectionEnabled = false;
    bool m_beautyEnabled = false;
    int m_beautyStrength = 50;
    QImage m_fakeBackground;
    QImage m_faceSwapImage;
    QImage m_currentFrame;

    QTimer* m_levelTimer = nullptr;
};

} // namespace xrk
