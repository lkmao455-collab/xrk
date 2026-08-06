#pragma once

#include <QObject>
#include <QVideoFrame>

namespace xrk {

class CameraCapture;

class CameraControl : public QObject {
    Q_OBJECT
public:
    explicit CameraControl(CameraCapture* owner, QObject* parent = nullptr);

    bool isInitialized() const;

signals:
    void frameReady(const QVideoFrame& frame);

public slots:
    void start(int cameraIndex);
    void stop();

private slots:
    void onSinkFrame();

private:
    CameraCapture* m_owner;
    bool m_initialized = false;
    int m_cameraIndex = 0;
    void* m_camera = nullptr;       // QCamera*
    void* m_sink = nullptr;         // QVideoSink*
    void* m_session = nullptr;      // QMediaCaptureSession*
};

} // namespace xrk