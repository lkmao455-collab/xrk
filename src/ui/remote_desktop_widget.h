#pragma once

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QShortcut>
#include <QPushButton>
#include <QComboBox>
#include <QHBoxLayout>
#include <QColor>
#include <QVector>
#include <memory>
#include "core/types.h"

namespace xrk {

class RemoteController;
class VideoDecoder;

class RemoteDesktopWidget : public QWidget {
    Q_OBJECT
public:
    explicit RemoteDesktopWidget(RemoteController* controller, QWidget* parent = nullptr);
    ~RemoteDesktopWidget();

    void startRemote(const QString& ip, uint16_t port);
    void startRemote(const QString& ip, uint16_t port, const QString& password);
    void stopRemote();
    // Enter the concealed UI mode used by a silent monitoring session: hide the
    // privacy-screen control and default to NOT forwarding local input.
    void enterSilentUiMode();
    bool isRemoteActive() const;
    void toggleFullscreen();

signals:
    void remoteStarted();
    void remoteStopped();
    void mouseEventSent(const MouseEvent& event);
    void keyEventSent(const KeyEvent& event);
    void filesDropped(const QStringList& filePaths);
    void downloadFileRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onScreenFrameReceived(const ScreenFrame& frame);
    void onScreenImageReceived(const QImage& image);
    void onFpsTimer();
    void onFrameRequestTimer();
    void onQualityInfoReceived(const QualityInfo& info);
    void onLatencyUpdated(qint64 ms);
    void onPrivacyScreenClicked();
    void onTakeoverClicked();
    void onBlockInputClicked();
    void onMonitorListReceived(const QList<MonitorInfo>& monitors, int currentMonitorIndex);
    void onAnnotationToggled(bool checked);
    void onAnnotateColorClicked();
    void onAnnotateClearClicked();
    void onWatermarkToggled(bool checked);
    void onMicToggled(bool checked);
    void onConsentRequested();
    void onConsentGranted();
    void onConsentDenied();

private:
    void setupUI();
    void setupFpsTimer();
    void setupShortcuts();
    void sendMouseEventToRemote(MouseAction action, MouseButton button, int x, int y, int delta = 0);
    void sendKeyEventToRemote(uint32_t keyCode, bool pressed, uint32_t modifiers,
                             const QString& text = QString());
    QPoint mapToRemote(const QPoint& localPos);
    QPoint remoteToWidget(const QPoint& remotePos) const;
    void updateQualityLabel();
    void updateColorButtonSwatch();

    RemoteController* m_controller = nullptr;
    QLabel* m_displayLabel = nullptr;
    QTimer* m_fpsTimer = nullptr;
    QTimer* m_frameRequestTimer = nullptr;
    QImage m_currentFrame;
    int m_frameCount = 0;
    int m_currentFps = 0;
    bool m_active = false;
    bool m_fullscreen = false;
    QString m_currentDeviceId;
    QShortcut* m_fullscreenShortcut = nullptr;
#ifdef XRK_FFMPEG_AVAILABLE
    std::unique_ptr<VideoDecoder> m_h264Decoder;
#endif
    QualityInfo m_currentQuality;
    qint64 m_roundTripMs = 0;
    QLabel* m_qualityLabel = nullptr;
    QPushButton* m_privacyButton = nullptr;
    QComboBox* m_monitorCombo = nullptr;
    QComboBox* m_qualityCombo = nullptr;   // gear selector: 自动/流畅/标准/高清/游戏
    bool m_privacyEnabled = false;

    // Silent-monitoring controls (plan §2.4): take over the controlled
    // machine's input, and/or block its local keyboard & mouse.
    QPushButton* m_takeoverButton = nullptr;   // checkable, default off
    QPushButton* m_blockInputButton = nullptr; // checkable, default off
    bool m_takeoverEnabled = false;
    bool m_blockInputEnabled = false;
    bool m_silentUiMode = false;           // hides privacy button, no loud "monitoring" chrome

    // Phase 4: local annotation overlay (whiteboard) + session watermark.
    // Strokes are stored in remote (frame) coordinates so they stay aligned
    // with the displayed frame regardless of scaling/letterboxing. v1 is a
    // local-only overlay (not transmitted to the host).
    QPushButton* m_annotateButton = nullptr;
    QPushButton* m_annotateColorButton = nullptr;
    QPushButton* m_annotateClearButton = nullptr;
    QPushButton* m_watermarkButton = nullptr;
    QPushButton* m_micButton = nullptr;       // two-way voice microphone toggle
    bool m_annotationEnabled = false;
    QColor m_annotationColor = Qt::red;
    int m_annotationWidth = 3;
    QVector<QVector<QPoint>> m_strokes;   // committed strokes (remote coords)
    QVector<QPoint> m_currentStroke;       // in-progress stroke (remote coords)
    bool m_watermarkEnabled = false;
    QRect m_frameTargetRect;               // where the frame is drawn (widget coords)
    QLabel* m_consentLabel = nullptr;      // "waiting for host approval" overlay

    // Top toolbar that hosts the action buttons (annotation / color / clear /
    // watermark / mic / privacy) so they no longer overlap the remote desktop.
    QWidget* m_toolbar = nullptr;
    QHBoxLayout* m_toolbarLayout = nullptr;
};

} // namespace xrk
