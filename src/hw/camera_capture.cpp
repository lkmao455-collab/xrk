#include "camera_capture.h"
#include "core/logger.h"

#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVideoSink>
#include <QVideoFrame>

namespace xrk {

// Camera lifecycle worker. Created and moved to the dedicated camera thread so
// that QCamera::start()/stop() -- which notify state changes through the Qt
// event loop -- always execute on a thread that has a live event loop running.
// Without this, stop() blocks forever waiting for a signal that never arrives
// when the caller (Host) has no running event loop (headless tests, app
// teardown), which previously hung Host destruction.
class CameraControl : public QObject {
    Q_OBJECT
public:
    explicit CameraControl(CameraCapture* owner, QObject* parent = nullptr)
        : QObject(parent), m_owner(owner) {}

    bool isInitialized() const { return m_initialized; }

signals:
    void frameReady(const QVideoFrame& frame);

public slots:
    void start(int cameraIndex) {
        QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
        if (cameras.isEmpty()) {
            LOG_ERROR("Camera: No cameras found");
            m_initialized = false;
            return;
        }

        if (cameraIndex < 0 || cameraIndex >= cameras.size()) {
            cameraIndex = 0;
        }

        m_cameraIndex = cameraIndex;
        QCameraDevice device = cameras[cameraIndex];

        // Parent to nullptr: these live on the camera thread and are deleted in
        // stop() on the same thread.
        m_camera = new QCamera(device, nullptr);
        m_sink = new QVideoSink(nullptr);
        m_session = new QMediaCaptureSession(nullptr);

        static_cast<QMediaCaptureSession*>(m_session)->setCamera(static_cast<QCamera*>(m_camera));
        static_cast<QMediaCaptureSession*>(m_session)->setVideoSink(static_cast<QVideoSink*>(m_sink));

        connect(static_cast<QVideoSink*>(m_sink), &QVideoSink::videoFrameChanged,
                this, &CameraControl::onSinkFrame);

        static_cast<QCamera*>(m_camera)->start();
        m_initialized = true;
        LOG_INFO("Camera initialized: " + device.description());
    }

    void stop() {
        if (m_camera) {
            static_cast<QCamera*>(m_camera)->stop();
            delete static_cast<QCamera*>(m_camera);
            m_camera = nullptr;
        }
        if (m_sink) {
            delete static_cast<QVideoSink*>(m_sink);
            m_sink = nullptr;
        }
        if (m_session) {
            delete static_cast<QMediaCaptureSession*>(m_session);
            m_session = nullptr;
        }
        m_initialized = false;
        LOG_INFO("Camera shutdown");
    }

private slots:
    void onSinkFrame() {
        if (!m_sink) return;
        QVideoFrame frame = static_cast<QVideoSink*>(m_sink)->videoFrame();
        if (frame.isValid()) {
            emit frameReady(frame);
        }
    }

private:
    CameraCapture* m_owner;
    bool m_initialized = false;
    int m_cameraIndex = 0;
    void* m_camera = nullptr;       // QCamera*
    void* m_sink = nullptr;         // QVideoSink*
    void* m_session = nullptr;      // QMediaCaptureSession*
};

CameraCapture::CameraCapture(QObject* parent) : QObject(parent) {
    // Dedicated thread with a live event loop for camera start()/stop().
    m_thread = new QThread(this);
    m_control = new CameraControl(this, nullptr);
    m_control->moveToThread(m_thread);
    connect(m_control, &CameraControl::frameReady,
            this, &CameraCapture::onFrameCaptured);
    m_thread->start();
}

CameraCapture::~CameraCapture() {
    shutdown();
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        // m_control is parented to m_thread and is deleted when the thread is.
        delete m_thread;
        m_thread = nullptr;
    }
}

bool CameraCapture::initialize(int cameraIndex) {
    if (isInitialized()) {
        shutdown();
    }

    // Run all camera setup on the dedicated camera thread. BlockingQueued so we
    // wait until the camera is actually created/started (or fails) before
    // returning; the synchronization point makes the resulting m_initialized
    // visible to the caller without needing a running event loop here.
    QMetaObject::invokeMethod(m_control, "start", Qt::BlockingQueuedConnection,
                              Q_ARG(int, cameraIndex));
    return m_control->isInitialized();
}

void CameraCapture::shutdown() {
    if (!m_control || !m_control->isInitialized()) return;

    // Tear the camera down on its own thread (which has a live event loop) so
    // stop() is guaranteed to receive its completion signal instead of
    // blocking the calling thread indefinitely.
    QMetaObject::invokeMethod(m_control, "stop", Qt::BlockingQueuedConnection);
}

bool CameraCapture::isInitialized() const {
    return m_control ? m_control->isInitialized() : false;
}

QImage CameraCapture::captureFrame() {
    if (!isInitialized()) return QImage();
    QMutexLocker locker(&m_frameMutex);
    return m_currentFrame;
}

int CameraCapture::cameraCount() const {
    return QMediaDevices::videoInputs().size();
}

QString CameraCapture::cameraName(int index) const {
    QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (index >= 0 && index < cameras.size()) {
        return cameras[index].description();
    }
    return {};
}

bool CameraCapture::setCameraIndex(int index) {
    if (index == m_cameraIndex) return true;
    if (index < 0) return false;

    QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (index >= cameras.size()) return false;

    if (isInitialized()) {
        initialize(index);
    } else {
        m_cameraIndex = index;
    }
    return true;
}

int CameraCapture::cameraIndex() const {
    return m_cameraIndex;
}

void CameraCapture::onFrameCaptured(const QVideoFrame& frame) {
    if (!frame.isValid()) return;
    QImage img = frame.toImage();
    if (img.isNull()) return;
    QImage rgb = img.convertToFormat(QImage::Format_RGB32);
    {
        QMutexLocker locker(&m_frameMutex);
        m_currentFrame = rgb;
    }
    emit frameCaptured(rgb);
}

} // namespace xrk

#include "camera_capture.moc"
