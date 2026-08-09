#include "remote_desktop_widget.h"
#include "app/remote_controller.h"
#ifdef XRK_FFMPEG_AVAILABLE
#include "hw/video_decoder.h"
#endif
#include "core/logger.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QContextMenuEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QMenu>
#include <QAction>
#include <QPushButton>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QColorDialog>

namespace xrk {

RemoteDesktopWidget::RemoteDesktopWidget(RemoteController* controller, QWidget* parent)
    : QWidget(parent), m_controller(controller) {
    setupUI();
    setupFpsTimer();
    setupShortcuts();
    
    if (m_controller) {
        connect(m_controller, &RemoteController::screenFrameReceived,
                this, &RemoteDesktopWidget::onScreenFrameReceived);
        connect(m_controller, &RemoteController::screenImageReceived,
                this, &RemoteDesktopWidget::onScreenImageReceived);
        connect(m_controller, &RemoteController::qualityInfoReceived,
                this, &RemoteDesktopWidget::onQualityInfoReceived);
        connect(m_controller, &RemoteController::latencyUpdated,
                this, &RemoteDesktopWidget::onLatencyUpdated);
    }

    m_qualityLabel = new QLabel(this);
    m_qualityLabel->setObjectName("quality-overlay");
    m_qualityLabel->setText("\u753b\u8d28: \u81ea\u52a8");
    m_qualityLabel->adjustSize();

    // Connection quality stats toggle button
    m_statsToggleBtn = new QPushButton(QString::fromUtf8("\u2139"), this);
    m_statsToggleBtn->setFixedSize(28, 28);
    m_statsToggleBtn->setToolTip(tr("连接质量统计"));
    m_statsToggleBtn->setCheckable(true);
    connect(m_statsToggleBtn, &QPushButton::toggled, this, [this](bool checked) {
        m_statsPanelVisible = checked;
        if (m_statsPanel) m_statsPanel->setVisible(checked);
    });

    // Stats overlay panel
    m_statsPanel = new QWidget(this);
    m_statsPanel->setObjectName("stats-panel");
    m_statsPanel->setFixedSize(220, 140);
    m_statsPanel->setVisible(false);
    auto* statsLayout = new QVBoxLayout(m_statsPanel);
    statsLayout->setContentsMargins(10, 8, 10, 8);
    statsLayout->setSpacing(4);

    auto makeStatLabel = [this](const QString& text) -> QLabel* {
        auto* lbl = new QLabel(text, m_statsPanel);
        lbl->setStyleSheet("color: #ddd; font-size: 12px; background: rgba(0,0,0,0.7); border-radius: 4px; padding: 2px 6px;");
        return lbl;
    };
    m_statsFpsLabel = makeStatLabel("FPS: -");
    m_statsBandwidthLabel = makeStatLabel("带宽: -");
    m_statsLatencyLabel = makeStatLabel("延迟: -");
    m_statsCodecLabel = makeStatLabel("编码: JPEG");
    m_statsResolutionLabel = makeStatLabel("分辨率: -");
    statsLayout->addWidget(m_statsFpsLabel);
    statsLayout->addWidget(m_statsBandwidthLabel);
    statsLayout->addWidget(m_statsLatencyLabel);
    statsLayout->addWidget(m_statsCodecLabel);
    statsLayout->addWidget(m_statsResolutionLabel);
    m_statsPanel->setStyleSheet("#stats-panel { background: rgba(0,0,0,0.75); border-radius: 6px; }");

    m_privacyButton = new QPushButton(tr("隐私屏"), this);
    m_privacyButton->setCheckable(true);
    m_privacyButton->setObjectName("privacy-button");
    m_privacyButton->adjustSize();
    connect(m_privacyButton, &QPushButton::clicked, this, &RemoteDesktopWidget::onPrivacyScreenClicked);

    m_takeoverButton = new QPushButton(tr("接管键鼠"), this);
    m_takeoverButton->setCheckable(true);
    m_takeoverButton->setObjectName("takeover-button");
    m_takeoverButton->adjustSize();
    connect(m_takeoverButton, &QPushButton::clicked, this, &RemoteDesktopWidget::onTakeoverClicked);

    m_blockInputButton = new QPushButton(tr("禁用对方键鼠"), this);
    m_blockInputButton->setCheckable(true);
    m_blockInputButton->setObjectName("blockinput-button");
    m_blockInputButton->adjustSize();
    connect(m_blockInputButton, &QPushButton::clicked, this, &RemoteDesktopWidget::onBlockInputClicked);

    m_monitorCombo = new QComboBox(this);
    m_monitorCombo->setObjectName("monitor-combo");
    m_monitorCombo->hide();
    m_monitorCombo->setMinimumWidth(200);
    connect(m_monitorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (m_controller && m_active && !m_monitorSwitching) {
                    // Preserve current frame for transition effect
                    if (!m_currentFrame.isNull()) {
                        m_lastFrameBeforeSwitch = m_currentFrame;
                    }
                    m_monitorSwitching = true;
                    m_switchFadeOpacity = 0.0;
                    if (m_switchingLabel) m_switchingLabel->show();
                    update();
                    
                    m_controller->switchMonitor(index);
                }
            });

    // Phase 7: quality/latency gear selector. "游戏" maps to ULTRA + game mode
    // (host prefers the H264 encoder + 60fps for lower interactive latency).
    m_qualityCombo = new QComboBox(this);
    m_qualityCombo->setObjectName("quality-combo");
    m_qualityCombo->setMinimumWidth(96);
    m_qualityCombo->addItem(tr("自动"), static_cast<int>(QualityLevel::AUTO));
    m_qualityCombo->addItem(tr("流畅"), static_cast<int>(QualityLevel::LOW));
    m_qualityCombo->addItem(tr("标准"), static_cast<int>(QualityLevel::MEDIUM));
    m_qualityCombo->addItem(tr("高清"), static_cast<int>(QualityLevel::HIGH));
    m_qualityCombo->addItem(tr("游戏"), static_cast<int>(QualityLevel::ULTRA));
    m_qualityCombo->setCurrentIndex(0);
    connect(m_qualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (!m_controller || !m_active) return;
                QualityLevel level = static_cast<QualityLevel>(
                    m_qualityCombo->itemData(index).toInt());
                bool game = (level == QualityLevel::ULTRA);
                m_controller->sendQualityLevel(level, game);
                LOG_INFO("Quality gear -> " + QString::number(static_cast<int>(level)) +
                         (game ? " (game)" : ""));
            });

    if (m_controller) {
        connect(m_controller, &RemoteController::monitorListReceived,
                this, &RemoteDesktopWidget::onMonitorListReceived);
        connect(m_controller, &RemoteController::monitorSwitchCompleted,
                this, &RemoteDesktopWidget::onMonitorSwitchCompleted);
        connect(m_controller, &RemoteController::autoSwitchStatusReceived,
                this, &RemoteDesktopWidget::onAutoSwitchStatusReceived);
        connect(m_controller, &RemoteController::thumbnailFrameReceived,
                this, [this](int monitorIndex, const QImage& thumbnail) {
                    // Update thumbnail label
                    for (auto& thumb : m_thumbnails) {
                        if (thumb.monitorIndex == monitorIndex && thumb.label) {
                            // Scale thumbnail to fit label size
                            QPixmap pixmap = QPixmap::fromImage(thumbnail).scaled(
                                thumb.label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                            thumb.label->setPixmap(pixmap);
                            break;
                        }
                    }
                });
        connect(m_controller, &RemoteController::consentRequested,
                this, &RemoteDesktopWidget::onConsentRequested);
        connect(m_controller, &RemoteController::consentGranted,
                this, &RemoteDesktopWidget::onConsentGranted);
        connect(m_controller, &RemoteController::consentDenied,
                this, &RemoteDesktopWidget::onConsentDenied);
    }

    // Auto-switch cycling controls
    m_autoSwitchStartButton = new QPushButton(tr("开始轮巡"), this);
    m_autoSwitchStartButton->setObjectName("auto-switch-start");
    m_autoSwitchStartButton->adjustSize();
    connect(m_autoSwitchStartButton, &QPushButton::clicked, this, [this]() {
        if (m_controller && m_active) {
            m_controller->startAutoSwitch();
        }
    });

    m_autoSwitchStopButton = new QPushButton(tr("停止轮巡"), this);
    m_autoSwitchStopButton->setObjectName("auto-switch-stop");
    m_autoSwitchStopButton->adjustSize();
    connect(m_autoSwitchStopButton, &QPushButton::clicked, this, [this]() {
        if (m_controller && m_active) {
            m_controller->stopAutoSwitch();
        }
    });

    m_autoSwitchPauseButton = new QPushButton(tr("暂停"), this);
    m_autoSwitchPauseButton->setObjectName("auto-switch-pause");
    m_autoSwitchPauseButton->setCheckable(true);
    m_autoSwitchPauseButton->adjustSize();
    connect(m_autoSwitchPauseButton, &QPushButton::clicked, this, [this](bool checked) {
        if (m_controller && m_active) {
            if (checked) {
                m_controller->pauseAutoSwitch();
            } else {
                m_controller->resumeAutoSwitch();
            }
        }
    });

    m_autoSwitchIntervalSpinBox = new QSpinBox(this);
    m_autoSwitchIntervalSpinBox->setObjectName("auto-switch-interval");
    m_autoSwitchIntervalSpinBox->setRange(1, 60);
    m_autoSwitchIntervalSpinBox->setValue(3);
    m_autoSwitchIntervalSpinBox->setSuffix(tr("秒"));
    m_autoSwitchIntervalSpinBox->setMinimumWidth(80);
    connect(m_autoSwitchIntervalSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int value) {
        if (m_controller && m_active) {
            m_controller->setAutoSwitchInterval(value * 1000);
        }
    });

    m_autoSwitchStatusLabel = new QLabel(this);
    m_autoSwitchStatusLabel->setObjectName("auto-switch-status");
    m_autoSwitchStatusLabel->setText(tr("轮巡: 停止"));
    m_autoSwitchStatusLabel->adjustSize();

    updateAutoSwitchUI();

    // Thumbnail mode toggle button
    m_thumbnailToggleButton = new QPushButton(tr("多屏预览"), this);
    m_thumbnailToggleButton->setObjectName("thumbnail-toggle");
    m_thumbnailToggleButton->setCheckable(true);
    m_thumbnailToggleButton->adjustSize();
    connect(m_thumbnailToggleButton, &QPushButton::toggled, this, [this](bool checked) {
        m_thumbnailMode = checked;
        updateThumbnailPanel();
        LOG_INFO("Thumbnail mode " + QString(checked ? "enabled" : "disabled"));
    });

    // Monitor switching overlay label
    m_switchingLabel = new QLabel(tr("切换中..."), this);
    m_switchingLabel->setObjectName("switching-overlay");
    m_switchingLabel->setAlignment(Qt::AlignCenter);
    m_switchingLabel->setStyleSheet(
        "QLabel { background-color: rgba(0, 0, 0, 180); color: white; "
        "font-size: 16px; padding: 20px 40px; border-radius: 8px; }");
    m_switchingLabel->hide();
    
    // Fade-in animation timer
    m_switchFadeTimer = new QTimer(this);
    m_switchFadeTimer->setInterval(20); // 50fps animation
    connect(m_switchFadeTimer, &QTimer::timeout, this, [this]() {
        m_switchFadeOpacity += 0.1;
        if (m_switchFadeOpacity >= 1.0) {
            m_switchFadeOpacity = 1.0;
            m_switchFadeTimer->stop();
            m_monitorSwitching = false;
        }
        update();
    });

    m_consentLabel = new QLabel(this);
    m_consentLabel->setObjectName("consent-overlay");
    m_consentLabel->hide();

    // Phase 4: annotation (whiteboard) + watermark toolbar (overlay, top-left).
    m_annotateButton = new QPushButton(tr("标注"), this);
    m_annotateButton->setCheckable(true);
    m_annotateButton->setObjectName("annotate-button");
    m_annotateButton->adjustSize();
    connect(m_annotateButton, &QPushButton::toggled,
            this, &RemoteDesktopWidget::onAnnotationToggled);

    m_annotateColorButton = new QPushButton(tr("颜色"), this);
    m_annotateColorButton->setObjectName("annotate-color-button");
    m_annotateColorButton->adjustSize();
    connect(m_annotateColorButton, &QPushButton::clicked,
            this, &RemoteDesktopWidget::onAnnotateColorClicked);
    updateColorButtonSwatch();
    m_annotateColorButton->hide(); // contextual: only shown while annotating

    m_annotateClearButton = new QPushButton(tr("清空"), this);
    m_annotateClearButton->setObjectName("annotate-clear-button");
    m_annotateClearButton->adjustSize();
    connect(m_annotateClearButton, &QPushButton::clicked,
            this, &RemoteDesktopWidget::onAnnotateClearClicked);
    m_annotateClearButton->hide(); // contextual: only shown while annotating

    m_watermarkButton = new QPushButton(tr("水印"), this);
    m_watermarkButton->setCheckable(true);
    m_watermarkButton->setObjectName("watermark-button");
    m_watermarkButton->adjustSize();
    connect(m_watermarkButton, &QPushButton::toggled,
            this, &RemoteDesktopWidget::onWatermarkToggled);

    // Phase 6: two-way voice microphone toggle.
    m_micButton = new QPushButton(tr("麦克风"), this);
    m_micButton->setCheckable(true);
    m_micButton->setObjectName("mic-button");
    m_micButton->adjustSize();
    connect(m_micButton, &QPushButton::toggled,
            this, &RemoteDesktopWidget::onMicToggled);

    if (m_controller) {
        connect(m_controller, &RemoteController::microphoneStateChanged,
                m_micButton, &QPushButton::setChecked);
    }

    // Assemble the top toolbar so these action buttons live on a toolbar
    // instead of floating over the remote desktop. Order: monitor selector,
    // then 标注 / 颜色 / 清空 / 水印 / 麦克风 / 隐私屏.
    if (m_toolbarLayout) {
        m_toolbarLayout->addWidget(m_monitorCombo);
        m_toolbarLayout->addWidget(m_qualityCombo);
        m_toolbarLayout->addWidget(m_autoSwitchStartButton);
        m_toolbarLayout->addWidget(m_autoSwitchStopButton);
        m_toolbarLayout->addWidget(m_autoSwitchPauseButton);
        m_toolbarLayout->addWidget(m_autoSwitchIntervalSpinBox);
        m_toolbarLayout->addWidget(m_autoSwitchStatusLabel);
        m_toolbarLayout->addWidget(m_thumbnailToggleButton);
        m_toolbarLayout->addWidget(m_annotateButton);
        m_toolbarLayout->addWidget(m_annotateColorButton);
        m_toolbarLayout->addWidget(m_annotateClearButton);
        m_toolbarLayout->addWidget(m_watermarkButton);
        m_toolbarLayout->addWidget(m_micButton);

        // Remote audio forwarding toggle
        m_audioButton = new QPushButton(tr("喇叭"), m_toolbar);
        m_audioButton->setToolTip(tr("远程音频转发"));
        m_audioButton->setCheckable(true);
        m_audioButton->setObjectName("audio-button");
        m_audioButton->adjustSize();
        connect(m_audioButton, &QPushButton::toggled, this, &RemoteDesktopWidget::onAudioToggled);
        m_toolbarLayout->addWidget(m_audioButton);

        m_toolbarLayout->addWidget(m_privacyButton);
        m_toolbarLayout->addWidget(m_takeoverButton);
        m_toolbarLayout->addWidget(m_blockInputButton);

        // Ctrl+Alt+Del button
        m_ctrlAltDelButton = new QPushButton("Ctrl+Alt+Del", m_toolbar);
        m_ctrlAltDelButton->setToolTip(tr("发送 Ctrl+Alt+Del"));
        m_ctrlAltDelButton->setObjectName("ctrlaltdel-button");
        m_ctrlAltDelButton->adjustSize();
        connect(m_ctrlAltDelButton, &QPushButton::clicked, this, [this]() {
            if (!m_controller || !m_active) return;
            // Send Ctrl+Alt+Del: press all three keys in sequence, then release
            // VK_CONTROL=0x11, VK_MENU=0x12 (Alt), VK_DELETE=0x2E
            sendKeyEventToRemote(0x11, true, 0, QString());  // Ctrl down
            sendKeyEventToRemote(0x12, true, 0x11, QString());  // Alt down
            sendKeyEventToRemote(0x2E, true, 0x13, QString());  // Delete down
            sendKeyEventToRemote(0x2E, false, 0x13, QString()); // Delete up
            sendKeyEventToRemote(0x12, false, 0x11, QString()); // Alt up
            sendKeyEventToRemote(0x11, false, 0, QString());    // Ctrl up
        });
        m_toolbarLayout->addWidget(m_ctrlAltDelButton);

        // In-session chat toggle
        m_chatOverlayToggleBtn = new QPushButton(tr("聊天"), m_toolbar);
        m_chatOverlayToggleBtn->setToolTip(tr("会话聊天"));
        m_chatOverlayToggleBtn->setCheckable(true);
        m_chatOverlayToggleBtn->adjustSize();
        connect(m_chatOverlayToggleBtn, &QPushButton::toggled, this, [this](bool checked) {
            m_chatOverlayVisible = checked;
            if (m_chatOverlay) m_chatOverlay->setVisible(checked);
        });
        m_toolbarLayout->addWidget(m_chatOverlayToggleBtn);

        m_toolbarLayout->addStretch(1);
    }

    // Toolbar buttons must NOT take keyboard focus, otherwise clicking one
    // (e.g. 隐私屏) steals focus from this widget and remote keystrokes stop
    // being delivered. The remote desktop keeps focus so keyboard control of
    // the host keeps working after toggling any toolbar action.
    for (QPushButton* b : {m_annotateButton, m_annotateColorButton,
                            m_annotateClearButton, m_watermarkButton,
                            m_micButton, m_audioButton, m_privacyButton,
                            m_takeoverButton, m_blockInputButton,
                            m_autoSwitchStartButton, m_autoSwitchStopButton,
                            m_autoSwitchPauseButton, m_thumbnailToggleButton,
                            m_ctrlAltDelButton, m_chatOverlayToggleBtn}) {
        if (b) b->setFocusPolicy(Qt::NoFocus);
    }
    // The gear selector and spin box must also not steal keyboard focus
    if (m_qualityCombo) m_qualityCombo->setFocusPolicy(Qt::NoFocus);
    if (m_autoSwitchIntervalSpinBox) m_autoSwitchIntervalSpinBox->setFocusPolicy(Qt::NoFocus);
}

RemoteDesktopWidget::~RemoteDesktopWidget() {
}

void RemoteDesktopWidget::startRemote(const QString& ip, uint16_t port) {
    startRemote(ip, port, QString());
}

void RemoteDesktopWidget::startRemote(const QString& ip, uint16_t port, const QString& password) {
    if (!m_controller) return;
    
    if (m_controller->startRemote(ip, port, password)) {
        m_active = true;
        m_currentDeviceId = ip;
        m_fpsTimer->start(1000);
        m_frameRequestTimer->start(33);
        if (m_qualityCombo) m_qualityCombo->setCurrentIndex(0); // host defaults to AUTO
        emit remoteStarted();
    }
}

void RemoteDesktopWidget::stopRemote() {
    if (!m_controller) return;
    
    m_controller->stopRemote();
    m_active = false;
    m_currentDeviceId.clear();
    m_currentFrame = QImage();
    m_fpsTimer->stop();
    m_frameRequestTimer->stop();
    if (m_idleTimer) m_idleTimer->stop();
    if (m_idleLockOverlay) m_idleLockOverlay->hide();
    if (m_monitorCombo) m_monitorCombo->hide();
    // Clear annotation state so a new session starts clean.
    m_strokes.clear();
    m_currentStroke.points.clear();
    update();
    emit remoteStopped();
}

bool RemoteDesktopWidget::isRemoteActive() const {
    return m_active;
}

void RemoteDesktopWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    // The top toolbar occupies the strip [0, top); the remote desktop is drawn
    // only in the area below it so the action buttons never overlap the frame.
    int top = m_toolbar ? m_toolbar->height() : 0;
    QRect display(0, top, width(), height() - top);

    QPainter painter(this);

    if (m_currentFrame.isNull()) {
        painter.fillRect(display, Qt::black);
        painter.setPen(Qt::white);
        painter.drawText(display, Qt::AlignCenter, "等待连接...");
        return;
    }

    // Handle monitor switch transition
    if (m_monitorSwitching && !m_lastFrameBeforeSwitch.isNull()) {
        // Draw last frame at reduced opacity during switch
        QImage scaled = m_lastFrameBeforeSwitch.scaled(display.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QRect targetRect(display.x() + (display.width() - scaled.width()) / 2,
                         display.y() + (display.height() - scaled.height()) / 2,
                         scaled.width(), scaled.height());
        painter.setOpacity(0.5);
        painter.drawImage(targetRect, scaled);
        painter.setOpacity(1.0);
        
        // Position switching overlay
        if (m_switchingLabel) {
            m_switchingLabel->adjustSize();
            m_switchingLabel->move(display.center().x() - m_switchingLabel->width() / 2,
                                   display.center().y() - m_switchingLabel->height() / 2);
            m_switchingLabel->show();
            m_switchingLabel->raise();
        }
        return;
    }

    QImage scaled = m_currentFrame.scaled(
        static_cast<int>(display.width() * m_zoomFactor),
        static_cast<int>(display.height() * m_zoomFactor),
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QRect targetRect(display.x() + (display.width() - scaled.width()) / 2,
                     display.y() + (display.height() - scaled.height()) / 2,
                     scaled.width(), scaled.height());
    m_frameTargetRect = targetRect;

    // Apply fade-in opacity during transition
    if (m_switchFadeOpacity > 0.0 && m_switchFadeOpacity < 1.0) {
        painter.setOpacity(m_switchFadeOpacity);
    }
    painter.drawImage(targetRect, scaled);
    painter.setOpacity(1.0);

    painter.setPen(Qt::green);
    painter.drawText(display.x() + 10, display.y() + 20, QString("FPS: %1").arg(m_currentFps));

    // Show decoded frame count and frame dimensions for diagnostics
    painter.setPen(Qt::cyan);
    painter.drawText(display.x() + 10, display.y() + 40,
        QString("Frames: %1 | %2x%3")
            .arg(m_frameCount)
            .arg(m_currentFrame.width())
            .arg(m_currentFrame.height()));

    // Phase 4: draw local annotation strokes (in remote/frame coordinates).
    if (!m_strokes.isEmpty() || !m_currentStroke.points.isEmpty()) {
        auto drawStroke = [&](const AnnotationStroke& stroke) {
            if (stroke.points.isEmpty()) return;
            QPen pen(stroke.color, stroke.width);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            if (stroke.points.size() == 1) {
                painter.drawPoint(remoteToWidget(stroke.points.first()));
                return;
            }
            for (int i = 1; i < stroke.points.size(); ++i) {
                painter.drawLine(remoteToWidget(stroke.points[i - 1]), remoteToWidget(stroke.points[i]));
            }
        };
        for (const AnnotationStroke& s : m_strokes) drawStroke(s);
        // The in-progress stroke uses the live (current) colour/width.
        AnnotationStroke live = m_currentStroke;
        live.color = m_annotationColor;
        live.width = m_annotationWidth;
        drawStroke(live);
    }

    // Phase 4: session watermark (viewer device id / ip) in bottom-right.
    if (m_watermarkEnabled && !m_currentDeviceId.isEmpty()) {
        QString wm = tr("查看端: %1").arg(m_currentDeviceId);
        QFont wmFont = painter.font();
        wmFont.setPointSize(11);
        painter.setFont(wmFont);
        QFontMetrics fm(wmFont);
        int w = fm.horizontalAdvance(wm) + 12;
        int h = fm.height() + 6;
        int wx = display.right() - w - 10;
        int wy = display.bottom() - h - 10;
        painter.fillRect(wx, wy, w, h, QColor(0, 0, 0, 140));
        painter.setPen(Qt::white);
        painter.drawText(wx + 6, wy + fm.ascent() + 3, wm);
    }

    if (m_qualityLabel) {
        m_qualityLabel->adjustSize();
        m_qualityLabel->move(width() - m_qualityLabel->width() - 8, top + 8);
        m_qualityLabel->raise();
    }
    if (m_statsToggleBtn) {
        m_statsToggleBtn->move(width() - 36, top + 8);
        m_statsToggleBtn->raise();
    }
    if (m_statsPanel) {
        m_statsPanel->move(width() - 228, top + 40);
        m_statsPanel->raise();
    }
    if (m_chatOverlay) {
        m_chatOverlay->move(width() - 290, height() - 220);
        m_chatOverlay->raise();
    }

    if (m_consentLabel && m_consentLabel->isVisible()) {
        m_consentLabel->adjustSize();
        m_consentLabel->move(display.center().x() - m_consentLabel->width() / 2,
                             display.center().y() - m_consentLabel->height() / 2);
        m_consentLabel->raise();
    }
}

void RemoteDesktopWidget::mouseMoveEvent(QMouseEvent* event) {
    resetIdleTimer();
    if (!m_active) return;

    // Toolbar auto-show in fullscreen: show when mouse near top
    if (m_fullscreen && m_toolbarAutoHide && !m_toolbarVisible) {
        if (event->pos().y() < 50) {
            showToolbar();
            return;
        }
    }

    // Reset hide timer when mouse moves in toolbar area
    if (m_fullscreen && m_toolbarAutoHide && m_toolbarVisible) {
        if (event->pos().y() < m_toolbar->height() + 20) {
            m_toolbarHideTimer->start();
        }
    }

    // Phase 4: while annotating, capture the stroke locally instead of sending input.
    if (m_annotationEnabled && !m_currentFrame.isNull() && !m_currentStroke.points.isEmpty()) {
        m_currentStroke.points.append(mapToRemote(event->pos()));
        update();
        return;
    }

    QPoint remotePos = mapToRemote(event->pos());
    sendMouseEventToRemote(MouseAction::MOVE, MouseButton::LEFT, remotePos.x(), remotePos.y());
}

void RemoteDesktopWidget::mousePressEvent(QMouseEvent* event) {
    resetIdleTimer();
    if (!m_active) return;

    // Phase 4: begin a local annotation stroke.
    if (m_annotationEnabled && !m_currentFrame.isNull()) {
        m_currentStroke.points.clear();
        m_currentStroke.color = m_annotationColor;
        m_currentStroke.width = m_annotationWidth;
        m_currentStroke.points.append(mapToRemote(event->pos()));
        update();
        return;
    }
    
    MouseButton button = MouseButton::LEFT;
    if (event->button() == Qt::RightButton) {
        button = MouseButton::RIGHT;
    } else if (event->button() == Qt::MiddleButton) {
        button = MouseButton::MIDDLE;
    }
    
    QPoint remotePos = mapToRemote(event->pos());
    sendMouseEventToRemote(MouseAction::PRESS, button, remotePos.x(), remotePos.y());
}

void RemoteDesktopWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (!m_active) return;

    // Phase 4: commit the local annotation stroke.
    if (m_annotationEnabled && !m_currentStroke.points.isEmpty()) {
        if (m_currentStroke.points.size() > 1) {
            m_strokes.append(m_currentStroke);
        }
        m_currentStroke.points.clear();
        update();
        // v1.6.0: push the full committed stroke set to the host so its
        // overlay mirrors exactly what the controller currently sees.
        if (m_controller && !m_strokes.isEmpty() && !m_currentFrame.isNull()) {
            AnnotationUpdate update;
            update.frameWidth = m_currentFrame.width();
            update.frameHeight = m_currentFrame.height();
            update.strokes = m_strokes;
            m_controller->sendAnnotationUpdate(update);
        }
        return;
    }
    
    MouseButton button = MouseButton::LEFT;
    if (event->button() == Qt::RightButton) {
        button = MouseButton::RIGHT;
    } else if (event->button() == Qt::MiddleButton) {
        button = MouseButton::MIDDLE;
    }
    
    QPoint remotePos = mapToRemote(event->pos());
    sendMouseEventToRemote(MouseAction::RELEASE, button, remotePos.x(), remotePos.y());
}

void RemoteDesktopWidget::wheelEvent(QWheelEvent* event) {
    if (!m_active) return;
    
    // Ctrl+mouse wheel = zoom in/out
    if (event->modifiers() & Qt::ControlModifier) {
        int delta = event->angleDelta().y();
        double oldZoom = m_zoomFactor;
        if (delta > 0) {
            m_zoomFactor = qMin(m_zoomFactor * 1.15, kMaxZoom);
        } else {
            m_zoomFactor = qMax(m_zoomFactor / 1.15, kMinZoom);
        }
        if (qAbs(m_zoomFactor - oldZoom) > 0.001) {
            update();
        }
        event->accept();
        return;
    }
    
    int delta = event->angleDelta().y();
    QPoint pos = mapToRemote(event->position().toPoint());
    sendMouseEventToRemote(MouseAction::SCROLL, MouseButton::LEFT, pos.x(), pos.y(), delta);
}

void RemoteDesktopWidget::keyPressEvent(QKeyEvent* event) {
    resetIdleTimer();
    if (!m_active) return;
    // Use nativeVirtualKey() which gives the platform VK code (Windows VK_*, Linux KeySym, macOS CGKeyCode).
    // nativeScanCode() gives hardware scan codes which are NOT virtual key codes
    // and the host's InputControl::keyEvent expects VK codes.
    // Also forward the typed character(s) so the host can inject the exact
    // Unicode text (KEYEVENTF_UNICODE), keeping remote typing correct even when
    // the host uses a different keyboard layout or an active IME.
    sendKeyEventToRemote(event->nativeVirtualKey(), true, event->modifiers(), event->text());
}

void RemoteDesktopWidget::keyReleaseEvent(QKeyEvent* event) {
    if (!m_active) return;
    sendKeyEventToRemote(event->nativeVirtualKey(), false, event->modifiers(), event->text());
}

void RemoteDesktopWidget::onScreenFrameReceived(const ScreenFrame& frame) {
    static int receivedCount = 0;
    static int decodeOkCount = 0;
    static int decodeFailCount = 0;
    receivedCount++;

    QImage image;
    if (frame.format == FrameFormat::JPEG) {
        bool loaded = image.loadFromData(frame.data, "JPEG");
        if (!loaded) {
            LOG_WARNING("[Widget] frame#" + QString::number(receivedCount) + 
                        " JPEG loadFromData failed, dataSz=" + QString::number(frame.data.size()) +
                        " first4=0x" + frame.data.left(4).toHex());
        }
    } else if (frame.format == FrameFormat::H264) {
#ifdef XRK_FFMPEG_AVAILABLE
        if (!m_h264Decoder) {
            m_h264Decoder = std::make_unique<VideoDecoder>();
            bool initOk = m_h264Decoder->initialize();
            if (!initOk) {
                LOG_ERROR("[Widget] H264 decoder initialization FAILED - "
                          "H.264 frames will not be displayed! "
                          "Check FFmpeg libavcodec H264 decoder support. "
                          "Falling back to JPEG if available.");
            } else {
                LOG_INFO("[Widget] H264 decoder initialized successfully");
            }
        }
        if (m_h264Decoder->isInitialized()) {
            image = m_h264Decoder->decode(frame.data);
            if (image.isNull()) {
                LOG_WARNING("[Widget] frame#" + QString::number(receivedCount) + 
                            " H264 decode returned null image, dataSz=" + QString::number(frame.data.size()));
            }
        } else {
            if (decodeFailCount < 5) {
                LOG_WARNING("[Widget] H264 decoder not initialized - cannot decode H264 frames");
            }
        }
#else
        if (decodeFailCount < 5) {
            LOG_WARNING("[Widget] H264 decoder not available (FFmpeg not built)");
        }
#endif
    } else {
        if (decodeFailCount < 5) {
            LOG_WARNING("[Widget] Unknown frame format: " + QString::number(static_cast<int>(frame.format)));
        }
    }

    if (!image.isNull()) {
        ++decodeOkCount;
        m_currentFrame = image;
        m_frameCount++;
        static bool firstDisplay = false;
        if (!firstDisplay) {
            firstDisplay = true;
            LOG_INFO("[DIAG] Widget: FIRST frame DECODED and displayed " +
                     QString::number(image.width()) + "x" + QString::number(image.height()));
        }
        update();
        if (decodeOkCount <= 5 || decodeOkCount % 300 == 0) {
            LOG_INFO("[Widget] frame#" + QString::number(receivedCount) +
                     " DECODE OK#" + QString::number(decodeOkCount) +
                     " sz=" + QString::number(image.width()) + "x" + QString::number(image.height()) +
                     " fmt=" + QString::number(static_cast<int>(frame.format)));
        }
    } else {
        ++decodeFailCount;
        if (decodeFailCount <= 10 || decodeFailCount % 100 == 0) {
            LOG_WARNING("[Widget] frame#" + QString::number(receivedCount) +
                        " DECODE FAIL#" + QString::number(decodeFailCount) +
                        " dataSz=" + QString::number(frame.data.size()) +
                        " fmt=" + QString::number(static_cast<int>(frame.format)) +
                        " first4=0x" + frame.data.left(4).toHex() +
                        " | TROUBLESHOOTING: If H264, check decoder init. If JPEG, check data corruption. "
                        "If all frames fail, check host encoder (H264 MF_E_NO_SAMPLE_TIMESTAMP?) and screen capture.");
        }
    }
}

void RemoteDesktopWidget::onScreenImageReceived(const QImage& image) {
    if (image.isNull()) return;
    m_currentFrame = image;
    m_frameCount++;
    update();
}

void RemoteDesktopWidget::onFpsTimer() {
    m_currentFps = m_frameCount;
    m_frameCount = 0;
    update();
}

void RemoteDesktopWidget::onFrameRequestTimer() {
    if (m_controller && m_active) {
        m_controller->requestScreenFrame();
    }
}

void RemoteDesktopWidget::onQualityInfoReceived(const QualityInfo& info) {
    m_currentQuality = info;
    updateQualityLabel();
}

void RemoteDesktopWidget::onLatencyUpdated(qint64 ms) {
    m_roundTripMs = ms;
    updateQualityLabel();
}

void RemoteDesktopWidget::updateQualityLabel() {
    if (!m_qualityLabel) return;

    const QualityInfo& info = m_currentQuality;
    QString qualityText;
    if (info.jpegQuality >= 80) qualityText = "超高";
    else if (info.jpegQuality >= 65) qualityText = "高";
    else if (info.jpegQuality >= 50) qualityText = "中";
    else qualityText = "低";

    QString bwText;
    if (info.bandwidthKbps >= 1024) {
        bwText = QString("%1 Mbps").arg(info.bandwidthKbps / 1024.0, 0, 'f', 1);
    } else {
        bwText = QString("%1 Kbps").arg(info.bandwidthKbps);
    }

    QString rttText = (m_roundTripMs > 0)
        ? QString(" | %1ms").arg(m_roundTripMs)
        : QString();

    m_qualityLabel->setText(
        QString("画质: %1 [Q:%2] | %3 | %4fps%5")
            .arg(qualityText)
            .arg(info.jpegQuality)
            .arg(bwText)
            .arg(info.captureFps)
            .arg(rttText));
    m_qualityLabel->adjustSize();

    // Update stats panel
    if (m_statsFpsLabel) m_statsFpsLabel->setText(QString("FPS: %1").arg(info.captureFps));
    if (m_statsBandwidthLabel) m_statsBandwidthLabel->setText(QString("带宽: %1").arg(bwText));
    if (m_statsLatencyLabel) m_statsLatencyLabel->setText(QString("延迟: %1ms").arg(m_roundTripMs > 0 ? m_roundTripMs : -1));
    if (m_statsCodecLabel) m_statsCodecLabel->setText(QString("编码: %1").arg(m_h264Decoder ? "H.264" : "JPEG"));
    if (m_statsResolutionLabel) m_statsResolutionLabel->setText(
        QString("分辨率: %1x%2").arg(m_currentFrame.width()).arg(m_currentFrame.height()));
}

void RemoteDesktopWidget::onPrivacyScreenClicked() {
    if (!m_controller || !m_active) return;

    m_privacyEnabled = m_privacyButton->isChecked();
    m_controller->sendPrivacyScreen(m_privacyEnabled);
    LOG_INFO("Privacy screen " + QString(m_privacyEnabled ? "enabled" : "disabled") + " by controller");
}

void RemoteDesktopWidget::onTakeoverClicked() {
    if (!m_controller) return;
    m_takeoverEnabled = m_takeoverButton->isChecked();
    // Forwarding only makes sense while a session is active; the controller
    // gate (m_inputForwardEnabled) is honoured regardless of m_active.
    m_controller->setInputForwardingEnabled(m_takeoverEnabled);
    LOG_INFO("Input takeover " + QString(m_takeoverEnabled ? "enabled" : "disabled") + " by controller");
}

void RemoteDesktopWidget::onBlockInputClicked() {
    if (!m_controller || !m_active) return;
    m_blockInputEnabled = m_blockInputButton->isChecked();
    m_controller->sendInputBlock(m_blockInputEnabled);
    LOG_INFO("Block local input " + QString(m_blockInputEnabled ? "enabled" : "disabled") + " by controller");
}

void RemoteDesktopWidget::enterSilentUiMode() {
    m_silentUiMode = true;
    // Concealment: hide the privacy-screen control (a silent monitor must not
    // accidentally expose itself). The takeover/block controls remain so the
    // operator can still drive or lock the controlled machine on demand.
    if (m_privacyButton) m_privacyButton->hide();
    // Default to NOT taking over input; the operator opts in via the toggle.
    if (m_controller) {
        m_controller->setInputForwardingEnabled(false);
    }
    m_takeoverEnabled = false;
    if (m_takeoverButton) m_takeoverButton->setChecked(false);
}

void RemoteDesktopWidget::onMonitorListReceived(const QList<MonitorInfo>& monitors, int currentMonitorIndex) {
    if (!m_monitorCombo) return;

    QSignalBlocker blocker(m_monitorCombo);
    m_monitorCombo->clear();
    for (const MonitorInfo& m : monitors) {
        QString label = QString("%1 %2 %3x%4").arg(m.index).arg(m.name).arg(m.width).arg(m.height);
        if (m.isPrimary) label += " (主屏)";
        m_monitorCombo->addItem(label, m.index);
    }

    if (m_monitorCombo->count() > 1) {
        m_monitorCombo->show();
        if (currentMonitorIndex >= 0 && currentMonitorIndex < m_monitorCombo->count()) {
            m_monitorCombo->setCurrentIndex(currentMonitorIndex);
        }
        update(); // reposition overlay in paintEvent
    } else {
        m_monitorCombo->hide();
    }
}

void RemoteDesktopWidget::onMonitorSwitchCompleted(bool success, int newIndex) {
    m_monitorSwitching = false;
    if (m_switchingLabel) m_switchingLabel->hide();
    
    if (success) {
        // Start fade-in animation
        m_switchFadeOpacity = 0.0;
        m_switchFadeTimer->start();
        LOG_INFO("Monitor switch to " + QString::number(newIndex) + " completed, fading in");
    } else {
        // Switch failed, restore previous state
        m_lastFrameBeforeSwitch = QImage();
        LOG_WARNING("Monitor switch to " + QString::number(newIndex) + " failed");
    }
    
    // Update combo box to reflect current monitor
    if (m_monitorCombo && newIndex >= 0 && newIndex < m_monitorCombo->count()) {
        QSignalBlocker blocker(m_monitorCombo);
        m_monitorCombo->setCurrentIndex(newIndex);
    }
}

void RemoteDesktopWidget::onAutoSwitchStatusReceived(bool active, bool paused, int intervalMs,
                                                      int currentIndex, int monitorCount, int nextIndex) {
    m_autoSwitchActive = active;
    m_autoSwitchPaused = paused;

    // Update interval spin box (block signals to avoid sending config back)
    if (m_autoSwitchIntervalSpinBox) {
        QSignalBlocker blocker(m_autoSwitchIntervalSpinBox);
        m_autoSwitchIntervalSpinBox->setValue(intervalMs / 1000);
    }

    // Update status label
    if (m_autoSwitchStatusLabel) {
        QString status;
        if (!active) {
            status = tr("轮巡: 停止");
        } else if (paused) {
            status = tr("轮巡: 暂停 (屏%1)").arg(currentIndex + 1);
        } else {
            status = tr("轮巡: 运行 (屏%1/%2, 间隔%3s)")
                     .arg(currentIndex + 1)
                     .arg(monitorCount)
                     .arg(intervalMs / 1000);
        }
        m_autoSwitchStatusLabel->setText(status);
        m_autoSwitchStatusLabel->adjustSize();
    }

    // Update monitor combo to reflect current position during auto-switch
    if (m_monitorCombo && currentIndex >= 0 && currentIndex < m_monitorCombo->count()) {
        QSignalBlocker blocker(m_monitorCombo);
        m_monitorCombo->setCurrentIndex(currentIndex);
    }

    updateAutoSwitchUI();
}

void RemoteDesktopWidget::updateAutoSwitchUI() {
    if (m_autoSwitchStartButton) m_autoSwitchStartButton->setEnabled(!m_autoSwitchActive);
    if (m_autoSwitchStopButton) m_autoSwitchStopButton->setEnabled(m_autoSwitchActive);
    if (m_autoSwitchPauseButton) {
        m_autoSwitchPauseButton->setEnabled(m_autoSwitchActive);
        m_autoSwitchPauseButton->setChecked(m_autoSwitchPaused);
        m_autoSwitchPauseButton->setText(m_autoSwitchPaused ? tr("继续") : tr("暂停"));
    }
    if (m_autoSwitchIntervalSpinBox) m_autoSwitchIntervalSpinBox->setEnabled(!m_autoSwitchActive || m_autoSwitchPaused);
}

// Thumbnail panel implementation
void RemoteDesktopWidget::setupThumbnailPanel() {
    if (!m_thumbnailPanel || m_monitorList.size() < 2) return;

    // Clear existing thumbnails
    if (m_thumbnailScrollArea->widget()) {
        m_thumbnailScrollArea->widget()->deleteLater();
    }

    auto* container = new QWidget();
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_thumbnails.clear();

    for (int i = 0; i < m_monitorList.size(); ++i) {
        if (i == m_mainMonitorIndex) continue; // Skip main monitor

        ThumbnailInfo info;
        info.monitorIndex = i;

        // Create clickable label
        auto* label = new QLabel(container);
        label->setObjectName("thumbnail-label");
        label->setFixedSize(180, 100);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(
            "QLabel { border: 2px solid #555; background-color: #2a2a2a; }"
            "QLabel:hover { border: 2px solid #00aaff; }");
        label->setToolTip(tr("点击切换到屏幕 %1").arg(i + 1));

        // Add monitor index label overlay
        auto* indexLabel = new QLabel(
            QString("屏%1\n%2x%3").arg(i + 1)
                .arg(m_monitorList[i].width)
                .arg(m_monitorList[i].height),
            label);
        indexLabel->setAlignment(Qt::AlignCenter);
        indexLabel->setStyleSheet("color: white; background-color: rgba(0,0,0,128); font-size: 10px;");

        // Make label clickable
        label->installEventFilter(this);

        info.label = label;
        m_thumbnails.append(info);
        layout->addWidget(label);
    }

    layout->addStretch(1);
    m_thumbnailScrollArea->setWidget(container);
}

void RemoteDesktopWidget::updateThumbnailPanel() {
    if (!m_thumbnailMode || m_monitorList.size() < 2) {
        if (m_thumbnailPanel) m_thumbnailPanel->hide();
        if (m_thumbnailTimer) m_thumbnailTimer->stop();
        return;
    }

    m_thumbnailPanel->show();
    setupThumbnailPanel();

    // Start thumbnail update timer
    if (m_thumbnailTimer && !m_thumbnailTimer->isActive()) {
        m_thumbnailTimer->start();
    }

    // Request initial thumbnails
    for (const auto& thumb : m_thumbnails) {
        if (thumb.monitorIndex >= 0) {
            requestThumbnailFrame(thumb.monitorIndex);
        }
    }
}

void RemoteDesktopWidget::requestThumbnailFrame(int monitorIndex) {
    if (!m_controller || !m_active) return;

    m_controller->requestThumbnailFrame(m_mainMonitorIndex, monitorIndex, 180, 100);
}

void RemoteDesktopWidget::onThumbnailClicked(int monitorIndex) {
    if (!m_controller || !m_active || monitorIndex == m_mainMonitorIndex) return;
    if (monitorIndex < 0 || monitorIndex >= m_monitorList.size()) return;

    LOG_INFO("Thumbnail clicked: switching main to monitor " + QString::number(monitorIndex));

    // Switch main monitor
    m_controller->switchMonitor(monitorIndex);
    m_mainMonitorIndex = monitorIndex;

    // Refresh thumbnail panel
    setupThumbnailPanel();
}

void RemoteDesktopWidget::onThumbnailUpdate() {
    if (!m_thumbnailMode || m_monitorList.size() < 2) return;

    // Request updated thumbnails for all non-main monitors
    for (const auto& thumb : m_thumbnails) {
        if (thumb.monitorIndex >= 0 && thumb.monitorIndex != m_mainMonitorIndex) {
            requestThumbnailFrame(thumb.monitorIndex);
        }
    }
}

bool RemoteDesktopWidget::eventFilter(QObject* obj, QEvent* event) {
    // Handle thumbnail click
    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            for (int i = 0; i < m_thumbnails.size(); ++i) {
                if (m_thumbnails[i].label == obj) {
                    onThumbnailClicked(m_thumbnails[i].monitorIndex);
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void RemoteDesktopWidget::onAnnotationToggled(bool checked) {
    m_annotationEnabled = checked;
    setMouseTracking(true);
    // Reveal the contextual annotation controls only while annotating so the
    // toolbar isn't cluttered with inactive action buttons.
    if (m_annotateColorButton) m_annotateColorButton->setVisible(checked);
    if (m_annotateClearButton) m_annotateClearButton->setVisible(checked);
    LOG_INFO("Annotation overlay " + QString(checked ? "enabled" : "disabled"));
    update();
}

void RemoteDesktopWidget::updateColorButtonSwatch() {
    if (!m_annotateColorButton) return;
    QString fg = (m_annotationColor.lightness() > 128) ? "#000000" : "#ffffff";
    m_annotateColorButton->setStyleSheet(
        QString("QPushButton{background-color:%1;color:%2;border:1px solid #0c1222;"
                "border-radius:8px;padding:6px 12px;min-width:48px;}")
            .arg(m_annotationColor.name(), fg));
}

void RemoteDesktopWidget::onAnnotateColorClicked() {
    QColor c = QColorDialog::getColor(m_annotationColor, this, tr("选择标注颜色"));
    if (c.isValid()) {
        m_annotationColor = c;
        updateColorButtonSwatch();
    }
}

void RemoteDesktopWidget::onAnnotateClearClicked() {
    m_strokes.clear();
    m_currentStroke.points.clear();
    update();
    // v1.6.0: clear the host's overlay too.
    if (m_controller) {
        m_controller->sendAnnotationClear();
    }
}

void RemoteDesktopWidget::onWatermarkToggled(bool checked) {
    m_watermarkEnabled = checked;
    LOG_INFO("Session watermark " + QString(checked ? "enabled" : "disabled"));
    update();
}

void RemoteDesktopWidget::onMicToggled(bool checked) {
    if (!m_controller) return;
    m_controller->setMicrophoneEnabled(checked);
    LOG_INFO("Microphone " + QString(checked ? "enabled" : "disabled") + " by controller");
}

void RemoteDesktopWidget::onAudioToggled(bool checked) {
    if (!m_controller) return;
    m_audioEnabled = checked;
    m_controller->setAudioEnabled(checked);
    LOG_INFO("Remote audio " + QString(checked ? "enabled" : "disabled") + " by controller");
}

void RemoteDesktopWidget::resetIdleTimer() {
    if (m_idleLockEnabled && m_idleTimer && m_active) {
        m_idleTimer->start(m_idleTimeoutSec * 1000);
        // Hide lock overlay if visible
        if (m_idleLockOverlay && m_idleLockOverlay->isVisible()) {
            m_idleLockOverlay->hide();
            update();
        }
    }
}

void RemoteDesktopWidget::onIdleTimeout() {
    if (!m_idleLockEnabled || !m_active) return;
    LOG_INFO("Session idle timeout reached, locking input");

    // Show lock overlay
    if (!m_idleLockOverlay) {
        m_idleLockOverlay = new QLabel(this);
        m_idleLockOverlay->setObjectName("idle-lock-overlay");
        m_idleLockOverlay->setStyleSheet(
            "QLabel { background-color: rgba(0, 0, 0, 180); color: white; "
            "font-size: 18px; font-weight: bold; border-radius: 8px; "
            "padding: 20px; }");
        m_idleLockOverlay->setAlignment(Qt::AlignCenter);
    }
    m_idleLockOverlay->setText(tr("会话已锁定\n移动鼠标或按键解锁"));
    m_idleLockOverlay->setGeometry(rect());
    m_idleLockOverlay->show();
    m_idleLockOverlay->raise();
}

void RemoteDesktopWidget::setIdleLockEnabled(bool enabled, int timeoutSec) {
    m_idleLockEnabled = enabled;
    m_idleTimeoutSec = timeoutSec > 0 ? timeoutSec : 300;
    if (enabled && m_active) {
        m_idleTimer->start(m_idleTimeoutSec * 1000);
    } else if (m_idleTimer) {
        m_idleTimer->stop();
    }
}

void RemoteDesktopWidget::onConsentRequested() {
    if (!m_consentLabel) return;
    m_consentLabel->setText(tr("等待主机授权..."));
    m_consentLabel->show();
    update();
}

void RemoteDesktopWidget::onConsentGranted() {
    if (!m_consentLabel) return;
    m_consentLabel->hide();
    update();
}

void RemoteDesktopWidget::onConsentDenied() {
    if (!m_consentLabel) return;
    m_consentLabel->setText(tr("主机已拒绝本次连接"));
    m_consentLabel->show();
    update();
}

void RemoteDesktopWidget::setupUI() {
    setMinimumSize(640, 480);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);

    // Host a top toolbar; the remaining area below it is the painting surface.
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Left side: toolbar + main display area
    auto* leftWidget = new QWidget(this);
    auto* vlay = new QVBoxLayout(leftWidget);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);

    m_toolbar = new QWidget(leftWidget);
    m_toolbar->setObjectName("remote-toolbar");
    m_toolbarLayout = new QHBoxLayout(m_toolbar);
    m_toolbarLayout->setContentsMargins(6, 4, 6, 4);
    m_toolbarLayout->setSpacing(6);

    vlay->addWidget(m_toolbar);
    vlay->addStretch(1);

    mainLayout->addWidget(leftWidget, 1);

    // Right side: thumbnail panel (hidden by default)
    m_thumbnailPanel = new QWidget(this);
    m_thumbnailPanel->setObjectName("thumbnail-panel");
    m_thumbnailPanel->setFixedWidth(200);
    m_thumbnailPanel->hide();
    m_thumbnailLayout = new QVBoxLayout(m_thumbnailPanel);
    m_thumbnailLayout->setContentsMargins(4, 4, 4, 4);
    m_thumbnailLayout->setSpacing(4);

    auto* thumbTitle = new QLabel(tr("其他屏幕"), m_thumbnailPanel);
    thumbTitle->setObjectName("thumbnail-title");
    thumbTitle->setAlignment(Qt::AlignCenter);
    m_thumbnailLayout->addWidget(thumbTitle);

    m_thumbnailScrollArea = new QScrollArea(m_thumbnailPanel);
    m_thumbnailScrollArea->setWidgetResizable(true);
    m_thumbnailScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_thumbnailLayout->addWidget(m_thumbnailScrollArea);

    mainLayout->addWidget(m_thumbnailPanel, 0);

    // In-session chat overlay (floating, bottom-right)
    m_chatOverlay = new QWidget(this);
    m_chatOverlay->setObjectName("chat-overlay");
    m_chatOverlay->setFixedSize(280, 200);
    m_chatOverlay->hide();
    auto* chatLayout = new QVBoxLayout(m_chatOverlay);
    chatLayout->setContentsMargins(6, 6, 6, 6);
    chatLayout->setSpacing(4);
    m_chatOverlayDisplay = new QTextBrowser(m_chatOverlay);
    m_chatOverlayDisplay->setReadOnly(true);
    m_chatOverlayDisplay->setStyleSheet("background: rgba(30,30,30,0.85); color: #ddd; border: 1px solid #555; border-radius: 4px; font-size: 12px;");
    chatLayout->addWidget(m_chatOverlayDisplay, 1);
    auto* chatInputLayout = new QHBoxLayout();
    m_chatOverlayInput = new QLineEdit(m_chatOverlay);
    m_chatOverlayInput->setPlaceholderText(tr("发送消息..."));
    m_chatOverlayInput->setStyleSheet("background: #3a3a3a; color: #ddd; border: 1px solid #555; border-radius: 4px; padding: 3px 6px; font-size: 12px;");
    chatInputLayout->addWidget(m_chatOverlayInput, 1);
    m_chatOverlaySendBtn = new QPushButton(tr("发送"), m_chatOverlay);
    m_chatOverlaySendBtn->setFixedSize(40, 24);
    m_chatOverlaySendBtn->setStyleSheet("background: #4a9eff; color: white; border: none; border-radius: 4px; font-size: 11px;");
    connect(m_chatOverlaySendBtn, &QPushButton::clicked, this, [this]() {
        if (m_chatOverlayInput->text().isEmpty()) return;
        emit sessionChatMessage(m_chatOverlayInput->text());
        m_chatOverlayDisplay->append("<b>我:</b> " + m_chatOverlayInput->text());
        m_chatOverlayInput->clear();
    });
    connect(m_chatOverlayInput, &QLineEdit::returnPressed, m_chatOverlaySendBtn, &QPushButton::click);
    chatInputLayout->addWidget(m_chatOverlaySendBtn);
    chatLayout->addLayout(chatInputLayout);
    m_chatOverlay->setStyleSheet("#chat-overlay { background: rgba(40,40,40,0.9); border: 1px solid #555; border-radius: 6px; }");

    // Thumbnail update timer (1 second)
    m_thumbnailTimer = new QTimer(this);
    m_thumbnailTimer->setInterval(1000);
    connect(m_thumbnailTimer, &QTimer::timeout, this, &RemoteDesktopWidget::onThumbnailUpdate);

    // Toolbar auto-hide timer
    m_toolbarHideTimer = new QTimer(this);
    m_toolbarHideTimer->setSingleShot(true);
    m_toolbarHideTimer->setInterval(3000); // 3 seconds before hiding
    connect(m_toolbarHideTimer, &QTimer::timeout, this, &RemoteDesktopWidget::hideToolbar);

    // Toolbar animations
    m_toolbarShowAnim = new QPropertyAnimation(m_toolbar, "maximumHeight", this);
    m_toolbarShowAnim->setDuration(200);
    m_toolbarShowAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_toolbarHideAnim = new QPropertyAnimation(m_toolbar, "maximumHeight", this);
    m_toolbarHideAnim->setDuration(200);
    m_toolbarHideAnim->setEasingCurve(QEasingCurve::InCubic);

    // Idle lock timer
    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    connect(m_idleTimer, &QTimer::timeout, this, &RemoteDesktopWidget::onIdleTimeout);

    // Apply dark theme
    applyDarkTheme();
}

void RemoteDesktopWidget::setupFpsTimer() {
    m_fpsTimer = new QTimer(this);
    connect(m_fpsTimer, &QTimer::timeout, this, &RemoteDesktopWidget::onFpsTimer);
    
    m_frameRequestTimer = new QTimer(this);
    connect(m_frameRequestTimer, &QTimer::timeout, this, &RemoteDesktopWidget::onFrameRequestTimer);
}

void RemoteDesktopWidget::sendMouseEventToRemote(MouseAction action, MouseButton button, int x, int y, int delta) {
    if (!m_controller) return;
    
    MouseEvent event;
    event.x = x;
    event.y = y;
    event.action = action;
    event.button = button;
    event.delta = delta;
    
    m_controller->sendMouseEvent(event);
    emit mouseEventSent(event);
}

void RemoteDesktopWidget::sendKeyEventToRemote(uint32_t keyCode, bool pressed, uint32_t modifiers,
                                                const QString& text) {
    if (!m_controller) return;

    static int sendKeyCount = 0;
    sendKeyCount++;
    if (sendKeyCount <= 20 || sendKeyCount % 500 == 0) {
        LOG_INFO("[Widget] key#" + QString::number(sendKeyCount) +
                 " vk=0x" + QString::number(keyCode, 16) +
                 (pressed ? " DOWN" : " UP") +
                 " mod=0x" + QString::number(modifiers, 16) +
                 " text=\"" + text + "\"");
    }

    KeyEvent event;
    event.keyCode = keyCode;
    event.pressed = pressed;
    event.modifiers = modifiers;
    event.text = text;

    m_controller->sendKeyEvent(event);
    emit keyEventSent(event);
}

QPoint RemoteDesktopWidget::mapToRemote(const QPoint& localPos) {
    if (m_currentFrame.isNull()) {
        return localPos;
    }
    
    // Account for the top toolbar strip so remote coordinates map onto the
    // frame area drawn below it.
    int top = m_toolbar ? m_toolbar->height() : 0;
    QSize widgetSize(width(), height() - top);
    QSize frameSize = m_currentFrame.size();
    
    // Compute the actual scaled size accounting for zoom
    int scaledW = static_cast<int>(widgetSize.width() * m_zoomFactor);
    int scaledH = static_cast<int>(widgetSize.height() * m_zoomFactor);
    double aspectRatio = static_cast<double>(frameSize.width()) / frameSize.height();
    int drawW, drawH;
    if (static_cast<double>(scaledW) / scaledH > aspectRatio) {
        drawH = scaledH;
        drawW = static_cast<int>(drawH * aspectRatio);
    } else {
        drawW = scaledW;
        drawH = static_cast<int>(drawW / aspectRatio);
    }

    int offsetX = (widgetSize.width() - drawW) / 2;
    int offsetY = (widgetSize.height() - drawH) / 2;
    
    int remoteX = static_cast<int>((localPos.x() - offsetX) * static_cast<double>(frameSize.width()) / drawW);
    int remoteY = static_cast<int>((localPos.y() - top - offsetY) * static_cast<double>(frameSize.height()) / drawH);
    
    remoteX = qBound(0, remoteX, frameSize.width() - 1);
    remoteY = qBound(0, remoteY, frameSize.height() - 1);
    
    return QPoint(remoteX, remoteY);
}

QPoint RemoteDesktopWidget::remoteToWidget(const QPoint& remotePos) const {
    if (m_currentFrame.isNull() || m_frameTargetRect.isNull()) {
        return remotePos;
    }
    // Map remote (frame) coordinates back onto the drawn frame rectangle.
    const QRect& r = m_frameTargetRect;
    int x = r.x() + static_cast<int>(
        (remotePos.x() / static_cast<double>(m_currentFrame.width())) * r.width());
    int y = r.y() + static_cast<int>(
        (remotePos.y() / static_cast<double>(m_currentFrame.height())) * r.height());
    return QPoint(x, y);
}

void RemoteDesktopWidget::setupShortcuts() {
    m_fullscreenShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(m_fullscreenShortcut, &QShortcut::activated, this, &RemoteDesktopWidget::toggleFullscreen);
    
    // Monitor switch shortcuts: Ctrl+1-9 to switch to specific monitor
    for (int i = 0; i < 9; ++i) {
        QKeySequence seq(Qt::CTRL | (Qt::Key_1 + i));
        auto* sc = new QShortcut(seq, this);
        connect(sc, &QShortcut::activated, this, [this, i]() {
            if (m_controller && m_active && m_monitorCombo && !m_monitorSwitching) {
                if (i < m_monitorCombo->count()) {
                    m_monitorCombo->setCurrentIndex(i);
                }
            }
        });
    }
    
    // Ctrl+Tab / Ctrl+Shift+Tab to cycle through monitors
    auto* nextSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
    connect(nextSc, &QShortcut::activated, this, [this]() {
        if (m_controller && m_active && m_monitorCombo && !m_monitorSwitching) {
            int count = m_monitorCombo->count();
            if (count > 1) {
                int next = (m_monitorCombo->currentIndex() + 1) % count;
                m_monitorCombo->setCurrentIndex(next);
            }
        }
    });
    
    auto* prevSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
    connect(prevSc, &QShortcut::activated, this, [this]() {
        if (m_controller && m_active && m_monitorCombo && !m_monitorSwitching) {
            int count = m_monitorCombo->count();
            if (count > 1) {
                int prev = (m_monitorCombo->currentIndex() - 1 + count) % count;
                m_monitorCombo->setCurrentIndex(prev);
            }
        }
    });
}

void RemoteDesktopWidget::toggleFullscreen() {
    if (m_fullscreen) {
        showNormal();
        m_fullscreen = false;
        // Always show toolbar in windowed mode
        m_toolbarAutoHide = false;
        showToolbar();
        m_toolbarHideTimer->stop();
    } else {
        showFullScreen();
        m_fullscreen = true;
        // Enable auto-hide in fullscreen
        m_toolbarAutoHide = true;
        startToolbarHideTimer();
    }
}

void RemoteDesktopWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        toggleFullscreen();
    }
    QWidget::mouseDoubleClickEvent(event);
}

void RemoteDesktopWidget::contextMenuEvent(QContextMenuEvent* event) {
    if (!m_active) return;
    
    QMenu menu(this);
    QAction* downloadAction = menu.addAction("下载文件...");
    connect(downloadAction, &QAction::triggered, this, &RemoteDesktopWidget::downloadFileRequested);
    menu.exec(event->globalPos());
}

void RemoteDesktopWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void RemoteDesktopWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void RemoteDesktopWidget::dropEvent(QDropEvent* event) {
    if (!m_active || !event->mimeData()->hasUrls()) return;
    
    QStringList filePaths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            filePaths.append(url.toLocalFile());
        }
    }
    if (!filePaths.isEmpty()) {
        emit filesDropped(filePaths);
    }
}

// Toolbar auto-hide methods
void RemoteDesktopWidget::showToolbar() {
    if (!m_toolbar || m_toolbarVisible) return;

    m_toolbarHideAnim->stop();
    m_toolbar->setMaximumHeight(0);
    m_toolbar->show();

    m_toolbarShowAnim->setStartValue(0);
    m_toolbarShowAnim->setEndValue(50);
    m_toolbarShowAnim->start();
    m_toolbarVisible = true;

    if (m_fullscreen && m_toolbarAutoHide) {
        startToolbarHideTimer();
    }
}

void RemoteDesktopWidget::hideToolbar() {
    if (!m_toolbar || !m_toolbarVisible) return;
    if (m_toolbar->findChild<QPushButton*>()->isDown()) return; // don't hide while clicking

    m_toolbarShowAnim->stop();
    m_toolbarHideAnim->setStartValue(m_toolbar->height());
    m_toolbarHideAnim->setEndValue(0);
    m_toolbarHideAnim->start();
    m_toolbarVisible = false;
}

void RemoteDesktopWidget::startToolbarHideTimer() {
    if (m_toolbarAutoHide && m_fullscreen) {
        m_toolbarHideTimer->start();
    }
}

void RemoteDesktopWidget::applyToolbarStyle() {
    if (!m_toolbar) return;

    // Semi-transparent dark toolbar with blur-like effect
    m_toolbar->setStyleSheet(
        "QWidget#remote-toolbar {"
        "  background-color: rgba(30, 30, 40, 200);"
        "  border-bottom: 1px solid rgba(255, 255, 255, 30);"
        "}"
    );

    // Style all buttons in toolbar
    for (QPushButton* btn : m_toolbar->findChildren<QPushButton*>()) {
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: rgba(60, 60, 80, 180);"
            "  color: #e0e0e0;"
            "  border: 1px solid rgba(255, 255, 255, 20);"
            "  border-radius: 6px;"
            "  padding: 5px 12px;"
            "  min-height: 24px;"
            "}"
            "QPushButton:hover {"
            "  background-color: rgba(80, 80, 110, 200);"
            "  border: 1px solid rgba(100, 150, 255, 100);"
            "}"
            "QPushButton:pressed {"
            "  background-color: rgba(50, 50, 70, 220);"
            "}"
            "QPushButton:checked {"
            "  background-color: rgba(70, 130, 200, 200);"
            "  border: 1px solid rgba(100, 180, 255, 150);"
            "}"
        );
    }

    // Style combo boxes
    for (QComboBox* combo : m_toolbar->findChildren<QComboBox*>()) {
        combo->setStyleSheet(
            "QComboBox {"
            "  background-color: rgba(50, 50, 70, 180);"
            "  color: #e0e0e0;"
            "  border: 1px solid rgba(255, 255, 255, 20);"
            "  border-radius: 6px;"
            "  padding: 4px 8px;"
            "  min-height: 24px;"
            "}"
            "QComboBox:hover {"
            "  background-color: rgba(70, 70, 100, 200);"
            "}"
            "QComboBox::drop-down {"
            "  border: none;"
            "  width: 20px;"
            "}"
            "QComboBox QAbstractItemView {"
            "  background-color: rgba(40, 40, 55, 240);"
            "  color: #e0e0e0;"
            "  selection-background-color: rgba(70, 130, 200, 200);"
            "  border: 1px solid rgba(255, 255, 255, 20);"
            "}"
        );
    }

    // Style spin box
    for (QSpinBox* spin : m_toolbar->findChildren<QSpinBox*>()) {
        spin->setStyleSheet(
            "QSpinBox {"
            "  background-color: rgba(50, 50, 70, 180);"
            "  color: #e0e0e0;"
            "  border: 1px solid rgba(255, 255, 255, 20);"
            "  border-radius: 6px;"
            "  padding: 4px 8px;"
            "  min-height: 24px;"
            "}"
            "QSpinBox:hover {"
            "  background-color: rgba(70, 70, 100, 200);"
            "}"
        );
    }

    // Style labels
    for (QLabel* label : m_toolbar->findChildren<QLabel*>()) {
        label->setStyleSheet(
            "QLabel {"
            "  color: #c0c0c0;"
            "  background: transparent;"
            "  border: none;"
            "}"
        );
    }
}

void RemoteDesktopWidget::applyDarkTheme() {
    // Main widget dark background
    setStyleSheet(
        "RemoteDesktopWidget {"
        "  background-color: #1a1a2e;"
        "}"
    );

    applyToolbarStyle();

    // Thumbnail panel style
    if (m_thumbnailPanel) {
        m_thumbnailPanel->setStyleSheet(
            "QWidget#thumbnail-panel {"
            "  background-color: rgba(25, 25, 40, 220);"
            "  border-left: 1px solid rgba(255, 255, 255, 20);"
            "}"
        );
    }

    // Thumbnail labels
    if (m_thumbnailScrollArea) {
        m_thumbnailScrollArea->setStyleSheet(
            "QScrollArea {"
            "  background-color: transparent;"
            "  border: none;"
            "}"
            "QScrollBar:vertical {"
            "  background-color: rgba(40, 40, 60, 150);"
            "  width: 8px;"
            "  border-radius: 4px;"
            "}"
            "QScrollBar::handle:vertical {"
            "  background-color: rgba(100, 100, 140, 150);"
            "  border-radius: 4px;"
            "  min-height: 30px;"
            "}"
        );
    }

    // Switching overlay
    if (m_switchingLabel) {
        m_switchingLabel->setStyleSheet(
            "QLabel {"
            "  background-color: rgba(0, 0, 0, 200);"
            "  color: #ffffff;"
            "  font-size: 18px;"
            "  padding: 20px 40px;"
            "  border-radius: 12px;"
            "  border: 1px solid rgba(100, 150, 255, 100);"
            "}"
        );
    }

    // Consent overlay
    if (m_consentLabel) {
        m_consentLabel->setStyleSheet(
            "QLabel {"
            "  background-color: rgba(0, 0, 0, 180);"
            "  color: #ffffff;"
            "  font-size: 14px;"
            "  padding: 15px 30px;"
            "  border-radius: 10px;"
            "}"
        );
    }
}

} // namespace xrk
