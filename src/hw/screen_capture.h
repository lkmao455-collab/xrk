#pragma once

#include <QObject>
#include <QImage>
#include <QRect>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <memory>
#include "core/types.h"

#ifdef _WIN32
struct DXGI_OUTDUPL_FRAME_INFO;
#endif

namespace xrk {

class ScreenCapture : public QObject {
    Q_OBJECT
public:
    explicit ScreenCapture(QObject* parent = nullptr);
    ~ScreenCapture();

    bool initialize();
    void shutdown();

    QImage captureFrame();
    QImage captureFrame(int monitorIndex);
    // Capture a frame and, when supported by the backend (DXGI Desktop
    // Duplication), also report the regions that changed since the previous
    // frame. Empty dirtyRects means "no hint" and the encoder must fall back
    // to a software diff. This is the entry point for the weak-network
    // tiled/differential transport.
    QImage captureFrame(QList<QRect>* dirtyRects);
    QImage captureFrame(int monitorIndex, QList<QRect>* dirtyRects);
    bool isInitialized() const;

    void setCaptureRect(const QRect& rect);
    QRect captureRect() const;

    void setTargetFps(int fps);
    int targetFps() const;

    QList<MonitorInfo> getMonitorList() const;
    int monitorCount() const;
    void setMonitorIndex(int index);
    int monitorIndex() const;

    // Thread-safe monitor switch with result feedback
    bool switchMonitorSafe(int index);

signals:
    void frameCaptured(const QImage& frame);
    void captureError(const QString& errorString);
    void monitorSwitchCompleted(bool success, int newIndex);

private:
    bool initializeDxgi();
    void shutdownDxgi();
    QImage captureDxgiFrame();
    QImage captureDxgiFrame(int monitorIndex);
    // Core DXGI grab. When dirtyRects is non-null it is filled with the
    // regions Desktop Duplication reported as changed since the previous
    // successful grab (move rects are reported as their destination area).
    QImage captureDxgiFrameEx(QList<QRect>* dirtyRects);
#ifdef _WIN32
    void collectDxgiDirtyRects(const DXGI_OUTDUPL_FRAME_INFO& frameInfo,
                               QList<QRect>* dirtyRects,
                               quint64 currentFrameHash);
#endif

    bool initializeGdi();
    QImage captureGdiFrame();
    QImage captureGdiFrame(int monitorIndex);

    bool initializeLinux();
    void shutdownLinux();
    QImage captureLinuxFrame();
    QImage captureLinuxFrame(int monitorIndex);

    bool initializeMac();
    void shutdownMac();
    QImage captureMacFrame();
    QImage captureMacFrame(int monitorIndex);

    bool m_initialized = false;
    bool m_useDxgi = false;
    bool m_dxgiFallback = false;
    int m_dxFailCount = 0;
    // Set whenever duplication metadata was dropped (init, device reset, a
    // failed grab). The next dirty-rect query then reports the whole screen
    // so the encoder cannot keep stale tiles.
    bool m_dirtyRectsStale = true;
    // Frame-level change signature used as a fallback when DDA cannot report
    // dirty regions: only a genuinely changed frame forces a full-screen
    // redraw, an identical frame is reported as "no change" and skipped.
    quint64 m_lastFrameHash = 0;
    bool m_hasLastFrameHash = false;
    QRect m_captureRect;
    int m_targetFps = 30;
    int m_monitorIndex = 0;
    QList<MonitorInfo> m_monitors;

    // Thread safety: protects all member variables during concurrent access
    // from capture thread (captureFrame) and main thread (setMonitorIndex/shutdown)
    mutable QMutex m_mutex;

    struct DxgiContext;
    std::unique_ptr<DxgiContext> m_dxgiContext;

    struct LinuxContext;
    std::unique_ptr<LinuxContext> m_linuxContext;

    struct MacContext;
    std::unique_ptr<MacContext> m_macContext;
};

} // namespace xrk
