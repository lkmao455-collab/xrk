#pragma once

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QShortcut>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QColor>
#include <QVector>
#include <QScrollArea>
#include <QPropertyAnimation>
#include <QLineEdit>
#include <QTextBrowser>
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
    void sessionChatMessage(const QString& message);

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
    bool eventFilter(QObject* obj, QEvent* event) override;

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
    void onMonitorSwitchCompleted(bool success, int newIndex);
    void onAutoSwitchStatusReceived(bool active, bool paused, int intervalMs, int currentIndex, int monitorCount, int nextIndex);
    void onThumbnailClicked(int monitorIndex);
    void onThumbnailUpdate();
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
    QPushButton* m_statsToggleBtn = nullptr;  // toggle connection stats overlay
    QWidget* m_statsPanel = nullptr;         // overlay panel with detailed stats
    QLabel* m_statsFpsLabel = nullptr;
    QLabel* m_statsBandwidthLabel = nullptr;
    QLabel* m_statsLatencyLabel = nullptr;
    QLabel* m_statsCodecLabel = nullptr;
    QLabel* m_statsResolutionLabel = nullptr;
    bool m_statsPanelVisible = false;
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
    // Committed strokes (remote/frame coords). Each stroke carries its own
    // colour/width so the on-host overlay can reproduce it exactly.
    QList<AnnotationStroke> m_strokes;
    AnnotationStroke m_currentStroke;       // in-progress stroke (remote coords)
    bool m_watermarkEnabled = false;
    QRect m_frameTargetRect;               // where the frame is drawn (widget coords)
    QLabel* m_consentLabel = nullptr;      // "waiting for host approval" overlay
    
    // Monitor switch transition effects
    bool m_monitorSwitching = false;       // true during monitor switch
    QLabel* m_switchingLabel = nullptr;    // "切换中..." overlay
    QImage m_lastFrameBeforeSwitch;        // preserve last frame during switch
    QTimer* m_switchFadeTimer = nullptr;   // fade-in animation timer
    qreal m_switchFadeOpacity = 0.0;       // current opacity for fade-in

    // Auto-switch cycling controls
    QPushButton* m_autoSwitchStartButton = nullptr;   // start cycling
    QPushButton* m_autoSwitchStopButton = nullptr;    // stop cycling
    QPushButton* m_autoSwitchPauseButton = nullptr;   // pause/resume
    QSpinBox* m_autoSwitchIntervalSpinBox = nullptr;  // interval in seconds
    QLabel* m_autoSwitchStatusLabel = nullptr;        // status display
    bool m_autoSwitchActive = false;
    bool m_autoSwitchPaused = false;
    void updateAutoSwitchUI();

    // Multi-monitor thumbnail panel
    QPushButton* m_thumbnailToggleButton = nullptr;   // toggle thumbnail mode
    bool m_thumbnailMode = false;                     // show thumbnails of other monitors
    QWidget* m_thumbnailPanel = nullptr;              // side panel for thumbnails
    QVBoxLayout* m_thumbnailLayout = nullptr;
    QScrollArea* m_thumbnailScrollArea = nullptr;

    // Ctrl+Alt+Del button
    QPushButton* m_ctrlAltDelButton = nullptr;

    // Screen zoom (Ctrl+mouse wheel)
    double m_zoomFactor = 1.0;
    static constexpr double kMinZoom = 0.25;
    static constexpr double kMaxZoom = 4.0;

    // Remote audio forwarding toggle
    QPushButton* m_audioButton = nullptr;
    bool m_audioEnabled = false;
    void onAudioToggled(bool checked);

    // Session idle lock
    QTimer* m_idleTimer = nullptr;
    int m_idleTimeoutSec = 300;  // default 5 minutes
    bool m_idleLockEnabled = false;
    QLabel* m_idleLockOverlay = nullptr;
    void resetIdleTimer();
    void onIdleTimeout();
    void setIdleLockEnabled(bool enabled, int timeoutSec = 300);

    // In-session chat overlay
    QWidget* m_chatOverlay = nullptr;
    QTextBrowser* m_chatOverlayDisplay = nullptr;
    QLineEdit* m_chatOverlayInput = nullptr;
    QPushButton* m_chatOverlaySendBtn = nullptr;
    QPushButton* m_chatOverlayToggleBtn = nullptr;
    bool m_chatOverlayVisible = false;
    struct ThumbnailInfo {
        QLabel* label = nullptr;
        QImage currentImage;
        int monitorIndex = -1;
    };
    QList<ThumbnailInfo> m_thumbnails;
    QTimer* m_thumbnailTimer = nullptr;               // 1s update timer
    int m_mainMonitorIndex = 0;                       // currently shown in main area
    QList<MonitorInfo> m_monitorList;                 // all monitors
    void setupThumbnailPanel();
    void updateThumbnailPanel();
    void requestThumbnailFrame(int monitorIndex);

    // Top toolbar that hosts the action buttons (annotation / color / clear /
    // watermark / mic / privacy) so they no longer overlap the remote desktop.
    QWidget* m_toolbar = nullptr;
    QHBoxLayout* m_toolbarLayout = nullptr;

    // Toolbar auto-hide
    QTimer* m_toolbarHideTimer = nullptr;
    QPropertyAnimation* m_toolbarShowAnim = nullptr;
    QPropertyAnimation* m_toolbarHideAnim = nullptr;
    bool m_toolbarVisible = true;
    bool m_toolbarAutoHide = true;       // enable auto-hide in fullscreen
    void showToolbar();
    void hideToolbar();
    void startToolbarHideTimer();
    void applyToolbarStyle();

    // UI theme
    void applyDarkTheme();
};

} // namespace xrk
