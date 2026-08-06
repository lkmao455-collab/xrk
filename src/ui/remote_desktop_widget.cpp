#include "remote_desktop_widget.h"
#include "app/remote_controller.h"
#include "hw/video_decoder.h"
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
        connect(m_controller, &RemoteController::qualityInfoReceived,
                this, &RemoteDesktopWidget::onQualityInfoReceived);
        connect(m_controller, &RemoteController::latencyUpdated,
                this, &RemoteDesktopWidget::onLatencyUpdated);
    }

    m_qualityLabel = new QLabel(this);
    m_qualityLabel->setObjectName("quality-overlay");
    m_qualityLabel->setText("\u753b\u8d28: \u81ea\u52a8");
    m_qualityLabel->adjustSize();

    m_privacyButton = new QPushButton(tr("隐私屏"), this);
    m_privacyButton->setCheckable(true);
    m_privacyButton->setObjectName("privacy-button");
    m_privacyButton->adjustSize();
    connect(m_privacyButton, &QPushButton::clicked, this, &RemoteDesktopWidget::onPrivacyScreenClicked);

    m_monitorCombo = new QComboBox(this);
    m_monitorCombo->setObjectName("monitor-combo");
    m_monitorCombo->hide();
    m_monitorCombo->setMinimumWidth(160);
    connect(m_monitorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (m_controller && m_active) {
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
        connect(m_controller, &RemoteController::consentRequested,
                this, &RemoteDesktopWidget::onConsentRequested);
        connect(m_controller, &RemoteController::consentGranted,
                this, &RemoteDesktopWidget::onConsentGranted);
        connect(m_controller, &RemoteController::consentDenied,
                this, &RemoteDesktopWidget::onConsentDenied);
    }

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

    m_annotateClearButton = new QPushButton(tr("清空"), this);
    m_annotateClearButton->setObjectName("annotate-clear-button");
    m_annotateClearButton->adjustSize();
    connect(m_annotateClearButton, &QPushButton::clicked,
            this, &RemoteDesktopWidget::onAnnotateClearClicked);

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
        m_toolbarLayout->addWidget(m_annotateButton);
        m_toolbarLayout->addWidget(m_annotateColorButton);
        m_toolbarLayout->addWidget(m_annotateClearButton);
        m_toolbarLayout->addWidget(m_watermarkButton);
        m_toolbarLayout->addWidget(m_micButton);
        m_toolbarLayout->addWidget(m_privacyButton);
        m_toolbarLayout->addStretch(1);
    }

    // Toolbar buttons must NOT take keyboard focus, otherwise clicking one
    // (e.g. 隐私屏) steals focus from this widget and remote keystrokes stop
    // being delivered. The remote desktop keeps focus so keyboard control of
    // the host keeps working after toggling any toolbar action.
    for (QPushButton* b : {m_annotateButton, m_annotateColorButton,
                            m_annotateClearButton, m_watermarkButton,
                            m_micButton, m_privacyButton}) {
        if (b) b->setFocusPolicy(Qt::NoFocus);
    }
    // The gear selector must also not steal keyboard focus from the remote
    // desktop, otherwise remote keystrokes stop after changing quality.
    if (m_qualityCombo) m_qualityCombo->setFocusPolicy(Qt::NoFocus);
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
    if (m_monitorCombo) m_monitorCombo->hide();
    // Clear annotation state so a new session starts clean.
    m_strokes.clear();
    m_currentStroke.clear();
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

    QImage scaled = m_currentFrame.scaled(display.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QRect targetRect(display.x() + (display.width() - scaled.width()) / 2,
                     display.y() + (display.height() - scaled.height()) / 2,
                     scaled.width(), scaled.height());
    m_frameTargetRect = targetRect;
    painter.drawImage(targetRect, scaled);

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
    if (!m_strokes.isEmpty() || !m_currentStroke.isEmpty()) {
        QPen pen(m_annotationColor, m_annotationWidth);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        auto drawStroke = [&](const QVector<QPoint>& stroke) {
            if (stroke.size() < 2) {
                if (stroke.size() == 1) {
                    QPoint p = remoteToWidget(stroke.first());
                    painter.drawPoint(p);
                }
                return;
            }
            for (int i = 1; i < stroke.size(); ++i) {
                painter.drawLine(remoteToWidget(stroke[i - 1]), remoteToWidget(stroke[i]));
            }
        };
        for (const QVector<QPoint>& s : m_strokes) drawStroke(s);
        drawStroke(m_currentStroke);
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

    if (m_consentLabel && m_consentLabel->isVisible()) {
        m_consentLabel->adjustSize();
        m_consentLabel->move(display.center().x() - m_consentLabel->width() / 2,
                             display.center().y() - m_consentLabel->height() / 2);
        m_consentLabel->raise();
    }
}

void RemoteDesktopWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!m_active) return;

    // Phase 4: while annotating, capture the stroke locally instead of sending input.
    if (m_annotationEnabled && !m_currentFrame.isNull() && !m_currentStroke.isEmpty()) {
        m_currentStroke.append(mapToRemote(event->pos()));
        update();
        return;
    }

    QPoint remotePos = mapToRemote(event->pos());
    sendMouseEventToRemote(MouseAction::MOVE, MouseButton::LEFT, remotePos.x(), remotePos.y());
}

void RemoteDesktopWidget::mousePressEvent(QMouseEvent* event) {
    if (!m_active) return;

    // Phase 4: begin a local annotation stroke.
    if (m_annotationEnabled && !m_currentFrame.isNull()) {
        m_currentStroke.clear();
        m_currentStroke.append(mapToRemote(event->pos()));
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
    if (m_annotationEnabled && !m_currentStroke.isEmpty()) {
        if (m_currentStroke.size() > 1) {
            m_strokes.append(m_currentStroke);
        }
        m_currentStroke.clear();
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
    sendMouseEventToRemote(MouseAction::RELEASE, button, remotePos.x(), remotePos.y());
}

void RemoteDesktopWidget::wheelEvent(QWheelEvent* event) {
    if (!m_active) return;
    
    int delta = event->angleDelta().y();
    QPoint pos = mapToRemote(event->position().toPoint());
    sendMouseEventToRemote(MouseAction::SCROLL, MouseButton::LEFT, pos.x(), pos.y(), delta);
}

void RemoteDesktopWidget::keyPressEvent(QKeyEvent* event) {
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
}

void RemoteDesktopWidget::onPrivacyScreenClicked() {
    if (!m_controller || !m_active) return;

    m_privacyEnabled = m_privacyButton->isChecked();
    m_controller->sendPrivacyScreen(m_privacyEnabled);
    LOG_INFO("Privacy screen " + QString(m_privacyEnabled ? "enabled" : "disabled") + " by controller");
}

void RemoteDesktopWidget::onMonitorListReceived(const QList<MonitorInfo>& monitors) {
    if (!m_monitorCombo) return;

    QSignalBlocker blocker(m_monitorCombo);
    m_monitorCombo->clear();
    for (const MonitorInfo& m : monitors) {
        QString label = QString("%1 %2").arg(m.index).arg(m.name);
        if (m.isPrimary) label += " (主屏)";
        m_monitorCombo->addItem(label, m.index);
    }

    if (m_monitorCombo->count() > 1) {
        m_monitorCombo->show();
        update(); // reposition overlay in paintEvent
    } else {
        m_monitorCombo->hide();
    }
}

void RemoteDesktopWidget::onAnnotationToggled(bool checked) {
    m_annotationEnabled = checked;
    setMouseTracking(true);
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
    m_currentStroke.clear();
    update();
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
    auto* vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);

    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName("remote-toolbar");
    m_toolbarLayout = new QHBoxLayout(m_toolbar);
    m_toolbarLayout->setContentsMargins(6, 4, 6, 4);
    m_toolbarLayout->setSpacing(6);

    vlay->addWidget(m_toolbar);
    vlay->addStretch(1);
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
    
    double scaleX = static_cast<double>(frameSize.width()) / widgetSize.width();
    double scaleY = static_cast<double>(frameSize.height()) / widgetSize.height();
    double scale = qMax(scaleX, scaleY);
    
    QSize scaledSize(frameSize.width() / scale, frameSize.height() / scale);
    int offsetX = (widgetSize.width() - scaledSize.width()) / 2;
    int offsetY = (widgetSize.height() - scaledSize.height()) / 2;
    
    int remoteX = static_cast<int>((localPos.x() - offsetX) * scale);
    int remoteY = static_cast<int>((localPos.y() - top - offsetY) * scale);
    
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
}

void RemoteDesktopWidget::toggleFullscreen() {
    if (m_fullscreen) {
        showNormal();
        m_fullscreen = false;
    } else {
        showFullScreen();
        m_fullscreen = true;
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

} // namespace xrk
