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
    
    QPainter painter(this);
    
    if (m_currentFrame.isNull()) {
        painter.fillRect(rect(), Qt::black);
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "等待连接...");
        return;
    }
    
    QImage scaled = m_currentFrame.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QRect targetRect = QRect((width() - scaled.width()) / 2, 
                             (height() - scaled.height()) / 2,
                             scaled.width(), scaled.height());
    m_frameTargetRect = targetRect;
    painter.drawImage(targetRect, scaled);
    
    painter.setPen(Qt::green);
    painter.drawText(10, 20, QString("FPS: %1").arg(m_currentFps));

    // Show decoded frame count and frame dimensions for diagnostics
    painter.setPen(Qt::cyan);
    painter.drawText(10, 40, QString("Frames: %1 | %2x%3")
        .arg(m_frameCount)
        .arg(m_currentFrame.width())
        .arg(m_currentFrame.height()));
    painter.drawText(10, 38, QString("Frames: %1").arg(m_frameCount));

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
        int wx = width() - w - 10;
        int wy = height() - h - 10;
        painter.fillRect(wx, wy, w, h, QColor(0, 0, 0, 140));
        painter.setPen(Qt::white);
        painter.drawText(wx + 6, wy + fm.ascent() + 3, wm);
    }

    if (m_qualityLabel) {
        m_qualityLabel->adjustSize();
        m_qualityLabel->move(width() - m_qualityLabel->width() - 8, 8);
        m_qualityLabel->raise();
    }

    if (m_privacyButton) {
        m_privacyButton->adjustSize();
        if (m_qualityLabel) {
            m_privacyButton->move(width() - m_qualityLabel->width() - m_privacyButton->width() - 16, 8);
        } else {
            m_privacyButton->move(width() - m_privacyButton->width() - 8, 8);
        }
        m_privacyButton->raise();
    }

    // Reposition overlay toolbar row (top-left): monitor combo, then annotation group.
    int toolbarX = 8;
    int toolbarY = 8;
    if (m_monitorCombo && m_monitorCombo->isVisible()) {
        m_monitorCombo->move(toolbarX, toolbarY);
        m_monitorCombo->raise();
        toolbarX += m_monitorCombo->width() + 6;
    }
    auto placeOverlay = [&](QWidget* w) {
        if (!w) return;
        w->adjustSize();
        w->move(toolbarX, toolbarY);
        w->raise();
        toolbarX += w->width() + 6;
    };
    placeOverlay(m_annotateButton);
    placeOverlay(m_annotateColorButton);
    placeOverlay(m_annotateClearButton);
    placeOverlay(m_watermarkButton);
    placeOverlay(m_micButton);

    if (m_consentLabel && m_consentLabel->isVisible()) {
        m_consentLabel->adjustSize();
        m_consentLabel->move((width() - m_consentLabel->width()) / 2,
                             (height() - m_consentLabel->height()) / 2);
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
    sendKeyEventToRemote(event->nativeVirtualKey(), true, event->modifiers());
}

void RemoteDesktopWidget::keyReleaseEvent(QKeyEvent* event) {
    if (!m_active) return;
    sendKeyEventToRemote(event->nativeVirtualKey(), false, event->modifiers());
}

void RemoteDesktopWidget::onScreenFrameReceived(const ScreenFrame& frame) {
    static int receivedCount = 0;
    static int decodeOkCount = 0;
    static int decodeFailCount = 0;
    receivedCount++;

    QImage image;
    if (frame.format == FrameFormat::JPEG) {
        image.loadFromData(frame.data, "JPEG");
    } else if (frame.format == FrameFormat::H264) {
        if (!m_h264Decoder) {
            m_h264Decoder = std::make_unique<VideoDecoder>();
            m_h264Decoder->initialize();
        }
        if (m_h264Decoder->isInitialized()) {
            image = m_h264Decoder->decode(frame.data);
        } else {
            if (decodeFailCount < 5) {
                LOG_WARNING("[Widget] H264 decoder not initialized");
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
                        " first4=0x" + frame.data.left(4).toHex());
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

void RemoteDesktopWidget::onAnnotateColorClicked() {
    QColor c = QColorDialog::getColor(m_annotationColor, this, tr("选择标注颜色"));
    if (c.isValid()) {
        m_annotationColor = c;
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

void RemoteDesktopWidget::sendKeyEventToRemote(uint32_t keyCode, bool pressed, uint32_t modifiers) {
    if (!m_controller) return;

    static int sendKeyCount = 0;
    sendKeyCount++;
    if (sendKeyCount <= 20 || sendKeyCount % 500 == 0) {
        LOG_INFO("[Widget] key#" + QString::number(sendKeyCount) +
                 " vk=0x" + QString::number(keyCode, 16) +
                 (pressed ? " DOWN" : " UP") +
                 " mod=0x" + QString::number(modifiers, 16));
    }

    KeyEvent event;
    event.keyCode = keyCode;
    event.pressed = pressed;
    event.modifiers = modifiers;
    
    m_controller->sendKeyEvent(event);
    emit keyEventSent(event);
}

QPoint RemoteDesktopWidget::mapToRemote(const QPoint& localPos) {
    if (m_currentFrame.isNull()) {
        return localPos;
    }
    
    QSize widgetSize = size();
    QSize frameSize = m_currentFrame.size();
    
    double scaleX = static_cast<double>(frameSize.width()) / widgetSize.width();
    double scaleY = static_cast<double>(frameSize.height()) / widgetSize.height();
    double scale = qMax(scaleX, scaleY);
    
    QSize scaledSize(frameSize.width() / scale, frameSize.height() / scale);
    int offsetX = (widgetSize.width() - scaledSize.width()) / 2;
    int offsetY = (widgetSize.height() - scaledSize.height()) / 2;
    
    int remoteX = static_cast<int>((localPos.x() - offsetX) * scale);
    int remoteY = static_cast<int>((localPos.y() - offsetY) * scale);
    
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
    
    m_disconnectShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(m_disconnectShortcut, &QShortcut::activated, this, &RemoteDesktopWidget::stopRemote);
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
