#pragma once

#include <QObject>
#include <QImage>
#include <QRect>
#include <QList>
#include <memory>
#include "core/types.h"

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
    bool isInitialized() const;

    void setCaptureRect(const QRect& rect);
    QRect captureRect() const;

    void setTargetFps(int fps);
    int targetFps() const;

    QList<MonitorInfo> getMonitorList() const;
    int monitorCount() const;
    void setMonitorIndex(int index);
    int monitorIndex() const;

signals:
    void frameCaptured(const QImage& frame);
    void captureError(const QString& errorString);

private:
    bool initializeDxgi();
    void shutdownDxgi();
    QImage captureDxgiFrame();
    QImage captureDxgiFrame(int monitorIndex);

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
    QRect m_captureRect;
    int m_targetFps = 30;
    int m_monitorIndex = 0;
    QList<MonitorInfo> m_monitors;

    struct DxgiContext;
    std::unique_ptr<DxgiContext> m_dxgiContext;

    struct LinuxContext;
    std::unique_ptr<LinuxContext> m_linuxContext;

    struct MacContext;
    std::unique_ptr<MacContext> m_macContext;
};

} // namespace xrk
