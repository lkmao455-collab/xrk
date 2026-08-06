#pragma once

#include <QObject>
#include <QImage>
#include <QString>
#include <QList>
#include <QThread>
#include <QMutex>
#include <QVideoFrame>

#include "camera_control.h"

namespace xrk {

class CameraCapture : public QObject {
    Q_OBJECT
public:
    explicit CameraCapture(QObject* parent = nullptr);
    ~CameraCapture();

    bool initialize(int cameraIndex = 0);
    void shutdown();
    bool isInitialized() const;

    QImage captureFrame();
    int cameraCount() const;
    QString cameraName(int index) const;
    bool setCameraIndex(int index);
    int cameraIndex() const;

signals:
    void frameCaptured(const QImage& frame);
    void cameraError(const QString& errorString);

private slots:
    // Receives frames from CameraControl (cross-thread, queued). Runs on this
    // object's thread (the Host thread).
    void onFrameCaptured(const QVideoFrame& frame);

private:
    int m_cameraIndex = 0;
    QImage m_currentFrame;
    QMutex m_frameMutex;

    CameraControl* m_control = nullptr;  // lives on m_thread
    QThread* m_thread = nullptr;         // owns the live event loop for camera ops
};

} // namespace xrk
