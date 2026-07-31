#include "host.h"
#include "core/protocol_manager.h"
#include "core/message_codec.h"
#include "core/encryption.h"
#include "hw/screen_capture.h"
#include "hw/camera_capture.h"
#include "hw/audio_capture.h"
#include "hw/audio_player.h"
#include "hw/input_control.h"
#include "hw/video_encoder.h"
#include "remote_terminal.h"
#include "screen_recorder.h"
#include "privacy_screen.h"
#include "system_info_collector.h"
#include "core/logger.h"
#include <QTcpSocket>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QBuffer>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDir>
#include <QFileInfo>
#include <QApplication>
#include <QClipboard>
#include <QDataStream>

// Avoid pulling in <windows.h> here: it defines MOUSE_EVENT / KEY_EVENT macros
// that collide with the MessageType enum cases below. Declare the one API we
// need (LockWorkStation, from user32.dll) directly.
extern "C" {
    __declspec(dllimport) int __stdcall LockWorkStation(void);
}

namespace xrk {

// ==================== CaptureWorker ====================

CaptureWorker::CaptureWorker(ScreenCapture* capture, CameraCapture* camera, FrameQueue<QImage>* queue, int fps)
    : QObject(nullptr), m_capture(capture), m_camera(camera), m_queue(queue), m_timer(nullptr), m_fps(fps), m_running(false) {
}

void CaptureWorker::setUseCamera(bool useCamera) {
    m_useCamera = useCamera;
}

void CaptureWorker::setFps(int fps) {
    m_fps = qBound(1, fps, 120);
    // This runs on the capture thread (invoked via BlockingQueuedConnection),
    // so restarting the timer here is always done on the timer's own thread.
    if (m_timer) {
        m_timer->stop();
        m_timer->start(1000 / m_fps);
    }
}

CaptureWorker::~CaptureWorker() {
    stop();
}

void CaptureWorker::start() {
    if (m_running) return;
    m_running = true;
    // Initialize screen capture on the capture thread so DXGI/GDI resources
    // are created on the same thread that will call captureFrame().
    if (m_capture && !m_capture->isInitialized()) {
        if (!m_capture->initialize()) {
            emit error("Screen capture init failed on capture thread");
        }
    }
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &CaptureWorker::onCaptureTimer);
    m_timer->start(1000 / m_fps);
}

void CaptureWorker::stop() {
    m_running = false;
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
}

void CaptureWorker::onCaptureTimer() {
    QImage frame;
    if (m_useCamera && m_camera && m_camera->isInitialized()) {
        frame = m_camera->captureFrame();
    } else if (m_capture && m_capture->isInitialized()) {
        frame = m_capture->captureFrame();
    }
    if (!frame.isNull()) {
        // Non-blocking enqueue: if queue is full, drop this frame instead of
        // blocking the timer thread (which would stall all subsequent captures).
        if (!m_queue->enqueueNonBlocking(frame)) {
            static int droppedCount = 0;
            if (++droppedCount % 300 == 1) {
                LOG_WARNING("CaptureWorker: dropped " + QString::number(droppedCount) +
                            " frames (encode queue full)");
            }
        }
    } else {
        static int nullCount = 0;
        if (++nullCount % 300 == 1) {
            LOG_WARNING("CaptureWorker: " + QString::number(nullCount) + " null frames so far");
        }
    }
}

// ==================== EncodeWorker ====================

EncodeWorker::EncodeWorker(FrameQueue<QImage>* inputQueue, FrameQueue<QByteArray>* outputQueue)
    : QObject(nullptr), m_inputQueue(inputQueue), m_outputQueue(outputQueue), m_encoder(nullptr), m_encryption(nullptr), m_thread(nullptr), m_running(false) {
}

EncodeWorker::~EncodeWorker() {
    stop();
}

void EncodeWorker::setEncoder(std::unique_ptr<VideoEncoder> encoder) {
    m_encoder = std::move(encoder);
}

void EncodeWorker::setEncoderAsync(std::unique_ptr<VideoEncoder> encoder) {
    m_pendingEncoder = std::move(encoder);
    m_encoderSwapPending.store(true);
}

VideoEncoder* EncodeWorker::encoder() const {
    return m_encoder.get();
}

void EncodeWorker::setEncryptionKey(const QByteArray& key, const QByteArray& iv) {
    m_encryption = std::make_unique<Encryption>();
    m_encryption->setKey(key, iv);
}

void EncodeWorker::start() {
    if (m_running) return;
    m_running = true;
    m_thread = QThread::create([this]() { processFrames(); });
    m_thread->start();
}

void EncodeWorker::stop() {
    m_running = false;
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

void EncodeWorker::processFrames() {
    int framesEncoded = 0;
    int encodeFailures = 0;
    int outputDrops = 0;
    qint64 totalEncodeUs = 0;
    qint64 maxEncodeUs = 0;

    while (m_running) {
        // Check for deferred encoder swap (safe: only done on this thread)
        if (m_encoderSwapPending.exchange(false)) {
            m_encoder = std::move(m_pendingEncoder);
            LOG_INFO("Encoder swapped on encode thread");
        }

        QImage rawFrame = m_inputQueue->dequeue(50);
        if (rawFrame.isNull()) {
            continue;
        }

        // At high FPS, drain stale frames from the queue to stay current
        while (m_inputQueue->size() > 2) {
            QImage stale = m_inputQueue->dequeue(1);
            if (stale.isNull()) break;
            rawFrame = stale;
        }

        QByteArray encodedData;
        FrameFormat format = FrameFormat::JPEG;

        qint64 encodeStart = QDateTime::currentMSecsSinceEpoch();

        if (m_encoder && m_encoder->isInitialized()) {
            encodedData = m_encoder->encode(rawFrame);
            format = (m_encoder->type() == EncoderType::H264) ? FrameFormat::H264 : FrameFormat::JPEG;
        } else {
            QBuffer buffer(&encodedData);
            buffer.open(QIODevice::WriteOnly);
            rawFrame.save(&buffer, "JPEG", 70);
            buffer.close();
        }

        qint64 encodeEnd = QDateTime::currentMSecsSinceEpoch();
        qint64 encodeUs = (encodeEnd - encodeStart) * 1000;
        totalEncodeUs += encodeUs;
        if (encodeUs > maxEncodeUs) maxEncodeUs = encodeUs;

        if (encodedData.isEmpty()) {
            ++encodeFailures;
            if (encodeFailures % 300 == 1) {
                LOG_WARNING("EncodeWorker: " + QString::number(encodeFailures) + " encode failures so far");
            }
            continue;
        }

        ++framesEncoded;
        if (framesEncoded % 300 == 1) {
            qint64 avgUs = framesEncoded > 0 ? totalEncodeUs / framesEncoded : 0;
            LOG_INFO("EncodeWorker: " + QString::number(framesEncoded) +
                     " frames, avg=" + QString::number(avgUs / 1000.0, 'f', 1) + "ms" +
                     " max=" + QString::number(maxEncodeUs / 1000.0, 'f', 1) + "ms" +
                     " inQ=" + QString::number(m_inputQueue->size()) +
                     " outQ=" + QString::number(m_outputQueue->size()));
        }

        ScreenFrame screenFrame;
        screenFrame.data = encodedData;
        screenFrame.width = rawFrame.width();
        screenFrame.height = rawFrame.height();
        screenFrame.format = format;
        screenFrame.timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());

        QByteArray payload = ProtocolManager::encodeScreenFrame(screenFrame);

        if (m_encryption && m_encryption->isInitialized()) {
            QByteArray encrypted = m_encryption->encrypt(payload);
            if (!encrypted.isEmpty()) {
                payload = encrypted;
            }
        }

        QByteArray message = ProtocolManager::encode(MessageType::SCREEN_FRAME, payload);

        // Non-blocking enqueue: drop frame if output queue is full
        // (network can't keep up with encode rate)
        if (!m_outputQueue->enqueueNonBlocking(message)) {
            ++outputDrops;
            if (outputDrops % 300 == 1) {
                LOG_WARNING("EncodeWorker: output queue full, dropped " +
                            QString::number(outputDrops) + " frames");
            }
        }
    }
}

// ==================== NetworkWorker ====================

NetworkWorker::NetworkWorker(FrameQueue<QByteArray>* queue)
    : QObject(nullptr), m_queue(queue) {
}

NetworkWorker::~NetworkWorker() {
    stop();
}

void NetworkWorker::start() {
    if (m_running) return;
    m_running = true;
    m_drainTimer = new QTimer(this);
    connect(m_drainTimer, &QTimer::timeout, this, &NetworkWorker::drainQueue);
    m_drainTimer->start(16); // ~60fps drain rate
}

void NetworkWorker::stop() {
    m_running = false;
    if (m_drainTimer) {
        m_drainTimer->stop();
        delete m_drainTimer;
        m_drainTimer = nullptr;
    }
}

void NetworkWorker::setClients(const QHash<QString, QTcpSocket*>& clients) {
    m_clients = clients;
}

void NetworkWorker::drainQueue() {
    // Drain all pending encoded frames, batch them into a single buffer
    // per client, and write once to minimize syscalls.
    static int framesSent = 0;
    static qint64 totalBytes = 0;
    static int drainCycles = 0;
    static int backpressureDrops = 0;

    QByteArray batch;
    int frameCount = 0;
    while (true) {
        QByteArray message = m_queue->dequeue(0);
        if (message.isEmpty()) break;
        batch.append(message);
        ++frameCount;
    }

    if (batch.isEmpty()) return;

    qint64 bytesThisCycle = 0;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        QTcpSocket* socket = it.value();
        if (!socket || socket->state() != QAbstractSocket::ConnectedState) continue;

        if (socket->bytesToWrite() > 256 * 1024) {
            ++backpressureDrops;
            if (backpressureDrops % 300 == 1) {
                LOG_WARNING("NetworkWorker: backpressure drop #" +
                            QString::number(backpressureDrops) +
                            " (buffer=" + QString::number(socket->bytesToWrite() / 1024) + "KB)");
            }
            continue;
        }

        qint64 written = socket->write(batch);
        bytesThisCycle += written;
        socket->flush();
    }

    framesSent += frameCount;
    totalBytes += bytesThisCycle;
    emit frameSent(static_cast<int>(bytesThisCycle));

    ++drainCycles;
    if (drainCycles % 300 == 0) {
        LOG_INFO("NetworkWorker: " + QString::number(framesSent) + " frames sent, " +
                 QString::number(totalBytes / 1024) + " KB, clients=" + QString::number(m_clients.size()));
    }
}

// ==================== Host ====================

Host::Host(QObject* parent) : QObject(parent) {
}

Host::~Host() {
    if (m_controllerAudioPlayer) {
        m_controllerAudioPlayer->shutdown();
        delete m_controllerAudioPlayer;
        m_controllerAudioPlayer = nullptr;
    }
    stop();
}

bool Host::start(uint16_t port) {
    if (m_running) {
        return true;
    }

    m_port = port;

    m_tcpServer = new QTcpServer(this);
    connect(m_tcpServer, &QTcpServer::newConnection, this, &Host::onNewConnection);

    if (!m_tcpServer->listen(QHostAddress::Any, port)) {
        LOG_ERROR("Host: Failed to start TCP server: " + m_tcpServer->errorString());
        return false;
    }

    m_udpSocket = new QUdpSocket(this);
    if (!m_udpSocket->bind(QHostAddress::Any, UDP_BROADCAST_PORT + 1)) {
        LOG_WARNING("Host: Failed to bind UDP socket for discovery");
    }
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &Host::onDiscoveryRequest);

    // Generate random 9-digit access code
    m_accessCode = QString::number(QRandomGenerator::global()->bounded(100000000, 999999999));
    LOG_INFO("Host: Access code: " + m_accessCode);

    m_screenCapture = new ScreenCapture(this);
    // NOTE: ScreenCapture::initialize() is deferred to CaptureWorker::start()
    // so DXGI/GDI resources are created on the capture thread, not the main
    // thread.  DXGI Desktop Duplication requires that AcquireNextFrame and
    // ReleaseFrame are called from the thread that created the duplication
    // interface.

    m_inputControl = new InputControl(this);
    m_inputControl->initialize();

    m_cameraCapture = new CameraCapture(this);
    if (!m_cameraCapture->initialize()) {
        LOG_WARNING("Host: Camera init failed, screen-only mode");
    }

    m_audioCapture = new AudioCapture(this);
    if (m_audioEnabled && !m_audioCapture->initialize()) {
        LOG_WARNING("Host: Audio capture init failed, no audio redirection");
    }
    connect(m_audioCapture, &AudioCapture::audioDataCaptured,
            this, &Host::onAudioDataCaptured);

    m_encryption = std::make_unique<Encryption>();
    m_encryption->generateKey();

    m_rawFrameQueue = new FrameQueue<QImage>(8);
    m_encodedFrameQueue = new FrameQueue<QByteArray>(5);

    m_captureThread = new QThread(this);
    m_captureWorker = new CaptureWorker(m_screenCapture, m_cameraCapture, m_rawFrameQueue, m_captureFps);
    m_captureWorker->moveToThread(m_captureThread);
    connect(m_captureThread, &QThread::started, m_captureWorker, &CaptureWorker::start);
    connect(m_captureWorker, &CaptureWorker::error, this, &Host::onCaptureWorkerError);
    m_captureThread->start();

    m_encodeThread = new QThread(this);
    m_encodeWorker = new EncodeWorker(m_rawFrameQueue, m_encodedFrameQueue);
    auto encoder = VideoEncoder::create(EncoderType::H264);
    if (!encoder || !encoder->initialize(1920, 1080, m_captureFps) ||
        encoder->encode(QImage(64, 64, QImage::Format_RGB32)).isEmpty()) {
        // initialize() can succeed yet encode() return nothing (e.g. the Media
        // Foundation H264 encoder is selected but cannot process input, or
        // libx264 is not available). Probe-encode and fall back to JPEG so the
        // remote desktop still renders instead of staying black.
        if (encoder) {
            LOG_WARNING("H264 encoder produced no output, falling back to JPEG");
        }
        encoder = VideoEncoder::create(EncoderType::JPEG);
        encoder->initialize(1920, 1080, m_captureFps);
        LOG_INFO("Using JPEG encoder for screen capture");
    }
    m_encodeWorker->setEncoder(std::move(encoder));
    if (m_encodeWorker->encoder()) {
        m_encodeWorker->encoder()->setJpegQuality(m_jpegQuality);
        m_encodeWorker->encoder()->setTrueColor(m_trueColor);
    }
    if (m_encryption && m_encryption->isInitialized()) {
        m_encodeWorker->setEncryptionKey(m_encryption->key(), m_encryption->iv());
    }
    m_encodeWorker->moveToThread(m_encodeThread);
    connect(m_encodeThread, &QThread::started, m_encodeWorker, &EncodeWorker::start);
    connect(m_encodeWorker, &EncodeWorker::error, this, &Host::onEncodeWorkerError);
    m_encodeThread->start();

    m_networkWorker = new NetworkWorker(m_encodedFrameQueue);
    connect(m_networkWorker, &NetworkWorker::error, this, &Host::onNetworkWorkerError);
    connect(m_networkWorker, &NetworkWorker::frameSent, this, [this](int bytes) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        m_bandwidthHistory.append({now, bytes});
        while (!m_bandwidthHistory.isEmpty() && (now - m_bandwidthHistory.first().timestamp) > 3000) {
            m_bandwidthHistory.removeFirst();
        }
    });
    m_networkWorker->start();

    m_qualityTimer = new QTimer(this);
    connect(m_qualityTimer, &QTimer::timeout, this, &Host::onQualityTimer);
    m_qualityTimer->start(2000);

    m_discoveryTimer = new QTimer(this);
    connect(m_discoveryTimer, &QTimer::timeout, this, &Host::broadcastDiscoveryResponse);
    m_discoveryTimer->start(3000);

    broadcastDiscoveryResponse();

    if (!m_auditLogger) {
        m_auditLogger = new AuditLogger(QString(), this);
    }

    m_running = true;
    LOG_INFO("Host started on port " + QString::number(port) + " (multi-threaded)" + (m_password.isEmpty() ? " (no auth)" : " (auth required)"));
    return true;
}

void Host::stop() {
    if (!m_running) {
        return;
    }

    m_running = false;

    stopRecording();

    if (m_privacyScreen) {
        m_privacyScreen->hide();
        delete m_privacyScreen;
        m_privacyScreen = nullptr;
    }

    if (m_discoveryTimer) {
        m_discoveryTimer->stop();
    }

    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value().socket) {
            it.value().socket->close();
        }
    }
    m_clients.clear();

    if (m_tcpServer) {
        m_tcpServer->close();
    }
    if (m_udpSocket) {
        m_udpSocket->close();
    }

    // Stop each worker from its OWN thread so any timers it owns (e.g. the
    // capture worker's QTimer) are torn down on the correct thread. Stopping
    // them from the calling thread triggers "Timers cannot be stopped from
    // another thread" and, on destruction, a segfault.
    auto stopWorker = [](QObject* worker, QThread* thread) {
        if (!worker || !thread) return;
        // Run the worker's stop() on its OWN thread. BlockingQueuedConnection
        // guarantees stop() (and any timer teardown it does) has completed on
        // the worker thread before we return, so the object can then be deleted
        // from this thread without tripping "Timers cannot be stopped from
        // another thread" / a segfault.
        QMetaObject::invokeMethod(worker, "stop", Qt::BlockingQueuedConnection);
        thread->quit();
        thread->wait();
        delete worker;
    };

    stopWorker(m_captureWorker, m_captureThread); m_captureWorker = nullptr;
    stopWorker(m_encodeWorker, m_encodeThread);   m_encodeWorker = nullptr;

    // NetworkWorker runs on the main thread (no dedicated thread), so just
    // stop and delete it directly.
    if (m_networkWorker) {
        m_networkWorker->stop();
        delete m_networkWorker;
        m_networkWorker = nullptr;
    }

    if (m_screenCapture) {
        m_screenCapture->shutdown();
    }
    if (m_inputControl) {
        m_inputControl->shutdown();
    }

    LOG_INFO("Host stopped");
}

bool Host::isRunning() const {
    return m_running;
}

void Host::setCaptureFps(int fps) {
    m_captureFps = qBound(1, fps, 60);
    // Update the running capture worker's frame rate on its OWN thread. The
    // previous implementation deleted and re-created the worker (and restarted
    // the capture thread) from the caller's thread, which caused a
    // cross-thread QObject deletion / data race and crashed the app when
    // "启动服务" was followed immediately by applying saved FPS settings.
    if (m_captureWorker) {
        QMetaObject::invokeMethod(m_captureWorker, "setFps",
                                   Qt::BlockingQueuedConnection,
                                   Q_ARG(int, m_captureFps));
    }
}

int Host::captureFps() const {
    return m_captureFps;
}

void Host::setPassword(const QString& password) {
    m_password = password;
}

QString Host::password() const {
    return m_password;
}

bool Host::isPasswordRequired() const {
    return !m_password.isEmpty();
}

void Host::setJpegQuality(int quality) {
    m_jpegQuality = qBound(m_minJpegQuality, quality, m_maxJpegQuality);
    // m_quality in JpegEncoder is atomic, so it's safe to poke the live
    // encoder from the main/GUI thread while the encode worker reads it.
    if (m_encodeWorker && m_encodeWorker->encoder()) {
        m_encodeWorker->encoder()->setJpegQuality(m_jpegQuality);
    }
}

void Host::logAudit(const QString& clientId, const QString& event, const QString& details) {
    if (m_auditLogger) {
        m_auditLogger->logConnection(clientId, event, details);
    }
}

void Host::logAuditOp(const QString& clientId, const QString& operation, const QString& details) {
    if (m_auditLogger) {
        m_auditLogger->logOperation(clientId, operation, details);
    }
}

void Host::onQualityTimer() {
    // Measure bandwidth over last 2 seconds
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 totalBytes = 0;
    qint64 windowStart = now - 2000;
    for (const auto& sample : m_bandwidthHistory) {
        if (sample.timestamp >= windowStart) {
            totalBytes += sample.bytes;
        }
    }

    int bandwidthBps = static_cast<int>(totalBytes * 1000LL / qMax(now - windowStart, 1LL));
    int bandwidthKbps = bandwidthBps / 1024;

    // If no clients, skip adaptation
    int clientCount = 0;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value().authenticated) clientCount++;
    }
    if (clientCount == 0) return;

    // Estimate required bandwidth for current quality (bytes per frame * fps * clients)
    int targetFps = qMin(m_captureFps, 30);
    int estBytesPerFrame = 0;
    if (m_jpegQuality <= 30) estBytesPerFrame = 10240;      // ~10KB/frame
    else if (m_jpegQuality <= 50) estBytesPerFrame = 20480;  // ~20KB
    else if (m_jpegQuality <= 65) estBytesPerFrame = 40960;  // ~40KB
    else if (m_jpegQuality <= 80) estBytesPerFrame = 81920;  // ~80KB
    else estBytesPerFrame = 122880;                           // ~120KB

    int requiredBps = estBytesPerFrame * targetFps * clientCount;

    if (requiredBps > 0) {
        double utilization = static_cast<double>(bandwidthBps) / requiredBps;
        if (utilization > 0.9 && m_qualityLevelIndex > 0) {
            m_consecutiveOverload++;
            m_consecutiveUnderload = 0;
            if (m_consecutiveOverload >= 2) {
                m_qualityLevelIndex--;
                setJpegQuality(QUALITY_STEPS[m_qualityLevelIndex]);
                m_consecutiveOverload = 0;
                LOG_DEBUG("Quality reduced to " + QString::number(m_jpegQuality) +
                          ", bandwidth=" + QString::number(bandwidthKbps) + "kbps, util=" +
                          QString::number(utilization, 'f', 2));
            }
        } else if (utilization < 0.5 && m_qualityLevelIndex < 4) {
            m_consecutiveUnderload++;
            m_consecutiveOverload = 0;
            if (m_consecutiveUnderload >= 3) {
                m_qualityLevelIndex++;
                setJpegQuality(QUALITY_STEPS[m_qualityLevelIndex]);
                m_consecutiveUnderload = 0;
                LOG_DEBUG("Quality increased to " + QString::number(m_jpegQuality) +
                          ", bandwidth=" + QString::number(bandwidthKbps) + "kbps, util=" +
                          QString::number(utilization, 'f', 2));
            }
        } else {
            m_consecutiveOverload = 0;
            m_consecutiveUnderload = 0;
        }
    }

    sendQualityInfo();
}

void Host::sendQualityInfo() {
    QualityInfo info;
    info.jpegQuality = m_jpegQuality;
    info.captureFps = m_captureFps;
    info.targetFps = qMin(m_captureFps, 30);

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 totalBytes = 0;
    qint64 windowStart = now - 2000;
    for (const auto& sample : m_bandwidthHistory) {
        if (sample.timestamp >= windowStart) {
            totalBytes += sample.bytes;
        }
    }
    info.bandwidthKbps = static_cast<int>(totalBytes * 1000LL / qMax(now - windowStart, 1LL)) / 1024;

    QByteArray payload = ProtocolManager::encodeQualityInfo(info);
    QByteArray message = ProtocolManager::encode(MessageType::QUALITY_INFO, payload);

    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value().socket && it.value().authenticated) {
            it.value().socket->write(message);
            it.value().socket->flush();
        }
    }
}

void Host::onNewConnection() {
    while (m_tcpServer->hasPendingConnections()) {
        QTcpSocket* socket = m_tcpServer->nextPendingConnection();
        acceptExternalSocket(socket);
    }
}

void Host::acceptExternalSocket(QTcpSocket* socket) {
    if (!socket) return;
    QString clientId = socket->peerAddress().toString() + ":" + QString::number(socket->peerPort());

    // Disable Nagle's algorithm for lowest latency real-time streaming
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    ClientInfo info;
    info.socket = socket;
    info.authenticated = m_password.isEmpty();
    m_clients[clientId] = info;

    connect(socket, &QTcpSocket::disconnected, this, &Host::onClientDisconnected);
    connect(socket, &QTcpSocket::readyRead, this, &Host::onClientDataReady);

    LOG_INFO("Host: Client connected: " + clientId + (info.authenticated ? " (auto-auth)" : " (needs auth)"));
    logAudit(clientId, "connected", info.authenticated ? "auto-auth" : "needs_auth");
    emit clientConnected(clientId);

    if (info.authenticated) {
        // No password required: the controller will not send an AUTH_REQ. The
        // session still requires the host user's explicit approval (consent),
        // so deliver the session key only after grantConsent().
        requestConsent(clientId);
    }

    updateNetworkWorkerClients();
}

void Host::sendAuthKeyTo(const QString& clientId) {
    QTcpSocket* socket = m_clients.value(clientId).socket;
    if (!socket) return;

    QByteArray keyData;
    if (m_encryption && m_encryption->isInitialized()) {
        keyData.append(m_encryption->key());
        keyData.append(m_encryption->iv());
    }
    QByteArray authPayload("OK");
    authPayload.append(keyData);
    QByteArray resp = ProtocolManager::encode(MessageType::AUTH_RESP, authPayload);
    socket->write(resp);
    socket->flush();
    LOG_INFO("Host: Sent session key to " + clientId);
}

void Host::requestConsent(const QString& clientId) {
    QTcpSocket* socket = m_clients.value(clientId).socket;
    if (!socket) return;

    QString peer = socket->peerAddress().toString();
    // Inform the controller that a host-side approval is required (so it can
    // show a "waiting for host approval" state). deviceName carries the host's
    // computer name for display on the controller side.
    QByteArray req = ProtocolManager::encodeConsent(false, QHostInfo::localHostName());
    QByteArray msg = ProtocolManager::encode(MessageType::CONSENT_REQUEST, req);
    socket->write(msg);
    socket->flush();

    emit consentRequested(clientId, peer);
    LOG_INFO("Host: Consent requested for " + clientId + " (peer " + peer + ")");
}

void Host::grantConsent(const QString& clientId) {
    if (!m_clients.contains(clientId)) return;
    ClientInfo& info = m_clients[clientId];
    if (!info.socket) return;

    info.consented = true;
    sendAuthKeyTo(clientId); // starts the session (key + AUTH_RESP OK)

    QByteArray resp = ProtocolManager::encodeConsent(true, QString());
    QByteArray msg = ProtocolManager::encode(MessageType::CONSENT_RESPONSE, resp);
    info.socket->write(msg);
    info.socket->flush();

    LOG_INFO("Host: Consent granted for " + clientId);
    emit clientAuthenticated(clientId);
    updateNetworkWorkerClients();
}

void Host::denyConsent(const QString& clientId) {
    if (!m_clients.contains(clientId)) return;
    ClientInfo& info = m_clients[clientId];
    QTcpSocket* socket = info.socket;

    if (socket) {
        QByteArray resp = ProtocolManager::encodeConsent(false, QString());
        QByteArray msg = ProtocolManager::encode(MessageType::CONSENT_RESPONSE, resp);
        socket->write(msg);
        socket->flush();
        socket->disconnectFromHost();
    }
    LOG_WARNING("Host: Consent denied for " + clientId);
    m_clients.remove(clientId);
    emit clientAuthFailed(clientId);
    updateNetworkWorkerClients();
}

void Host::onClientDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QString clientId;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value().socket == socket) {
            clientId = it.key();
            break;
        }
    }

    if (!clientId.isEmpty()) {
        m_clients.remove(clientId);
        LOG_INFO("Host: Client disconnected: " + clientId);
        logAudit(clientId, "disconnected");
        emit clientDisconnected(clientId);
        updateNetworkWorkerClients();
    }
}

void Host::onClientDataReady() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QString clientId;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value().socket == socket) {
            clientId = it.key();
            break;
        }
    }

    if (clientId.isEmpty()) return;

    m_clients[clientId].buffer.append(socket->readAll());
    processClientBuffer(clientId);
}

void Host::processClientBuffer(const QString& clientId) {
    if (!m_clients.contains(clientId)) return;
    QByteArray& buffer = m_clients[clientId].buffer;

    while (static_cast<size_t>(buffer.size()) >= MessageCodec::MIN_MESSAGE_SIZE) {
        MessageHeader header;
        uint32_t sessionIdLen = 0;
        if (!MessageCodec::parseHeader(buffer, header, sessionIdLen)) {
            break;
        }

        size_t totalSize = MessageCodec::HEADER_FIXED_SIZE + 4 +
                           sessionIdLen +
                           header.length + MessageCodec::CHECKSUM_SIZE;

        if (static_cast<size_t>(buffer.size()) < totalSize) {
            break;
        }

        QByteArray messageData = buffer.left(totalSize);
        buffer.remove(0, totalSize);

        if (!MessageCodec::verifyChecksum(messageData)) {
            continue;
        }

        processClientMessage(clientId, messageData);
    }
}

void Host::processClientMessage(const QString& clientId, const QByteArray& data) {
    MessageType type;
    QByteArray payload;
    QString sessionId;

    if (!ProtocolManager::decode(data, type, payload, sessionId)) {
        return;
    }

    if (type == MessageType::AUTH_REQ) {
        processAuthRequest(clientId, payload);
        return;
    }

    if (!m_clients.value(clientId).authenticated) {
        // Silently drop messages from unauthenticated clients (except AUTH_REQ
        // which is handled above).  Previously this sent NOT_AUTHORIZED back for
        // every unauthenticated message, including SCREEN_FRAME_ACK that the
        // controller starts sending immediately on connect — before auth
        // completes.  The NOT_AUTHORIZED reply was misinterpreted by the
        // controller as an auth failure, disrupting the session.
        return;
    }

    switch (type) {
        case MessageType::MOUSE_EVENT:
            processMouseEvent(clientId, payload);
            break;
        case MessageType::KEY_EVENT:
            processKeyEvent(clientId, payload);
            break;
        case MessageType::SCREEN_FRAME_ACK:
            break;
        case MessageType::SCREENSHOT_REQ: {
            QImage frame = m_screenCapture ? m_screenCapture->captureFrame() : QImage();
            if (!frame.isNull()) {
                QByteArray jpeg;
                QBuffer buffer(&jpeg);
                buffer.open(QIODevice::WriteOnly);
                frame.save(&buffer, "JPEG", 85);
                buffer.close();
                
                ScreenFrame screenFrame;
                screenFrame.data = jpeg;
                screenFrame.width = frame.width();
                screenFrame.height = frame.height();
                screenFrame.format = FrameFormat::JPEG;
                
                QByteArray respPayload = ProtocolManager::encodeScreenFrame(screenFrame);
                QByteArray resp = ProtocolManager::encode(MessageType::SCREENSHOT_RESP, respPayload);
                QTcpSocket* socket = m_clients.value(clientId).socket;
                if (socket) {
                    socket->write(resp);
                    socket->flush();
                }
            }
            break;
        }
        case MessageType::HEARTBEAT: {
            QTcpSocket* socket = m_clients.value(clientId).socket;
            if (socket) {
                // Echo the controller's timestamp so it can measure RTT.
                QByteArray resp = ProtocolManager::encode(MessageType::HEARTBEAT_RESP, payload);
                socket->write(resp);
                socket->flush();
            }
            break;
        }
        case MessageType::TERMINAL_START:
            processTerminalStart(clientId, payload);
            logAuditOp(clientId, "terminal_start");
            break;
        case MessageType::TERMINAL_INPUT:
            processTerminalInput(payload);
            break;
        case MessageType::TERMINAL_STOP:
            processTerminalStop(clientId);
            logAuditOp(clientId, "terminal_stop");
            break;
        case MessageType::CHAT_MESSAGE: {
            ChatMessage chat = ProtocolManager::decodeChatMessage(payload);
            chat.sender = "Host:" + clientId.left(8);
            chat.timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
            QByteArray respPayload = ProtocolManager::encodeChatMessage(chat);
            QByteArray resp = ProtocolManager::encode(MessageType::CHAT_MESSAGE, respPayload);
            for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
                if (it.value().authenticated && it.value().socket) {
                    it.value().socket->write(resp);
                    it.value().socket->flush();
                }
            }
            break;
        }
        case MessageType::RECORD_START: {
            RecordControl control = ProtocolManager::decodeRecordControl(payload);
            QString filePath = control.filePath;
            if (filePath.isEmpty()) {
                filePath = QString("record_%1.avi")
                    .arg(QDateTime::currentMSecsSinceEpoch());
            }
            bool ok = Host::startRecording(filePath, control.fps);
            QString status = ok ? "OK" : "ERROR:Failed to start recording";
            QByteArray ackPayload = status.toUtf8();
            QByteArray ack = ProtocolManager::encode(MessageType::RECORD_ACK, ackPayload);
            QTcpSocket* socket = m_clients.value(clientId).socket;
            if (socket) {
                socket->write(ack);
                socket->flush();
            }
            logAuditOp(clientId, "record_start", filePath);
            break;
        }
        case MessageType::RECORD_STOP: {
    stopRecording();
    setCameraMode(false);
            QByteArray ack = ProtocolManager::encode(MessageType::RECORD_ACK, QByteArray("OK"));
            QTcpSocket* socket = m_clients.value(clientId).socket;
            if (socket) {
                socket->write(ack);
                socket->flush();
            }
            logAuditOp(clientId, "record_stop");
            break;
        }
        case MessageType::CAMERA_MODE: {
            if (payload.size() >= 1) {
                bool enable = (payload[0] != 0);
                setCameraMode(enable);
            }
            break;
        }
        case MessageType::AUDIO_START: {
            if (payload.size() >= 1) {
                bool enable = (payload[0] != 0);
                setAudioEnabled(enable);
            }
            break;
        }
        case MessageType::AUDIO_DATA: {
            // Phase 6: controller microphone audio → play on the host's speakers.
            // Only play once the host user has granted consent. Do NOT rebroadcast.
            if (!m_clients.value(clientId).consented) return;
            if (!m_controllerAudioPlayer) {
                m_controllerAudioPlayer = new AudioPlayer(this);
                if (!m_controllerAudioPlayer->initialize()) {
                    LOG_WARNING("Host: controller-audio player init failed");
                    delete m_controllerAudioPlayer;
                    m_controllerAudioPlayer = nullptr;
                }
            }
            if (m_controllerAudioPlayer && m_controllerAudioPlayer->isInitialized()) {
                m_controllerAudioPlayer->playAudio(payload);
            }
            break;
        }
        case MessageType::FILE_REQ: {
            handleFileRequest(clientId, payload);
            logAuditOp(clientId, "file_transfer");
            break;
        }
        case MessageType::FILE_DATA: {
            handleFileData(clientId, payload);
            break;
        }
        case MessageType::CLIPBOARD_DATA: {
            ClipboardData clipData = ProtocolManager::decodeClipboardData(payload);
            // Guard for headless/service mode where no QApplication exists.
            if (auto* app = (qApp ? qobject_cast<QApplication*>(qApp) : nullptr)) {
                app->clipboard()->setText(QString::fromUtf8(clipData.data));
            }
            QByteArray respPayload = ProtocolManager::encodeClipboardData(clipData);
            QByteArray resp = ProtocolManager::encode(MessageType::CLIPBOARD_DATA, respPayload);
            for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
                if (it.key() != clientId && it.value().authenticated && it.value().socket) {
                    it.value().socket->write(resp);
                    it.value().socket->flush();
                }
            }
            break;
        }
        case MessageType::FILE_BROWSER_REQ: {
            handleFileBrowserRequest(clientId, payload);
            break;
        }
        case MessageType::SYSINFO_REQ: {
            handleSystemInfoRequest(clientId, payload);
            break;
        }
        case MessageType::POWER_COMMAND: {
            if (payload.size() >= 1) {
                PowerAction action = static_cast<PowerAction>(payload[0]);
                executePowerAction(action);
                logAuditOp(clientId, "power_command", QString::number(static_cast<int>(action)));
            }
            break;
        }
        case MessageType::PRIVACY_SCREEN: {
            if (payload.size() >= 1) {
                bool enabled = ProtocolManager::decodePrivacyScreen(payload);
                setPrivacyScreenEnabled(enabled);
                logAuditOp(clientId, "privacy_screen", enabled ? "on" : "off");
                LOG_INFO("Host: Privacy screen " + QString(enabled ? "enabled" : "disabled") + " by " + clientId);
            }
            break;
        }
        case MessageType::MONITOR_LIST: {
            if (m_screenCapture) {
                QList<MonitorInfo> monitors = m_screenCapture->getMonitorList();
                QByteArray respPayload = ProtocolManager::encodeMonitorList(monitors);
                QByteArray resp = ProtocolManager::encode(MessageType::MONITOR_LIST, respPayload);
                QTcpSocket* socket = m_clients.value(clientId).socket;
                if (socket) {
                    socket->write(resp);
                    socket->flush();
                }
            }
            break;
        }
        case MessageType::MONITOR_SWITCH: {
            if (payload.size() >= 4 && m_screenCapture) {
                QDataStream stream(payload);
                stream.setByteOrder(QDataStream::BigEndian);
                uint32_t index;
                stream >> index;
                m_screenCapture->setMonitorIndex(static_cast<int>(index));
                LOG_INFO("Host: Monitor switched to index " + QString::number(index));
            }
            break;
        }
        default:
            break;
    }
}

void Host::processAuthRequest(const QString& clientId, const QByteArray& payload) {
    QString receivedPassword = QString::fromUtf8(payload);
    QTcpSocket* socket = m_clients.value(clientId).socket;

    if (!socket) return;

    bool authOk = false;

    if (m_password.isEmpty()) {
        authOk = true;
    } else if (receivedPassword == m_password) {
        authOk = true;
    }

    if (authOk) {
        m_clients[clientId].authenticated = true;

        // Phase 5: do NOT start the session yet. Ask the host user to approve.
        LOG_INFO("Host: Client authenticated, awaiting consent: " + clientId);
        requestConsent(clientId);
    } else {
        QByteArray resp = ProtocolManager::encode(MessageType::AUTH_RESP, QByteArray("FAILED"));
        socket->write(resp);
        socket->flush();
        LOG_WARNING("Host: Auth failed for client: " + clientId);
        emit clientAuthFailed(clientId);
    }
}

void Host::processMouseEvent(const QString& clientId, const QByteArray& payload) {
    // Phase 5: ignore input until the host user has granted consent.
    if (!m_clients.value(clientId).consented) return;
    if (!m_inputControl) return;
    MouseEvent event = ProtocolManager::decodeMouseEvent(payload);
    m_inputControl->processMouseEvent(event);
}

void Host::processKeyEvent(const QString& clientId, const QByteArray& payload) {
    // Phase 5: ignore input until the host user has granted consent.
    if (!m_clients.value(clientId).consented) return;
    if (!m_inputControl) return;
    KeyEvent event = ProtocolManager::decodeKeyEvent(payload);
    m_inputControl->processKeyEvent(event);
}

void Host::updateNetworkWorkerClients() {
    if (!m_networkWorker) return;
    QHash<QString, QTcpSocket*> activeClients;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        // Phase 5: only fully-approved (consented) clients receive frames.
        if (it.value().consented) {
            activeClients[it.key()] = it.value().socket;
        }
    }
    m_networkWorker->setClients(activeClients);
    
    if (m_privacyScreenEnabled) {
        bool hasClient = !activeClients.isEmpty();
        if (hasClient && !m_privacyScreen) {
            m_privacyScreen = new PrivacyScreen(this);
            m_privacyScreen->show();
        } else if (!hasClient && m_privacyScreen) {
            m_privacyScreen->hide();
            m_privacyScreen->deleteLater();
            m_privacyScreen = nullptr;
        } else if (hasClient && m_privacyScreen && !m_privacyScreen->isVisible()) {
            m_privacyScreen->show();
        }
    }
}

void Host::onDiscoveryRequest() {
    while (m_udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_udpSocket->pendingDatagramSize());
        QHostAddress sender;
        quint16 senderPort;
        m_udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        MessageType type;
        QByteArray payload;
        QString sessionId;
        if (ProtocolManager::decode(datagram, type, payload, sessionId)) {
            if (type == MessageType::DEVICE_DISCOVER_REQ) {
                broadcastDiscoveryResponse();
            }
        }
    }
}

void Host::broadcastDiscoveryResponse() {
    if (!m_udpSocket) return;

    DeviceInfo info;
    info.deviceId = QHostInfo::localHostName();
    info.deviceName = QHostInfo::localHostName();
    info.ipAddress = getLocalIp();
    info.port = m_port;
    info.version = "1.0.0";
    info.accessCode = m_accessCode;
    info.timestamp = QDateTime::currentMSecsSinceEpoch();

    QByteArray payload = ProtocolManager::encodeDeviceInfo(info);
    QByteArray message = ProtocolManager::encode(MessageType::DEVICE_DISCOVER_RESP, payload);

    m_udpSocket->writeDatagram(message, QHostAddress::Broadcast, UDP_BROADCAST_PORT);
}

QString Host::getLocalIp() {
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        if (iface.flags().testFlag(QNetworkInterface::IsUp) &&
            iface.flags().testFlag(QNetworkInterface::IsRunning) &&
            !iface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            const auto entries = iface.addressEntries();
            for (const QNetworkAddressEntry& entry : entries) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    return entry.ip().toString();
                }
            }
        }
    }
    return "127.0.0.1";
}

bool Host::hasFrameChanged(const QImage& current, const QImage& previous, int threshold) {
    if (current.size() != previous.size()) {
        return true;
    }

    int sampleStep = 4;
    int changedPixels = 0;
    int totalSamples = 0;

    for (int y = 0; y < current.height(); y += sampleStep) {
        const uchar* currLine = current.constScanLine(y);
        const uchar* prevLine = previous.constScanLine(y);
        
        for (int x = 0; x < current.width(); x += sampleStep) {
            int offset = x * 4;
            int rDiff = qAbs(currLine[offset] - prevLine[offset]);
            int gDiff = qAbs(currLine[offset + 1] - prevLine[offset + 1]);
            int bDiff = qAbs(currLine[offset + 2] - prevLine[offset + 2]);
            
            if (rDiff + gDiff + bDiff > threshold * 3) {
                changedPixels++;
            }
            totalSamples++;
        }
    }

    double changeRatio = static_cast<double>(changedPixels) / totalSamples;
    return changeRatio > 0.01;
}

void Host::onCaptureWorkerError(const QString& message) {
    LOG_ERROR("Capture worker error: " + message);
}

void Host::onEncodeWorkerError(const QString& message) {
    LOG_ERROR("Encode worker error: " + message);
}

void Host::onNetworkWorkerError(const QString& message) {
    LOG_ERROR("Network worker error: " + message);
}

void Host::onAudioDataCaptured(const QByteArray& pcmData) {
    if (!m_running || pcmData.isEmpty()) return;

    QByteArray msg = ProtocolManager::encode(MessageType::AUDIO_DATA, pcmData, QString());
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value().authenticated && it.value().socket) {
            it.value().socket->write(msg);
        }
    }
}

void Host::executePowerAction(PowerAction action) {
    LOG_INFO("Host: Executing power action: " + QString::number(static_cast<int>(action)));
    
    const char* cmd = nullptr;
    switch (action) {
        case PowerAction::SHUTDOWN:
            cmd = "shutdown /s /t 3 /c \"远程控制用户请求关机\"";
            break;
        case PowerAction::RESTART:
            cmd = "shutdown /r /t 3 /c \"远程控制用户请求重启\"";
            break;
        case PowerAction::LOGOUT:
            cmd = "shutdown /l";
            break;
        case PowerAction::SLEEP:
            cmd = "rundll32.exe powrprof.dll,SetSuspendState 0,1,0";
            break;
        case PowerAction::HIBERNATE:
            cmd = "rundll32.exe powrprof.dll,SetSuspendState 1,1,0";
            break;
        case PowerAction::LOCK:
            // Lock the workstation immediately (no shell command needed).
            ::LockWorkStation();
            break;
    }
    if (cmd) {
        ::system(cmd);
    }
}

void Host::handleFileRequest(const QString& clientId, const QByteArray& payload) {
    FileRequest request = ProtocolManager::decodeFileRequest(payload);
    if (!m_clients.contains(clientId)) return;
    
    ClientInfo& client = m_clients[clientId];
    ClientInfo::FileTransfer transfer;
    transfer.fileId = request.fileId;
    transfer.fileName = request.fileName;
    transfer.fileSize = request.fileSize;
    transfer.isUpload = request.isUpload;
    
    if (request.isUpload) {
        QString savePath = QDir::tempPath() + "/" + request.fileName;
        QFile* file = new QFile(savePath);
        if (file->open(QIODevice::WriteOnly)) {
            transfer.file = file;
            LOG_INFO("Host: Receiving file upload: " + request.fileName);
        } else {
            delete file;
            return;
        }
    } else {
        // Open the full remote path; fall back to basename for legacy requests.
        QString hostPath = request.path.isEmpty() ? request.fileName : request.path;
        QFile* file = new QFile(hostPath);
        if (file->exists() && file->open(QIODevice::ReadOnly)) {
            transfer.file = file;
            transfer.fileSize = file->size();
            LOG_INFO("Host: Sending file download: " + request.fileName);
            
            // Send FILE_DATA chunks
            char buf[65536];
            uint64_t offset = 0;
            while (!file->atEnd()) {
                qint64 bytesRead = file->read(buf, sizeof(buf));
                if (bytesRead <= 0) break;
                
                FileData fd;
                fd.fileId = request.fileId;
                fd.offset = offset;
                fd.data = QByteArray(buf, bytesRead);
                
                QByteArray chunk = ProtocolManager::encodeFileData(fd);
                QByteArray msg = ProtocolManager::encode(MessageType::FILE_DATA, chunk);
                client.socket->write(msg);
                
                offset += bytesRead;
            }
            file->close();
            delete file;
            LOG_INFO("Host: File download completed: " + request.fileName);

            // Send an empty FILE_DATA chunk as an end-of-transfer marker so the
            // controller knows when the download is finished (size may be unknown).
            FileData eof;
            eof.fileId = request.fileId;
            eof.offset = offset;
            eof.data = QByteArray();
            QByteArray eofMsg = ProtocolManager::encode(
                MessageType::FILE_DATA, ProtocolManager::encodeFileData(eof));
            client.socket->write(eofMsg);
            return;
        } else {
            LOG_ERROR("Host: File not found: " + request.fileName);
            delete file;
            return;
        }
    }
    
    client.fileTransfers[request.fileId] = transfer;
}

void Host::handleFileData(const QString& clientId, const QByteArray& payload) {
    FileData fileData = ProtocolManager::decodeFileData(payload);
    if (!m_clients.contains(clientId)) return;
    
    ClientInfo& client = m_clients[clientId];
    if (!client.fileTransfers.contains(fileData.fileId)) return;
    
    ClientInfo::FileTransfer& transfer = client.fileTransfers[fileData.fileId];
    if (transfer.file && transfer.file->isOpen()) {
        transfer.file->seek(fileData.offset);
        transfer.file->write(fileData.data);
        transfer.bytesReceived += fileData.data.size();
        
        if (transfer.fileSize > 0 && transfer.bytesReceived >= transfer.fileSize) {
            transfer.file->close();
            LOG_INFO("Host: File upload completed: " + transfer.fileName);
            client.fileTransfers.remove(fileData.fileId);
            delete transfer.file;
        }
    }
}

void Host::handleFileBrowserRequest(const QString& clientId, const QByteArray& payload) {
    FileBrowserRequest req = ProtocolManager::decodeFileBrowserRequest(payload);
    FileBrowserResponse resp;
    resp.path = req.path;

    QDir dir(req.path);
    if (!dir.exists()) {
        resp.success = false;
        resp.errorMessage = "目录不存在: " + req.path;
    } else {
        resp.success = true;
        QFileInfoList files = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
            QDir::DirsFirst | QDir::Name);
        for (const QFileInfo& fi : files) {
            FileBrowserEntry entry;
            entry.name = fi.fileName();
            entry.path = fi.absoluteFilePath();
            entry.isDir = fi.isDir();
            entry.fileSize = fi.isDir() ? 0 : static_cast<uint64_t>(fi.size());
            entry.lastModified = fi.lastModified().toString("yyyy-MM-dd hh:mm:ss");
            resp.entries.append(entry);
        }
    }

    QByteArray respPayload = ProtocolManager::encodeFileBrowserResponse(resp);
    QByteArray msg = ProtocolManager::encode(MessageType::FILE_BROWSER_RESP, respPayload);
    QTcpSocket* socket = m_clients.value(clientId).socket;
    if (socket) {
        socket->write(msg);
        socket->flush();
    }
}

void Host::handleSystemInfoRequest(const QString& clientId, const QByteArray& payload) {
    Q_UNUSED(payload);
    SysInfo info = SystemInfoCollector::collect();

    QByteArray respPayload = ProtocolManager::encodeSysInfo(info);
    QByteArray msg = ProtocolManager::encode(MessageType::SYSINFO_RESP, respPayload);
    QTcpSocket* socket = m_clients.value(clientId).socket;
    if (socket) {
        socket->write(msg);
        socket->flush();
    }
}

void Host::setEncoderType(EncoderType type) {
    if (!m_encodeWorker) return;
    
    auto encoder = VideoEncoder::create(type);
    if (encoder) {
        encoder->initialize(1920, 1080, m_captureFps);
        m_encodeWorker->setEncoderAsync(std::move(encoder));
        LOG_INFO("Encoder switch queued to " + QString(type == EncoderType::H264 ? "H264" : "JPEG"));
    }
}

bool Host::startRecording(const QString& filePath, int fps) {
    if (!m_screenCapture || !m_screenCapture->isInitialized()) {
        LOG_ERROR("Cannot start recording: screen capture not initialized");
        return false;
    }

    stopRecording();

    ScreenCapture* cap = m_screenCapture;
    QImage testFrame = cap->captureFrame();
    if (testFrame.isNull()) {
        LOG_ERROR("Cannot start recording: failed to capture test frame");
        return false;
    }

    m_recorder = new ScreenRecorder();
    if (!m_recorder->startRecording(filePath, testFrame.width(), testFrame.height(), fps)) {
        delete m_recorder;
        m_recorder = nullptr;
        LOG_ERROR("Cannot start recording: failed to open file");
        return false;
    }

    m_recordFps = qBound(1, fps, 60);
    m_recordTimer = new QTimer(this);
    connect(m_recordTimer, &QTimer::timeout, this, [this]() {
        if (!m_recorder || !m_screenCapture) return;
        QImage frame = m_screenCapture->captureFrame();
        if (frame.isNull()) return;

        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        frame.save(&buffer, "JPEG", 85);
        buffer.close();

        m_recorder->addFrame(jpeg);
    });
    m_recordTimer->start(1000 / m_recordFps);

    LOG_INFO("Recording started: " + filePath + " at " + QString::number(fps) + " fps");
    return true;
}

void Host::stopRecording() {
    if (m_recordTimer) {
        m_recordTimer->stop();
        delete m_recordTimer;
        m_recordTimer = nullptr;
    }
    if (m_recorder) {
        QString path = m_recorder->filePath();
        m_recorder->stopRecording();
        delete m_recorder;
        m_recorder = nullptr;
        LOG_INFO("Recording stopped: " + path);
    }
}

bool Host::isRecording() const {
    return m_recorder != nullptr;
}

void Host::setEncoderTrueColor(bool enable) {
    m_trueColor = enable;
    // Applied when the encoder is (re)created in start(). Reconfiguring a live
    // encoder (sws context + pixel format) from the GUI thread races with the
    // encode worker, so only touch the running encoder while the host is idle.
    if (m_encodeWorker && m_encodeWorker->encoder() && !m_running) {
        m_encodeWorker->encoder()->setTrueColor(enable);
    }
}

void Host::setAudioEnabled(bool enabled) {
    m_audioEnabled = enabled;
    if (m_running) {
        if (enabled && m_audioCapture && !m_audioCapture->isInitialized()) {
            m_audioCapture->initialize();
        } else if (!enabled && m_audioCapture && m_audioCapture->isInitialized()) {
            m_audioCapture->shutdown();
        }
    }
}

bool Host::isAudioEnabled() const {
    return m_audioEnabled;
}

void Host::setPrivacyScreenEnabled(bool enabled) {
    m_privacyScreenEnabled = enabled;
    if (enabled) {
        // Show immediately if clients are already connected
        bool hasClient = false;
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
            if (it.value().authenticated) { hasClient = true; break; }
        }
        if (hasClient && !m_privacyScreen) {
            m_privacyScreen = new PrivacyScreen(this);
            m_privacyScreen->show();
        }
    } else {
        if (m_privacyScreen) {
            m_privacyScreen->hide();
            m_privacyScreen->deleteLater();
            m_privacyScreen = nullptr;
        }
    }
    LOG_INFO("Privacy screen " + QString(enabled ? "enabled" : "disabled"));
}

bool Host::isPrivacyScreenEnabled() const {
    return m_privacyScreenEnabled;
}

void Host::setCameraMode(bool enabled) {
    if (enabled && m_cameraCapture && m_cameraCapture->isInitialized()) {
        m_captureWorker->setUseCamera(true);
        LOG_INFO("Host: Switched to camera mode");
    } else if (!enabled) {
        m_captureWorker->setUseCamera(false);
        LOG_INFO("Host: Switched to screen mode");
    }
}

bool Host::isCameraMode() const {
    return m_captureWorker ? m_captureWorker->isUsingCamera() : false;
}

EncoderType Host::encoderType() const {
    if (m_encodeWorker && m_encodeWorker->encoder()) {
        return m_encodeWorker->encoder()->type();
    }
    return EncoderType::JPEG;
}

void Host::processTerminalStart(const QString& clientId, const QByteArray& payload) {
    TerminalData data = ProtocolManager::decodeTerminalData(payload);
    
    if (m_terminals.contains(clientId)) {
        m_terminals[clientId]->stopTerminal();
        delete m_terminals[clientId];
    }
    
    RemoteTerminal* terminal = new RemoteTerminal(this);
    
    connect(terminal, &RemoteTerminal::outputReady, this, [this, clientId](const QByteArray& output) {
        QTcpSocket* socket = m_clients.value(clientId).socket;
        if (!socket) return;
        
        TerminalData outData;
        outData.data = QString::fromUtf8(output);
        QByteArray payload = ProtocolManager::encodeTerminalData(outData);
        QByteArray msg = ProtocolManager::encode(MessageType::TERMINAL_OUTPUT, payload);
        socket->write(msg);
        socket->flush();
    });
    
    connect(terminal, &RemoteTerminal::terminalClosed, this, [this, clientId](int exitCode) {
        Q_UNUSED(exitCode);
        QTcpSocket* socket = m_clients.value(clientId).socket;
        if (socket) {
            TerminalData outData;
            outData.data = "\r\n[Process exited]\r\n";
            QByteArray payload = ProtocolManager::encodeTerminalData(outData);
            QByteArray msg = ProtocolManager::encode(MessageType::TERMINAL_OUTPUT, payload);
            socket->write(msg);
            socket->flush();
        }
        if (m_terminals.contains(clientId)) {
            m_terminals[clientId]->deleteLater();
            m_terminals.remove(clientId);
        }
    });
    
    connect(terminal, &RemoteTerminal::terminalError, this, [this, clientId](const QString& error) {
        QTcpSocket* socket = m_clients.value(clientId).socket;
        if (socket) {
            TerminalData outData;
            outData.data = "\r\n[Error: " + error + "]\r\n";
            QByteArray payload = ProtocolManager::encodeTerminalData(outData);
            QByteArray msg = ProtocolManager::encode(MessageType::TERMINAL_OUTPUT, payload);
            socket->write(msg);
            socket->flush();
        }
    });
    
    if (terminal->startTerminal(data.shellType, data.cols, data.rows)) {
        m_terminals[clientId] = terminal;
        LOG_INFO("Terminal started for client: " + clientId);
    } else {
        delete terminal;
        LOG_ERROR("Failed to start terminal for client: " + clientId);
    }
}

void Host::processTerminalInput(const QByteArray& payload) {
    TerminalData data = ProtocolManager::decodeTerminalData(payload);
    for (auto it = m_terminals.begin(); it != m_terminals.end(); ++it) {
        if (it.value()->isRunning()) {
            it.value()->writeInput(data.data.toUtf8());
        }
    }
}

void Host::processTerminalStop(const QString& clientId) {
    if (m_terminals.contains(clientId)) {
        m_terminals[clientId]->stopTerminal();
        m_terminals[clientId]->deleteLater();
        m_terminals.remove(clientId);
        LOG_INFO("Terminal stopped for client: " + clientId);
    }
}

// ---------------- P2P / relay connection path ----------------

void Host::setNatTraversal(NatTraversal* nat) {
    if (m_nat == nat) return;
    m_nat = nat;
    if (!m_p2p) {
        m_p2p = new P2PManager(this);
    }
    if (m_nat) {
        connect(m_nat, &NatTraversal::peerAddressReceived, this, &Host::onPeerAddressReceived);
        connect(m_nat, &NatTraversal::bridgeSocketReady, this, &Host::onBridgeSocketReady);
    }
}

void Host::onPeerAddressReceived(const QString& peerId, const QHostAddress& address, quint16 port) {
    Q_UNUSED(peerId);
    if (m_p2p) {
        LOG_INFO("Host: punching peer " + address.toString() + ":" + QString::number(port));
        m_p2p->beginPunch(address, port);
    }
}

void Host::onP2PDirect(QTcpSocket* socket) {
    LOG_INFO("Host: P2P direct connection accepted");
    acceptExternalSocket(socket);
}

void Host::onBridgeSocketReady(QTcpSocket* socket, const QByteArray& initialData) {
    LOG_INFO("Host: relay bridge connection accepted");
    acceptExternalSocket(socket);
    if (!initialData.isEmpty()) {
        QString clientId;
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
            if (it.value().socket == socket) {
                clientId = it.key();
                break;
            }
        }
        if (!clientId.isEmpty()) {
            m_clients[clientId].buffer.append(initialData);
            processClientBuffer(clientId);
        }
    }
}

} // namespace xrk
