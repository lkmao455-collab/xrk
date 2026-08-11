#include "host.h"
#include <algorithm>
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
#include "process_collector.h"
#include "annotation_overlay.h"
#include "database_manager.h"
#include "core/permission_model.h"
#include "core/logger.h"
#include <QTcpSocket>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QBuffer>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QApplication>
#include <QClipboard>
#include <QDataStream>
#include <QSettings>

// Avoid pulling in <windows.h> here: it defines MOUSE_EVENT / KEY_EVENT macros
// that collide with the MessageType enum cases below. Declare the one API we
// need (LockWorkStation, from user32.dll) directly.
#ifdef _WIN32
extern "C" {
    __declspec(dllimport) int __stdcall LockWorkStation(void);
}
#endif

namespace xrk {

// ==================== CaptureWorker ====================

CaptureWorker::CaptureWorker(ScreenCapture* capture, CameraCapture* camera, FrameQueue<CapturedFrame>* queue, int fps)
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
    // [DIAG] One-time, prominent marker so a single run reveals whether capture works.
    if (m_capture) {
        if (m_capture->isInitialized()) {
            LOG_INFO("[DIAG] CaptureWorker: screen capture INITIALIZED OK (fps=" + QString::number(m_fps) + ")");
        } else {
            LOG_ERROR("[DIAG] CaptureWorker: screen capture FAILED to initialize - desktop will be BLACK. "
                      "Check DXGI/GDI availability, a real display is present, and this is not a "
                      "headless/secure-desktop/UAC-prompt session.");
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
    CapturedFrame captured;
    if (m_useCamera && m_camera && m_camera->isInitialized()) {
        // The camera backend has no change metadata; an empty dirty list tells
        // the encoder to diff the whole frame itself.
        captured.image = m_camera->captureFrame();
    } else if (m_capture && m_capture->isInitialized()) {
        captured.image = m_capture->captureFrame(&captured.dirtyRects);
    }
    const QImage& frame = captured.image;
    if (!frame.isNull()) {
        static bool firstCaptured = false;
        if (!firstCaptured) {
            firstCaptured = true;
            LOG_INFO("[DIAG] CaptureWorker: FIRST frame captured " +
                     QString::number(frame.width()) + "x" + QString::number(frame.height()));
        }
        static int captureCount = 0;
        if (++captureCount % 1800 == 1) { // Log every ~30 seconds at 60fps
            LOG_INFO("CaptureWorker: captured " + QString::number(captureCount) + 
                     " frames, current: " + QString::number(frame.width()) + "x" + QString::number(frame.height()));
        }
        // Non-blocking enqueue: if queue is full, drop this frame instead of
        // blocking the timer thread (which would stall all subsequent captures).
        if (!m_queue->enqueueNonBlocking(captured)) {
            static int droppedCount = 0;
            if (++droppedCount % 300 == 1) {
                LOG_WARNING("CaptureWorker: dropped " + QString::number(droppedCount) +
                            " frames (encode queue full)");
            }
        }
    } else {
        static int nullCount = 0;
        if (++nullCount % 300 == 1) {
            LOG_WARNING("CaptureWorker: " + QString::number(nullCount) + 
                        " null frames so far. Screen capture may be failing - "
                        "check DXGI Desktop Duplication / GDI fallback. "
                        "Common causes: UAC, secure desktop, no display, "
                        "headless session, or permission issues.");
        }
    }
}

// ==================== EncodeWorker ====================

EncodeWorker::EncodeWorker(FrameQueue<CapturedFrame>* inputQueue, FrameQueue<QByteArray>* outputQueue)
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

void EncodeWorker::requestFallbackToJpeg() {
    m_fallbackRequested = true;
}

void EncodeWorker::setTiledMode(bool enabled) {
    if (m_tiledMode.exchange(enabled) != enabled && enabled) {
        // Entering tiled mode: the client has no tile cache yet.
        m_keyFrameRequest = true;
    }
}

QByteArray EncodeWorker::sealPayload(const QByteArray& payload) {
    if (m_encryption && m_encryption->isInitialized()) {
        QByteArray encrypted = m_encryption->encrypt(payload);
        if (!encrypted.isEmpty()) {
            return encrypted;
        }
    }
    return payload;
}

void EncodeWorker::resendTiles(const QList<QPoint>& tiles) {
    QImage frame;
    {
        QMutexLocker lock(&m_lastFrameMutex);
        frame = m_lastRawFrame;
    }
    if (frame.isNull()) {
        return;
    }

    int sent = 0;
    const uint32_t fw = static_cast<uint32_t>(frame.width());
    const uint32_t fh = static_cast<uint32_t>(frame.height());
    const int tileSize = m_tileEncoder ? m_tileEncoder->tileSize() : TILE_SIZE;
    static std::atomic<uint32_t> s_seq{0};

    for (const QPoint& p : tiles) {
        const int col = p.x() / tileSize;
        const int row = p.y() / tileSize;
        const int tx = col * tileSize;
        const int ty = row * tileSize;
        if (tx >= frame.width() || ty >= frame.height()) {
            continue; // out of bounds relative to current frame
        }
        const QRect area(tx, ty,
                        qMin(tileSize, frame.width() - tx),
                        qMin(tileSize, frame.height() - ty));
        TileEncoding encoding = TileEncoding::JPEG;
        QByteArray data = TileEncoder::encodeTilePixels(frame.copy(area), m_tileQuality.load(), &encoding);
        if (data.isEmpty()) {
            continue;
        }

        ScreenTile tile;
        tile.x = static_cast<uint32_t>(tx);
        tile.y = static_cast<uint32_t>(ty);
        tile.w = static_cast<uint32_t>(area.width());
        tile.h = static_cast<uint32_t>(area.height());
        tile.seq = ++s_seq;
        tile.frameWidth = fw;
        tile.frameHeight = fh;
        tile.encoding = static_cast<uint8_t>(encoding);
        tile.timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
        tile.data = data;
        tile.hash = QCryptographicHash::hash(data, QCryptographicHash::Md5);

        QByteArray message = ProtocolManager::encode(
            MessageType::SCREEN_TILE, sealPayload(ProtocolManager::encodeScreenTile(tile)));
        if (!m_outputQueue->enqueueNonBlocking(message)) {
            break; // output backpressured; a keyframe will eventually repair it
        }
        ++sent;
    }
    if (sent > 0) {
        LOG_INFO("EncodeWorker(tiled): NACK resend — " + QString::number(sent) + " tile(s)");
    }
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
            m_consecutiveEmptyEncodes = 0;
            LOG_INFO("Encoder swapped on encode thread");
        }

        // Check for fallback request (from host due to consecutive empty encodes)
        if (m_fallbackRequested) {
            m_fallbackRequested = false;
            auto jpegEncoder = VideoEncoder::create(EncoderType::JPEG);
            if (jpegEncoder && jpegEncoder->initialize(1920, 1080, 60)) {
                m_encoder = std::move(jpegEncoder);
                m_consecutiveEmptyEncodes = 0;
                emit encoderChanged(EncoderType::JPEG);
                LOG_WARNING("EncodeWorker: Fallback to JPEG encoder due to consecutive empty H264 encodes");
            } else {
                LOG_ERROR("EncodeWorker: JPEG fallback failed!");
            }
        }

        CapturedFrame captured = m_inputQueue->dequeue(50);
        if (captured.image.isNull()) {
            continue;
        }

        // At high FPS, drain stale frames from the queue to stay current. The
        // dirty regions of the skipped frames must be carried forward, or the
        // tiled encoder would never learn that those areas changed.
        while (m_inputQueue->size() > 2) {
            CapturedFrame stale = m_inputQueue->dequeue(1);
            if (stale.image.isNull()) break;
            if (captured.dirtyRects.isEmpty() || stale.dirtyRects.isEmpty()) {
                stale.dirtyRects.clear();   // one frame had no hint -> no hint at all
            } else {
                stale.dirtyRects.append(captured.dirtyRects);
            }
            captured = stale;
        }

        const QImage& rawFrame = captured.image;

        if (m_tiledMode.load()) {
            if (!m_tileEncoder) {
                m_tileEncoder = std::make_unique<TileEncoder>();
            }
            m_tileEncoder->setQuality(m_tileQuality.load());
            m_tileEncoder->setMaxTilesPerFrame(m_maxTilesPerFrame.load());
            const bool keyFrame = m_keyFrameRequest.exchange(false);
            if (keyFrame) {
                m_tileEncoder->requestKeyFrame();
            }

            QList<ScreenTile> tiles = m_tileEncoder->buildTiles(rawFrame, captured.dirtyRects);
            if (tiles.isEmpty()) {
                continue;   // nothing on screen moved: send nothing at all
            }

            // Cache the current frame so NACK resends can re-encode requested
            // tiles on demand.
            {
                QMutexLocker lock(&m_lastFrameMutex);
                m_lastRawFrame = rawFrame.copy();
            }

            // Cursor-aware priority: stream the tile(s) under the pointer first
            // so the region the user is actively using repaints before the rest
            // of the screen on a bandwidth-limited link.
            const QPoint mp = m_mousePos;
            if (!mp.isNull() && tiles.size() > 1) {
                std::stable_partition(tiles.begin(), tiles.end(),
                    [&](const ScreenTile& t) {
                        const int px = mp.x(), py = mp.y();
                        return px >= static_cast<int>(t.x) && px < static_cast<int>(t.x + t.w) &&
                               py >= static_cast<int>(t.y) && py < static_cast<int>(t.y + t.h);
                    });
            }
            if (keyFrame) {
                LOG_INFO("EncodeWorker(tiled): keyframe sent — " +
                         QString::number(tiles.size()) + " tiles, " +
                         QString::number(m_tileEncoder->lastFrameBytes() / 1024) + "KB");
            }

            ++framesEncoded;
            int tileDrops = 0;
            for (const ScreenTile& tile : tiles) {
                QByteArray payload = sealPayload(ProtocolManager::encodeScreenTile(tile));
                QByteArray message = ProtocolManager::encode(MessageType::SCREEN_TILE, payload);
                if (!m_outputQueue->enqueueNonBlocking(message)) {
                    ++tileDrops;
                }
            }
            if (tileDrops > 0) {
                // Whatever did not fit is lost for good, so the client's cache
                // for those tiles is stale: schedule a full refresh.
                outputDrops += tileDrops;
                m_tileEncoder->requestKeyFrame();
                if (outputDrops % 300 < tileDrops) {
                    LOG_WARNING("EncodeWorker: output queue full, dropped " +
                                QString::number(outputDrops) + " tiles");
                }
            }
            if (framesEncoded % 300 == 1) {
                LOG_INFO("EncodeWorker(tiled): " + QString::number(framesEncoded) +
                         " frames, " + QString::number(tiles.size()) + " tiles/" +
                         QString::number(m_tileEncoder->lastFrameBytes() / 1024) + "KB this frame, " +
                         QString::number(m_tileEncoder->pendingTileCount()) + " deferred");
            }
            continue;
        }

        QByteArray encodedData;
        FrameFormat format = FrameFormat::JPEG;

        qint64 encodeStart = QDateTime::currentMSecsSinceEpoch();

        if (m_encoder && m_encoder->isInitialized()) {
            encodedData = m_encoder->encode(rawFrame);
            format = (m_encoder->type() == EncoderType::H264) ? FrameFormat::H264 : FrameFormat::JPEG;
            if (encodedData.isEmpty()) {
                // Encoder returned empty data - track consecutive failures for auto-fallback
                int emptyCount = ++m_consecutiveEmptyEncodes;
                if (emptyCount % 30 == 1) {
                    LOG_WARNING("EncodeWorker: Empty encode #" + QString::number(emptyCount) +
                                " (type: " + (m_encoder->type() == EncoderType::H264 ? "H264" : "JPEG") + ")");
                }
                // Auto-fallback after 60 consecutive empty encodes (~1 second at 60fps)
                if (m_encoder->type() == EncoderType::H264 && emptyCount >= 60) {
                    LOG_ERROR("EncodeWorker: " + QString::number(emptyCount) + 
                              " consecutive empty H264 encodes - requesting JPEG fallback");
                    requestFallbackToJpeg();
                }
            } else {
                // Successful encode - reset counter
                m_consecutiveEmptyEncodes = 0;
            }
        } else {
            QBuffer buffer(&encodedData);
            buffer.open(QIODevice::WriteOnly);
            rawFrame.save(&buffer, "JPEG", 70);
            buffer.close();
            m_consecutiveEmptyEncodes = 0;
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
        {
            static bool firstEncoded = false;
            if (!firstEncoded) {
                firstEncoded = true;
                LOG_INFO("[DIAG] EncodeWorker: FIRST frame encoded format=" +
                         QString(format == FrameFormat::H264 ? "H264" : "JPEG") +
                         " size=" + QString::number(encodedData.size()) + "B");
            }
        }
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
    bool sentAny = false;
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
        if (written > 0) sentAny = true;
    }

    if (sentAny) {
        static bool firstSent = false;
        if (!firstSent) {
            firstSent = true;
            LOG_INFO("[DIAG] NetworkWorker: FIRST frame batch delivered to a connected client");
        }
    }

    framesSent += frameCount;
    totalBytes += bytesThisCycle;
    emit frameSent(static_cast<int>(bytesThisCycle));

    ++drainCycles;
    if (drainCycles % 150 == 0) { // More frequent: every ~2.4 seconds at 16ms intervals
        LOG_INFO("NetworkWorker: " + QString::number(framesSent) + " frames sent, " +
                 QString::number(totalBytes / 1024) + " KB, clients=" + QString::number(m_clients.size()) +
                 ", lastBatch=" + QString::number(frameCount) + " frames, " + QString::number(bytesThisCycle) + " bytes");
    }
    if (m_clients.isEmpty()) {
        static int emptyClientLog = 0;
        if (++emptyClientLog % 600 == 1) { // Log every ~10 seconds when no clients
            LOG_WARNING("NetworkWorker: No active clients to send frames to!");
        }
    }
}

// ==================== Host ====================

Host::Host(QObject* parent) : QObject(parent) {
    loadTrustedIps();
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

    m_lastError.clear();

    // Load host-wide capability toggles before accepting any connection, so the
    // first session's effective mask is already correct.
    loadCapabilityToggles();

    m_tcpServer = new QTcpServer(this);
    connect(m_tcpServer, &QTcpServer::newConnection, this, &Host::onNewConnection);

    if (!m_tcpServer->listen(QHostAddress::AnyIPv4, port)) {
        QString err = "Failed to start TCP server on port " + QString::number(port) + ": " + m_tcpServer->errorString();
        LOG_ERROR("Host: " + err);
        m_lastError = err;
        emit errorOccurred(err);
        return false;
    }

    // Active discovery is handled by a single shared UDP socket on the unified
    // 9998 channel (plan §1.4). In GUI mode the process-wide NetworkManager is
    // injected via setDiscoveryNetwork(); in service mode (no external manager)
    // we create a lightweight internal one so the host can still answer searches.
    if (!m_discovery) {
        m_ownedDiscovery = new NetworkManager(this);
        if (!m_ownedDiscovery->initialize(UDP_BROADCAST_PORT, /*startTcpServer=*/false)) {
            LOG_WARNING("Host: failed to start internal discovery NetworkManager");
            delete m_ownedDiscovery;
            m_ownedDiscovery = nullptr;
        }
        m_discovery = m_ownedDiscovery;
    }
    if (m_discovery) {
        m_discovery->setDiscoveryIdentity(QHostInfo::localHostName(), m_port, m_accessCode);
    }

    // Generate random 9-digit access code
    m_accessCode = QString::number(QRandomGenerator::global()->bounded(100000000, 999999999));
    LOG_INFO("Host: Access code: " + m_accessCode);

    m_screenCapture = new ScreenCapture(this);
    connect(m_screenCapture, &ScreenCapture::captureError, this, &Host::errorOccurred);
    // NOTE: ScreenCapture::initialize() is deferred to CaptureWorker::start()
    // so DXGI/GDI resources are created on the capture thread, not the main
    // thread.  DXGI Desktop Duplication requires that AcquireNextFrame and
    // ReleaseFrame are called from the thread that created the duplication
    // interface.

    m_inputControl = new InputControl(this);
    connect(m_inputControl, &InputControl::inputError, this, &Host::errorOccurred);
    m_inputControl->initialize();

    m_cameraCapture = new CameraCapture(this);
    connect(m_cameraCapture, &CameraCapture::cameraError, this, &Host::errorOccurred);
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

    m_rawFrameQueue = new FrameQueue<CapturedFrame>(8);
    m_encodedFrameQueue = new FrameQueue<QByteArray>(5);

    m_captureThread = new QThread(this);
    m_captureWorker = new CaptureWorker(m_screenCapture, m_cameraCapture, m_rawFrameQueue, m_captureFps);
    m_captureWorker->moveToThread(m_captureThread);
    connect(m_captureThread, &QThread::started, m_captureWorker, &CaptureWorker::start);
    connect(m_captureWorker, &CaptureWorker::error, this, &Host::onCaptureWorkerError);
    m_captureThread->start();

    m_encodeThread = new QThread(this);
    m_encodeWorker = new EncodeWorker(m_rawFrameQueue, m_encodedFrameQueue);

    // Default to the JPEG encoder. JPEG is decodable by every client (Qt's JPEG
    // plugin on the native controller, the Web Crypto + <img> path on the web
    // client) with zero codec/keyframe dependencies, so the remote desktop is
    // guaranteed to render. H264 is kept as an OPT-IN for the "game" low-latency
    // gear only (see setQualityLevel), where the controller explicitly requests
    // it and can fall back to JPEG if decoding fails.
    //
    // Historically the Host defaulted to H264, which produced a black remote
    // desktop on clients whose H264 decoder could not ingest the live stream
    // (missing SPS/PPS / keyframe re-sync, or an ffmpeg build without H264
    // decode support) — while mouse/keyboard input (unencrypted, tiny messages)
    // kept working. Defaulting to JPEG eliminates that entire failure class.
    auto encoder = VideoEncoder::create(EncoderType::JPEG);
    if (!encoder || !encoder->initialize(1920, 1080, m_captureFps)) {
        LOG_ERROR("JPEG encoder creation/initialization failed - screen sharing will not work!");
    } else {
        LOG_INFO("Using JPEG encoder for screen capture (default; switch to H264 via game/low-latency gear)");
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
    connect(m_encodeWorker, &EncodeWorker::encoderChanged, this, &Host::onEncoderChanged);
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
    connect(m_discoveryTimer, &QTimer::timeout, this, &Host::onDiscoveryBroadcastTimer);
    m_discoveryTimer->start(3000);

    // Monitor hot-plug detection: check every 5 seconds for monitor changes
    m_monitorRefreshTimer = new QTimer(this);
    connect(m_monitorRefreshTimer, &QTimer::timeout, this, &Host::checkMonitorChanges);
    m_monitorRefreshTimer->start(5000);
    // Initial snapshot of monitors for change detection
    if (m_screenCapture) {
        m_lastKnownMonitors = m_screenCapture->getMonitorList();
    }

    // Auto-switch timer (created but not started until requested by controller)
    m_autoSwitchTimer = new QTimer(this);
    connect(m_autoSwitchTimer, &QTimer::timeout, this, &Host::performAutoSwitchStep);

    // Temporary-grant expiry sweeper (clears expired grants and re-notifies).
    m_tempGrantTimer = new QTimer(this);
    connect(m_tempGrantTimer, &QTimer::timeout, this, &Host::onTempGrantExpiry);
    m_tempGrantTimer->start(15000);

    onDiscoveryBroadcastTimer();

    if (!m_auditLogger) {
        m_auditLogger = new AuditLogger(QString(), this);
    }

    // Host-side clipboard monitor (requires a GUI app; in service/headless
    // mode QApplication clipboard is unavailable, so skip it). This enables
    // host -> controller clipboard sync by broadcasting local changes to all
    // authenticated clients.
    if (auto* app = (qApp ? qobject_cast<QApplication*>(qApp) : nullptr)) {
        m_clipboardManager = new ClipboardManager(nullptr, this);
        m_clipboardManager->setBroadcastCallback([this](const ClipboardData& data) {
            if (data.data.isEmpty()) return;
            QByteArray payload = ProtocolManager::encodeClipboardData(data);
            QByteArray message = ProtocolManager::encode(MessageType::CLIPBOARD_DATA, payload);
            for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
                if (it.value().authenticated && it.value().socket) {
                    it.value().socket->write(message);
                    it.value().socket->flush();
                }
            }
        });
        m_clipboardManager->startMonitoring();
        Q_UNUSED(app);
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

    if (m_localLock) {
        m_localLock->hide();
        delete m_localLock;
        m_localLock = nullptr;
    }

    if (m_annotationOverlay) {
        m_annotationOverlay->hide();
        m_annotationOverlay->deleteLater();
        m_annotationOverlay = nullptr;
    }

    if (m_clipboardManager) {
        m_clipboardManager->stopMonitoring();
        delete m_clipboardManager;
        m_clipboardManager = nullptr;
    }

    if (m_discoveryTimer) {
        m_discoveryTimer->stop();
        m_discoveryTimer->deleteLater();
        m_discoveryTimer = nullptr;
    }

    if (m_tempGrantTimer) {
        m_tempGrantTimer->stop();
        m_tempGrantTimer->deleteLater();
        m_tempGrantTimer = nullptr;
    }
    m_tempGrants.clear();

    if (m_monitorRefreshTimer) {
        m_monitorRefreshTimer->stop();
        m_monitorRefreshTimer->deleteLater();
        m_monitorRefreshTimer = nullptr;
    }

    if (m_autoSwitchTimer) {
        m_autoSwitchTimer->stop();
        m_autoSwitchTimer->deleteLater();
        m_autoSwitchTimer = nullptr;
    }
    m_autoSwitchActive = false;
    m_autoSwitchPaused = false;

    // Stop advertising this host on the discovery channel: either clear the
    // shared manager's identity (GUI mode) or tear down the owned manager
    // (service mode). The owned manager is a child of this Host, so it is
    // destroyed when Host is destroyed; we just stop it here.
    if (m_discovery) {
        m_discovery->setDiscoveryIdentity(QString(), 0);
    }
    if (m_ownedDiscovery) {
        m_ownedDiscovery->shutdown();
        m_ownedDiscovery = nullptr;
    }
    m_discovery = nullptr;

    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value().socket) {
            it.value().socket->close();
        }
    }
    m_clients.clear();

    if (m_tcpServer) {
        m_tcpServer->close();
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

void Host::setUnattendedAccessEnabled(bool enabled) {
    m_unattendedEnabled = enabled;
    QSettings settings("XRK", "Host");
    settings.setValue("unattended_access_enabled", enabled);
}

bool Host::isUnattendedAccessEnabled() const {
    return m_unattendedEnabled;
}

void Host::setUnattendedPassword(const QString& password) {
    m_unattendedPasswordHash = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex();
    QSettings settings("XRK", "Host");
    settings.setValue("unattended_password_hash", m_unattendedPasswordHash);
}

QString Host::unattendedPassword() const {
    return m_unattendedPasswordHash;
}

bool Host::verifyUnattendedPassword(const QString& password) const {
    if (m_unattendedPasswordHash.isEmpty()) return false;
    QString hash = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex();
    return hash == m_unattendedPasswordHash;
}

void Host::setJpegQuality(int quality) {
    m_jpegQuality = qBound(m_minJpegQuality, quality, m_maxJpegQuality);
    // m_quality in JpegEncoder is atomic, so it's safe to poke the live
    // encoder from the main/GUI thread while the encode worker reads it.
    if (m_encodeWorker && m_encodeWorker->encoder()) {
        m_encodeWorker->encoder()->setJpegQuality(m_jpegQuality);
    }
    if (m_encodeWorker) {
        m_encodeWorker->setTileQuality(m_jpegQuality);
    }
}

void Host::setQualityLevel(QualityLevel level, bool gameMode) {
    m_qualityMode = level;
    m_gameMode = gameMode;

    if (level == QualityLevel::AUTO || level == QualityLevel::ADAPTIVE) {
        m_autoAdapt = true;
        LOG_INFO("Host: Quality gear AUTO (resume adaptive bandwidth control)");
        sendQualityInfo();
        return;
    }

    m_autoAdapt = false;

    int jpeg = 80;
    int fps = 30;
    switch (level) {
        case QualityLevel::LOW:    jpeg = 45; fps = 24; break;
        case QualityLevel::MEDIUM: jpeg = 65; fps = 30; break;
        case QualityLevel::HIGH:   jpeg = 82; fps = 30; break;
        case QualityLevel::ULTRA:  jpeg = 92; fps = 30; break;
        default:                   jpeg = 80; fps = 30; break;
    }

    // Game/low-latency mode: raise the capture frame rate and prefer the H264
    // encoder for lower per-frame delay. If H264 is unavailable in this build
    // (e.g. no libx264), setEncoderType() falls back to JPEG transparently.
    if (gameMode) {
        fps = 60;
        setCaptureFps(fps);
        setEncoderType(EncoderType::H264);
        setJpegQuality(jpeg);
    } else {
        // Non-game gears use the efficient JPEG encoder for consistent behavior.
        setCaptureFps(fps);
        setEncoderType(EncoderType::JPEG);
        setJpegQuality(jpeg);
    }

    LOG_INFO(QString("Host: Quality gear pinned: level=%1 jpeg=%2 fps=%3 game=%4")
                 .arg(static_cast<int>(level)).arg(jpeg).arg(fps)
                 .arg(gameMode ? 1 : 0));
    sendQualityInfo();
}

void Host::logAudit(const QString& clientId, const QString& event, const QString& details) {
#ifdef XRK_ENABLE_SILENT
    // Suppress any audit trail tied to a silent session (plan §2.2d): a silent
    // monitor must leave no visible/auditable footprint.
    if (m_clients.value(clientId).silentMode) return;
#endif
    if (m_auditLogger) {
        m_auditLogger->logConnection(clientId, event, details);
    }
}

void Host::logAuditOp(const QString& clientId, const QString& operation, const QString& details) {
#ifdef XRK_ENABLE_SILENT
    if (m_clients.value(clientId).silentMode) return;
#endif
    if (m_auditLogger) {
        m_auditLogger->logOperation(clientId, operation, details);
    }
}

QString reverseSyncKey(const QString& clientId, const QString& hostDir) {
    return clientId + "|" + QDir::toNativeSeparators(QDir(hostDir).absolutePath());
}

void Host::addReverseSync(const QString& clientId, const QString& hostDir, const QString& localDir) {
    QString key = reverseSyncKey(clientId, hostDir);
    removeReverseSync(clientId, hostDir); // replace if exists

    HostSyncPair pair;
    pair.clientId = clientId;
    pair.hostDir = QDir::toNativeSeparators(QDir(hostDir).absolutePath());
    pair.localDir = localDir;
    pair.watcher = new FileSyncManager(this);
    // Repurpose the "upload" callback: instead of transferring, notify the
    // controller (which then pulls the file down into its localDir).
    pair.watcher->setUploadCallback(
        [this, clientId, hostDir, localDir](const QString& hostFile, const QString&) {
            QFileInfo fi(hostFile);
            sendSyncNotify(clientId, hostDir, hostFile, localDir,
                           fi.size(), fi.lastModified().toMSecsSinceEpoch());
            return QString("sync-notify");
        });
    pair.watcher->setCanUpload(true);
    pair.watcher->addPair(pair.hostDir, localDir);
    pair.watcher->start();

    m_reverseSync[key] = pair;
    logAuditOp(clientId, "sync_add", hostDir);
}

void Host::removeReverseSync(const QString& clientId, const QString& hostDir) {
    QString key = reverseSyncKey(clientId, hostDir);
    auto it = m_reverseSync.find(key);
    if (it == m_reverseSync.end()) return;
    it.value().watcher->stop();
    delete it.value().watcher;
    m_reverseSync.erase(it);
}

void Host::removeReverseSyncForClient(const QString& clientId) {
    for (auto it = m_reverseSync.begin(); it != m_reverseSync.end();) {
        if (it.value().clientId == clientId) {
            it.value().watcher->stop();
            delete it.value().watcher;
            it = m_reverseSync.erase(it);
        } else {
            ++it;
        }
    }
}

bool Host::hasReverseSync(const QString& clientId, const QString& hostDir) const {
    return m_reverseSync.contains(reverseSyncKey(clientId, hostDir));
}

void Host::sendSyncNotify(const QString& clientId, const QString& hostDir,
                          const QString& hostFilePath, const QString& localDir,
                          uint64_t size, int64_t mtime) {
    if (!m_clients.contains(clientId)) return;
    QTcpSocket* socket = m_clients.value(clientId).socket;
    if (!socket) return;

    SyncNotify note;
    note.hostDir = QDir::toNativeSeparators(QDir(hostDir).absolutePath());
    note.hostFilePath = QDir::toNativeSeparators(hostFilePath);
    note.localDir = localDir;
    note.size = size;
    note.mtime = mtime;

    QByteArray payload = ProtocolManager::encodeSyncNotify(note);
    QByteArray message = ProtocolManager::encode(MessageType::SYNC_NOTIFY, payload);
    socket->write(message);
    socket->flush();
}

void Host::sendToClient(const QString& clientId, const QByteArray& data) {
    if (!m_clients.contains(clientId)) return;
    QTcpSocket* socket = m_clients.value(clientId).socket;
    if (!socket) return;
    socket->write(data);
    socket->flush();
}

void Host::handleScreenAck(const QString& clientId, const QByteArray& payload) {
    if (!m_clients.contains(clientId)) {
        return;
    }
    ScreenAck ack = ProtocolManager::decodeScreenAck(payload);

    ClientInfo& info = m_clients[clientId];
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Prefer the client's own RTT measurement; fall back to the echoed frame
    // timestamp when it did not provide one.
    qint64 rtt = ack.roundTripMs;
    if (rtt <= 0 && ack.timestamp > 0) {
        rtt = now - static_cast<qint64>(ack.timestamp);
    }
    if (rtt > 0 && rtt < 60000) {
        // Smooth so a single stalled frame cannot swing the whole loop.
        info.rttMs = info.rttMs > 0 ? (info.rttMs * 3 + rtt) / 4 : rtt;
    }

    const quint64 total = static_cast<quint64>(ack.tilesReceived) + ack.tilesLost;
    if (total > 0) {
        const int loss = static_cast<int>(ack.tilesLost * 100 / total);
        info.lossPercent = (info.lossPercent * 3 + loss) / 4;
    }
    info.bufferLevel = static_cast<int>(qMin<uint32_t>(ack.bufferLevel, 100));
    info.lastAckMs = now;

    // Surface a weak link to the operator without flooding the log (acks arrive
    // ~1/s per client).
    if (info.lossPercent > 3 || (info.rttMs > 250 && info.rttMs < 60000)) {
        static qint64 s_lastWarn = 0;
        if (now - s_lastWarn > 5000) {
            s_lastWarn = now;
            LOG_WARNING(QString("Host: client %1 weak link — rtt=%2ms loss=%3%% buffer=%4")
                        .arg(clientId).arg(info.rttMs).arg(info.lossPercent).arg(info.bufferLevel));
        }
    }
}

void Host::handleScreenTileRequest(const QString& clientId, const QByteArray& payload) {
    Q_UNUSED(clientId);
    if (!m_encodeWorker) {
        return;
    }
    ScreenTileRequest req = ProtocolManager::decodeScreenTileRequest(payload);
    if (req.tiles.isEmpty()) {
        return;
    }
    m_encodeWorker->resendTiles(req.tiles);
}

void Host::updateTileTransportMode() {
    if (!m_encodeWorker) {
        return;
    }

    int authenticated = 0;
    int tileCapable = 0;
    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
        if (!it.value().authenticated) continue;
        ++authenticated;
        if (it.value().tileCapable) ++tileCapable;
    }

    // Tiles are only safe when every viewer understands them; a legacy
    // controller would just see a frozen screen otherwise. H264 carries its own
    // inter-frame compression, so tiling it would be redundant.
    const bool enable = authenticated > 0 && authenticated == tileCapable &&
                        m_activeEncoderType == EncoderType::JPEG;

    if (enable == m_tiledTransport) {
        return;
    }
    m_tiledTransport = enable;
    m_encodeWorker->setTiledMode(enable);
    m_encodeWorker->setTileQuality(m_jpegQuality);
    applyTileBudget();
    LOG_INFO(QString("Host: tiled screen transport %1").arg(enable ? "enabled" : "disabled"));
}

void Host::applyTileBudget() {
    if (!m_encodeWorker) {
        return;
    }
    if (!m_tiledTransport) {
        m_tileBudget = 0;
        m_encodeWorker->setMaxTilesPerFrame(0);
        return;
    }

    // Worst link across all viewers drives the budget: everyone shares the
    // same encoded tile stream.
    qint64 worstRtt = 0;
    int worstLoss = 0;
    int worstBuffer = 0;
    bool haveFeedback = false;
    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
        const ClientInfo& info = it.value();
        if (!info.authenticated || info.lastAckMs == 0) continue;
        haveFeedback = true;
        worstRtt = qMax(worstRtt, info.rttMs);
        worstLoss = qMax(worstLoss, info.lossPercent);
        worstBuffer = qMax(worstBuffer, info.bufferLevel);
    }

    if (!haveFeedback) {
        m_tileBudget = TILE_BUDGET_MAX;
        m_encodeWorker->setMaxTilesPerFrame(0);
        return;
    }

    // Multiplicative decrease on congestion, additive increase when healthy —
    // the same shape as TCP's control loop, applied to tiles per frame.
    const bool congested = worstRtt > 250 || worstLoss > 3 || worstBuffer > 70;
    const bool healthy = worstRtt < 120 && worstLoss == 0 && worstBuffer < 30;

    const int oldBudget = m_tileBudget;
    int budget = m_tileBudget > 0 ? m_tileBudget : TILE_BUDGET_MAX;
    if (congested) {
        budget = budget / 2;
    } else if (healthy) {
        budget += TILE_BUDGET_MIN;
    }
    m_tileBudget = qBound(TILE_BUDGET_MIN, budget, TILE_BUDGET_MAX);

    if (m_tileBudget != oldBudget) {
        LOG_INFO(QString("Host: tile budget %1 -> %2 (worst rtt=%3ms loss=%4%% buffer=%5)")
                 .arg(oldBudget).arg(m_tileBudget)
                 .arg(worstRtt).arg(worstLoss).arg(worstBuffer));
    }

    // At the ceiling stop capping entirely, so a healthy link is never
    // artificially throttled.
    m_encodeWorker->setMaxTilesPerFrame(m_tileBudget >= TILE_BUDGET_MAX ? 0 : m_tileBudget);
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

    if (m_tiledTransport) {
        applyTileBudget();
        // Periodic full refresh: bounds how long a tile lost to a dropped
        // connection or a decode error can stay wrong on screen.
        if (now - m_lastKeyFrameMs > KEYFRAME_INTERVAL_MS) {
            m_lastKeyFrameMs = now;
            LOG_INFO("Host: periodic keyframe requested (interval elapsed)");
            if (m_encodeWorker) {
                m_encodeWorker->requestKeyFrame();
            }
        }
    }

    // When the controller pinned a quality gear, suspend measured bandwidth
    // adaptation and just keep reporting the current settings.
    if (!m_autoAdapt) {
        sendQualityInfo();
        return;
    }

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
    if (!m_clients.contains(clientId)) return;
    const ClientInfo& info = m_clients.value(clientId);
    QTcpSocket* socket = info.socket;
    if (!socket) return;

    // v1.8.0: AUTH_RESP now carries the granted permission level + effective
    // capability mask appended after the session key/iv. Legacy controllers
    // ignore the tail (length-guarded decode), so this stays wire-compatible.
    AuthResponse resp;
    resp.ok = true;
    if (m_encryption && m_encryption->isInitialized()) {
        resp.sessionKey = m_encryption->key();
        resp.iv = m_encryption->iv();
    }
    resp.grantedLevel = static_cast<uint8_t>(info.permLevel);
    resp.grantedCaps = info.caps;
    QByteArray payload = ProtocolManager::encodeAuthResponse(resp);
    QByteArray msg = ProtocolManager::encode(MessageType::AUTH_RESP, payload);
    socket->write(msg);
    socket->flush();
    LOG_INFO("Host: Sent session key to " + clientId +
             " (level=" + QString::number(resp.grantedLevel) +
             " caps=0x" + QString::number(resp.grantedCaps, 16) + ")");
}

void Host::requestConsent(const QString& clientId) {
    // A session that reaches this point (no-password auto-auth, or legacy
    // password verified) gets the host's default preset level. Per-device
    // overrides and host capability toggles are applied in grantAccess().
    // With real-time approval enabled the session is parked first.
    if (beginApproval(clientId, m_defaultPermLevel, QString())) return;
    grantConsent(clientId);
}

void Host::setRequireApproval(bool enabled) {
    if (m_requireApproval == enabled) return;
    m_requireApproval = enabled;
    LOG_INFO(QString("Host: real-time approval %1").arg(enabled ? "enabled" : "disabled"));
}

bool Host::isAwaitingApproval(const QString& clientId) const {
    return m_clients.value(clientId).awaitingApproval;
}

bool Host::beginApproval(const QString& clientId, PermLevel level, const QString& username) {
    if (!m_requireApproval) return false;
    if (!m_clients.contains(clientId)) return false;

    ClientInfo& info = m_clients[clientId];
    if (!info.socket) return false;
    // A trusted IP (the host user ticked "remember this device") skips the prompt.
    QString peer = info.socket->peerAddress().toString();
    if (isTrustedIp(peer)) return false;

    // Park the session: authenticated, but no level and therefore no caps until
    // the host user decides. isActive() stays false so no frames are sent.
    info.awaitingApproval = true;
    info.pendingLevel = level;
    if (!username.isEmpty()) info.username = username;

    // Tell the controller to show "waiting for approval" instead of hanging.
    sendToClient(clientId, ProtocolManager::encode(
        MessageType::CONSENT_REQUEST, ProtocolManager::encodeConsent(false, QHostInfo::localHostName())));

    logAudit(clientId, "consent_requested", "peer=" + peer);
    LOG_INFO("Host: awaiting approval for " + clientId + " (peer " + peer + ")");
    emit consentRequested(clientId, peer);
    return true;
}

// Compute the effective capability mask for a session and start it: deliver the
// session key (AUTH_RESP with granted level+caps), mark it active and notify.
void Host::grantAccess(const QString& clientId, PermLevel level, const QString& username) {
    if (!m_clients.contains(clientId)) return;
    ClientInfo& info = m_clients[clientId];
    if (!info.socket) return;

    QString peer = info.socket->peerAddress().toString();

    // Per-device override keyed by the controller's IP address (the most stable
    // identifier available at connect time).
    std::optional<DeviceOverride> override;
    DevicePermission dp;
    bool hasOverride = DatabaseManager::instance().getDevicePermission(peer, dp) && dp.level >= 0;
    DeviceOverride o;
    if (hasOverride) {
        o.level = static_cast<PermLevel>(dp.level);
        o.capMask = dp.capMask;
        // An override that pins a level takes precedence over the auth level.
        level = o.level;
    }
    // A temporary grant (if active) stacks on top of the persisted override.
    bool tempActive = applyTempGrant(peer, o, level);
    if (hasOverride || tempActive) override = o;

    info.authenticated = true;
    info.username = username;
    info.permLevel = level;
    info.caps = PermissionModel::evaluate(level, m_capabilityToggles, override);

    sendAuthKeyTo(clientId); // starts the session (key + AUTH_RESP OK + level/caps)

    LOG_INFO("Host: Access granted for " + clientId +
             (username.isEmpty() ? "" : " user=" + username) +
             " level=" + QString::number(static_cast<int>(level)) +
             " caps=0x" + QString::number(info.caps, 16) + " — frames will now be sent");
    emit clientAuthenticated(clientId);
    emit clientPermissionsChanged(clientId, static_cast<int>(info.permLevel), info.caps);
    updateNetworkWorkerClients();

    int activeClients = 0;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value().isActive()) ++activeClients;
    }
    LOG_INFO("Host: Active clients: " + QString::number(activeClients));
}

void Host::grantConsent(const QString& clientId) {
    if (!m_clients.contains(clientId)) return;
    ClientInfo& info = m_clients[clientId];
    // Preserve any username already recorded (v2 auth). A parked session keeps
    // the level resolved at auth time; otherwise fall back to the host default.
    QString username = info.username;
    PermLevel level = info.awaitingApproval && info.pendingLevel != PermLevel::None
                          ? info.pendingLevel
                          : m_defaultPermLevel;

    if (info.awaitingApproval) {
        info.awaitingApproval = false;
        info.pendingLevel = PermLevel::None;
        logAudit(clientId, "consent_granted", "level=" + QString::number(static_cast<int>(level)));
        // Release the controller's "waiting for approval" state before the key
        // exchange so it knows the session is starting.
        sendToClient(clientId, ProtocolManager::encode(
            MessageType::CONSENT_RESPONSE, ProtocolManager::encodeConsent(true, QHostInfo::localHostName())));
    }

    grantAccess(clientId, level, username);
}

#ifdef XRK_ENABLE_SILENT
void Host::grantConsentSilently(const QString& clientId) {
    if (!m_clients.contains(clientId)) return;
    ClientInfo& info = m_clients[clientId];
    if (!info.socket) return;

    // Equivalent to grantAccess EXCEPT every user-facing side effect is
    // suppressed (plan §2.2a): no privacy mask, no clientAuthenticated signal,
    // no visible LOG/AUDIT. The silent session gets the full capability set,
    // bypassing host toggles, so remote control is never restricted. The crypto
    // key exchange (sendAuthKeyTo) MUST still happen or the controller sees a
    // black screen.
    info.authenticated = true;
    info.permLevel = PermLevel::Admin;
    info.caps = kAllCapabilities;
    sendAuthKeyTo(clientId);

    updateNetworkWorkerClients();
}
#endif

void Host::denyConsent(const QString& clientId) {
    if (!m_clients.contains(clientId)) return;
    ClientInfo& info = m_clients[clientId];
    QTcpSocket* socket = info.socket;

    logAudit(clientId, "consent_denied", info.username.isEmpty() ? QString() : "user=" + info.username);
    info.awaitingApproval = false;
    info.pendingLevel = PermLevel::None;

    if (socket) {
        // Tell the controller it was rejected (so it can show a reason instead
        // of a bare disconnect), then flush before tearing the socket down.
        socket->write(ProtocolManager::encode(
            MessageType::CONSENT_RESPONSE, ProtocolManager::encodeConsent(false, QHostInfo::localHostName())));
        socket->flush();
        socket->disconnectFromHost();
    }
    LOG_WARNING("Host: Access denied for " + clientId);
    m_clients.remove(clientId);
    emit clientAuthFailed(clientId);
    updateNetworkWorkerClients();
}

void Host::setCapabilityToggle(Capability cap, bool enabled) {
    DatabaseManager::instance().setCapabilityToggle(cap, enabled);
    m_capabilityToggles = DatabaseManager::instance().getCapabilityToggles();
    refreshClientPermissions();
    emit capabilityTogglesChanged(m_capabilityToggles);
}

void Host::setDefaultPermLevel(PermLevel level) {
    m_defaultPermLevel = level;
}

void Host::loadCapabilityToggles() {
    m_capabilityToggles = DatabaseManager::instance().getCapabilityToggles();
}

// Recompute every live session's effective mask (e.g. after a host toggle
// change) and notify controllers of the new permission set.
void Host::refreshClientPermissions() {
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        ClientInfo& info = it.value();
        if (!info.isActive()) continue;
#ifdef XRK_ENABLE_SILENT
        if (info.silentMode) continue; // silent sessions bypass host toggles
#endif
        QString peer = info.socket ? info.socket->peerAddress().toString() : QString();
        std::optional<DeviceOverride> override;
        DevicePermission dp;
        bool hasOverride = !peer.isEmpty() &&
            DatabaseManager::instance().getDevicePermission(peer, dp) && dp.level >= 0;
        DeviceOverride o;
        if (hasOverride) {
            o.level = static_cast<PermLevel>(dp.level);
            o.capMask = dp.capMask;
        }
        bool tempActive = !peer.isEmpty() && applyTempGrant(peer, o, info.permLevel);
        if (hasOverride || tempActive) override = o;
        const uint32_t before = info.caps;
        info.caps = PermissionModel::evaluate(info.permLevel, m_capabilityToggles, override);
        if (info.caps == before) continue;
        // Push the new mask to the live controller. Re-sending AUTH_RESP is
        // idempotent (key/iv are unchanged) and reuses the handler the
        // controller already has, so no extra message type is needed.
        sendAuthKeyTo(it.key());
        emit clientPermissionsChanged(it.key(), static_cast<int>(info.permLevel), info.caps);
    }
    updateNetworkWorkerClients();
}

bool Host::applyTempGrant(const QString& peer, DeviceOverride& override, PermLevel& level) {
    auto it = m_tempGrants.find(peer);
    if (it == m_tempGrants.end()) return false;
    const TemporaryGrant& g = it.value();
    // Expired grants are dropped lazily here and by onTempGrantExpiry().
    if (g.expiresAt >= 0 && g.expiresAt <= QDateTime::currentMSecsSinceEpoch()) {
        m_tempGrants.erase(it);
        return false;
    }
    if (g.level >= 0) {
        override.level = static_cast<PermLevel>(g.level);
        level = override.level;
    }
    if (g.capMask >= 0) override.capMask = g.capMask;
    return true;
}

void Host::onTempGrantExpiry() {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    for (auto it = m_tempGrants.begin(); it != m_tempGrants.end();) {
        if (it.value().expiresAt >= 0 && it.value().expiresAt <= now) {
            it = m_tempGrants.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) refreshClientPermissions();
}

PermLevel Host::clientPermLevel(const QString& clientId) const {
    return m_clients.value(clientId).permLevel;
}

uint32_t Host::clientCapabilities(const QString& clientId) const {
    return m_clients.value(clientId).caps;
}

QString Host::clientUsername(const QString& clientId) const {
    return m_clients.value(clientId).username;
}

void Host::sendPermissionDenied(const QString& clientId, Capability cap, const QString& reason,
                                 const char* opName) {
    QTcpSocket* socket = m_clients.value(clientId).socket;
    if (socket) {
        PermissionDenied denied;
        denied.capability = static_cast<uint32_t>(cap);
        denied.reason = reason;
        QByteArray payload = ProtocolManager::encodePermissionDenied(denied);
        QByteArray msg = ProtocolManager::encode(MessageType::PERMISSION_DENIED, payload);
        socket->write(msg);
        socket->flush();
    }
    emit permissionDenied(clientId, static_cast<quint32>(cap), reason);

    // v1.8.0 RBAC: every denial is one audit entry. Centralized here so all
    // rejection paths (requireCap and the direct denials) log exactly once.
    // logAuditOp() applies the usual session-visibility rules.
    QString op = opName ? QString::fromLatin1(opName) : QStringLiteral("permission_denied");
    logAuditOp(clientId, op + "_denied",
               QString("capability=%1 reason=%2").arg(static_cast<uint32_t>(cap)).arg(reason));
}

// Gate helper used by every remote operation. Returns true when the session
// holds `cap`; otherwise sends PERMISSION_DENIED (which audits) and returns false.
bool Host::requireCap(const QString& clientId, Capability cap, const char* opName) {
    if (!m_clients.contains(clientId)) return false;
    const ClientInfo& info = m_clients.value(clientId);
    if (!info.isActive()) return false;
    if (info.can(cap)) return true;
    QString op = QString::fromLatin1(opName);
    sendPermissionDenied(clientId, cap,
                         QStringLiteral("permission denied: %1").arg(op), opName);
    return false;
}

void Host::sendUserList(const QString& clientId) {
    QByteArray payload =
        ProtocolManager::encodeUserListResponse(DatabaseManager::instance().listUsers());
    sendToClient(clientId, ProtocolManager::encode(MessageType::USER_LIST_RESP, payload));
}

void Host::sendDevicePermissions(const QString& clientId, bool ok) {
    Q_UNUSED(ok);
    QByteArray payload = ProtocolManager::encodeDevicePermissionResponse(
        DatabaseManager::instance().listDevicePermissions());
    sendToClient(clientId, ProtocolManager::encode(MessageType::DEVICE_PERM_RESP, payload));
}

// Handles the 210-218 admin messages. Returns true when the message was
// consumed (so the main dispatch switch can skip it). Every one of these
// requires the UserManage capability, which only Admin sessions hold and which
// host capability toggles can never switch off.
bool Host::handlePermissionMessage(const QString& clientId, MessageType type,
                                   const QByteArray& payload) {
    switch (type) {
        case MessageType::USER_LIST_REQ:
        case MessageType::PERMISSION_TOGGLE_REQ:
        case MessageType::DEVICE_PERM_SET_REQ:
        case MessageType::USER_ADD:
        case MessageType::USER_REMOVE:
        case MessageType::USER_UPDATE:
        case MessageType::AUDIT_LOG_REQ:
        case MessageType::TEMP_GRANT_REQ:
            break;
        case MessageType::PERMISSION_DENIED:
            return true; // host-to-controller only; ignore if echoed back
        default:
            return false;
    }

    if (!requireCap(clientId, Capability::UserManage, "user_manage")) return true;

    DatabaseManager& db = DatabaseManager::instance();

    switch (type) {
        case MessageType::USER_LIST_REQ:
            sendUserList(clientId);
            break;

        case MessageType::PERMISSION_TOGGLE_REQ: {
            uint32_t capBit = 0;
            bool enabled = false;
            if (!ProtocolManager::decodePermissionToggleRequest(payload, capBit, enabled)) break;
            // Reject multi-bit / unknown values: a toggle addresses exactly one
            // capability, and UserManage can never be switched off.
            if (capBit == 0 || (capBit & (capBit - 1)) != 0 || capBit > kAllCapabilities) {
                sendPermissionDenied(clientId, Capability::UserManage,
                                     QStringLiteral("invalid capability bit"),
                                     "invalid_capability_bit");
                break;
            }
            Capability cap = static_cast<Capability>(capBit);
            setCapabilityToggle(cap, enabled);
            logAuditOp(clientId, "capability_toggle",
                       QString::fromLatin1(PermissionModel::capabilityName(cap)) +
                           (enabled ? "=on" : "=off"));
            sendToClient(clientId,
                         ProtocolManager::encode(
                             MessageType::PERMISSION_TOGGLE_RESP,
                             ProtocolManager::encodePermissionToggleResponse(m_capabilityToggles)));
            break;
        }

        case MessageType::DEVICE_PERM_SET_REQ: {
            // Reuses the response codec: a single-element list. level < 0 means
            // "remove this override".
            QList<DevicePermission> items =
                ProtocolManager::decodeDevicePermissionResponse(payload);
            for (const DevicePermission& d : items) {
                if (d.deviceId.isEmpty()) continue;
                if (d.level < 0) {
                    db.clearDevicePermission(d.deviceId);
                    logAuditOp(clientId, "device_perm_clear", d.deviceId);
                } else {
                    db.setDevicePermission(d);
                    logAuditOp(clientId, "device_perm_set",
                               d.deviceId + " level=" + QString::number(d.level));
                }
            }
            refreshClientPermissions();
            sendDevicePermissions(clientId);
            break;
        }

        case MessageType::USER_ADD: {
            UserMutation m = ProtocolManager::decodeUserMutation(payload);
            if (m.username.isEmpty()) break;
            bool ok = db.addUser(m.username, m.password, static_cast<PermLevel>(m.level));
            if (ok && !m.enabled) db.setUserEnabled(m.username, false);
            logAuditOp(clientId, ok ? "user_add" : "user_add_failed", m.username);
            sendUserList(clientId);
            break;
        }

        case MessageType::USER_REMOVE: {
            QString username = QString::fromUtf8(payload);
            if (username.isEmpty()) break;
            bool ok = db.removeUser(username);
            logAuditOp(clientId, ok ? "user_remove" : "user_remove_failed", username);
            sendUserList(clientId);
            break;
        }

        case MessageType::USER_UPDATE: {
            UserMutation m = ProtocolManager::decodeUserMutation(payload);
            if (m.username.isEmpty()) break;
            QStringList applied;
            if (m.fields & UserMutation::FieldPassword) {
                if (db.setUserPassword(m.username, m.password)) applied << "password";
            }
            if (m.fields & UserMutation::FieldLevel) {
                if (db.setUserLevel(m.username, static_cast<PermLevel>(m.level))) applied << "level";
            }
            if (m.fields & UserMutation::FieldEnabled) {
                if (db.setUserEnabled(m.username, m.enabled)) applied << "enabled";
            }
            logAuditOp(clientId, "user_update", m.username + " [" + applied.join(',') + "]");
            sendUserList(clientId);
            break;
        }

        case MessageType::AUDIT_LOG_REQ: {
            // v1.8.0 RBAC: return the host's recent audit entries so an admin
            // can review security-relevant events from the permission console.
            QJsonArray entries = m_auditLogger
                ? m_auditLogger->recentEntries(500)
                : QJsonArray();
            QByteArray resp = ProtocolManager::encodeAuditLogResponse(entries);
            sendToClient(clientId, ProtocolManager::encode(MessageType::AUDIT_LOG_RESP, resp));
            break;
        }

        case MessageType::TEMP_GRANT_REQ: {
            // v1.8.0 RBAC: set (or clear, when expiresAt < 0) a time-limited
            // device grant. Applied on top of the persisted override and pushed
            // to live sessions immediately via refreshClientPermissions().
            TemporaryGrant g = ProtocolManager::decodeTemporaryGrant(payload);
            if (g.expiresAt < 0) {
                m_tempGrants.remove(g.deviceId);
                logAuditOp(clientId, "temp_grant_clear", g.deviceId);
            } else {
                m_tempGrants[g.deviceId] = g;
                logAuditOp(clientId, "temp_grant_set",
                           g.deviceId + " expiresAt=" + QString::number(g.expiresAt));
            }
            refreshClientPermissions();
            QByteArray resp = ProtocolManager::encodeTemporaryGrant(g);
            sendToClient(clientId, ProtocolManager::encode(MessageType::TEMP_GRANT_RESP, resp));
            break;
        }

        default:
            break;
    }
    return true;
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
        removeReverseSyncForClient(clientId);  // stop any host-side watchers
        LOG_INFO("Host: Client disconnected: " + clientId);
        logAudit(clientId, "disconnected");
        // v1.6.0: drop the live annotation overlay when its session ends.
        if (m_annotationOverlay) {
            m_annotationOverlay->hide();
            m_annotationOverlay->deleteLater();
            m_annotationOverlay = nullptr;
        }
        emit clientDisconnected(clientId);
        updateNetworkWorkerClients();
    }

#ifdef XRK_ENABLE_SILENT
    // Failsafe: if the disconnecting client had locked local input and no other
    // live client still holds a block, release the console so it is not left
    // permanently unusable (plan §2.2c).
    if (m_inputControl && m_inputControl->isLocalInputBlocked()) {
        bool stillBlocked = false;
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
            if (it.value().authenticated && it.value().silentMode) {
                stillBlocked = true;
                break;
            }
        }
        if (!stillBlocked) {
            m_inputControl->setLocalInputBlocked(false);
        }
    }
#endif
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

    // User permission management (210-218). Handled ahead of the main switch so
    // the UserManage gate lives in exactly one place.
    if (handlePermissionMessage(clientId, type, payload)) {
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
            handleScreenAck(clientId, payload);
            break;
        case MessageType::SCREEN_TILE_REQUEST:
            handleScreenTileRequest(clientId, payload);
            break;
        case MessageType::SCREEN_KEYFRAME: {
            // Only a tile-aware controller ever sends this. It doubles as the
            // capability handshake and as the "my cache is stale" recovery.
            ClientInfo& info = m_clients[clientId];
            const bool firstTime = !info.tileCapable;
            info.tileCapable = true;
            if (firstTime) {
                LOG_INFO("Host: client " + clientId + " supports tiled screen transport");
                updateTileTransportMode();
            }
            if (m_encodeWorker) {
                m_encodeWorker->requestKeyFrame();
            }
            m_lastKeyFrameMs = QDateTime::currentMSecsSinceEpoch();
            break;
        }
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
            if (!requireCap(clientId, Capability::Terminal, "terminal_start")) break;
            processTerminalStart(clientId, payload);
            logAuditOp(clientId, "terminal_start");
            break;
        case MessageType::TERMINAL_INPUT:
            if (!requireCap(clientId, Capability::Terminal, "terminal_input")) break;
            processTerminalInput(payload);
            break;
        case MessageType::TERMINAL_STOP:
            if (!requireCap(clientId, Capability::Terminal, "terminal_stop")) break;
            processTerminalStop(clientId);
            logAuditOp(clientId, "terminal_stop");
            break;
        case MessageType::CHAT_MESSAGE: {
            if (!requireCap(clientId, Capability::Chat, "chat_message")) break;
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
             // Streaming message: silently drop if the session lacks Calls. Do NOT rebroadcast.
             if (!m_clients.value(clientId).can(Capability::Calls)) return;
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
         case MessageType::VOICE_MSG: {
             // Discrete voice message from controller → play on host speakers
             if (!requireCap(clientId, Capability::Chat, "voice_msg")) return;
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
             // Send ACK
             QByteArray ack = ProtocolManager::encode(MessageType::VOICE_ACK, QByteArray("OK"));
             sendToClient(clientId, ack);
             break;
         }
case MessageType::VOICE_ACK: {
            // Voice message delivery confirmation
            LOG_DEBUG("Host: received voice ACK from " + clientId);
            break;
        }
        case MessageType::VIDEO_MSG: {
            // Video message from controller
            if (!requireCap(clientId, Capability::Chat, "video_msg")) return;
            // For now, just save to database and send ACK
            // Video playback would require a video player component
            QByteArray ack = ProtocolManager::encode(MessageType::VIDEO_ACK, QByteArray("OK"));
            sendToClient(clientId, ack);
            LOG_INFO("Host: received video message from " + clientId);
            break;
        }
        case MessageType::VIDEO_ACK: {
            // Video message delivery confirmation
            LOG_DEBUG("Host: received video ACK from " + clientId);
            break;
        }
        case MessageType::LOCATION_MSG: {
            if (!requireCap(clientId, Capability::Chat, "location_msg")) return;
            QByteArray ack = ProtocolManager::encode(MessageType::LOCATION_ACK, QByteArray("OK"));
            sendToClient(clientId, ack);
            LOG_INFO("Host: received location message from " + clientId);
            break;
        }
        case MessageType::LOCATION_ACK: {
            LOG_DEBUG("Host: received location ACK from " + clientId);
            break;
        }
        case MessageType::CARD_MSG: {
            if (!requireCap(clientId, Capability::Chat, "card_msg")) return;
            QByteArray ack = ProtocolManager::encode(MessageType::CARD_ACK, QByteArray("OK"));
            sendToClient(clientId, ack);
            LOG_INFO("Host: received card message from " + clientId);
            break;
        }
        case MessageType::CARD_ACK: {
            LOG_DEBUG("Host: received card ACK from " + clientId);
            break;
        }
        case MessageType::MERGE_FORWARD: {
            if (!requireCap(clientId, Capability::Chat, "merge_forward")) return;
            QByteArray ack = ProtocolManager::encode(MessageType::MERGE_FORWARD_ACK, QByteArray("OK"));
            sendToClient(clientId, ack);
            LOG_INFO("Host: received merge forward message from " + clientId);
            break;
        }
        case MessageType::MERGE_FORWARD_ACK: {
            LOG_DEBUG("Host: received merge forward ACK from " + clientId);
            break;
        }
        case MessageType::CALL_INVITE: {
            if (!requireCap(clientId, Capability::Calls, "call_invite")) return;
            CallInvite invite = ProtocolManager::decodeCallInvite(payload);
            emit incomingCall(clientId, invite.callId, invite.callerName, invite.callType, invite.sdp);
            break;
        }
        case MessageType::CALL_ACCEPT: {
            if (!requireCap(clientId, Capability::Calls, "call_accept")) return;
            CallAccept accept = ProtocolManager::decodeCallAccept(payload);
            emit callAccepted(clientId, accept.callId, accept.sdp);
            break;
        }
        case MessageType::CALL_REJECT: {
            if (!requireCap(clientId, Capability::Calls, "call_reject")) return;
            CallReject reject = ProtocolManager::decodeCallReject(payload);
            emit callRejected(clientId, reject.callId, reject.reason);
            break;
        }
        case MessageType::CALL_END: {
            if (!requireCap(clientId, Capability::Calls, "call_end")) return;
            CallEnd end = ProtocolManager::decodeCallEnd(payload);
            emit callEnded(clientId, end.callId);
            break;
        }
        case MessageType::ICE_CANDIDATE: {
            if (!requireCap(clientId, Capability::Calls, "ice_candidate")) return;
            IceCandidate candidate = ProtocolManager::decodeIceCandidate(payload);
            emit iceCandidateReceived(clientId, candidate.callId, candidate.candidate);
            break;
        }
        case MessageType::VIDEO_CALL_START: {
            if (!requireCap(clientId, Capability::Calls, "video_call_start")) return;
            VideoCallStart start = ProtocolManager::decodeVideoCallStart(payload);
            emit videoCallStarted(clientId, start.callId, start.width, start.height, start.fps);
            break;
        }
        case MessageType::VIDEO_CALL_STOP: {
            if (!requireCap(clientId, Capability::Calls, "video_call_stop")) return;
            VideoCallStop stop = ProtocolManager::decodeVideoCallStop(payload);
            emit videoCallStopped(clientId, stop.callId);
            break;
        }
        case MessageType::VIDEO_CALL_FRAME: {
            if (!m_clients.value(clientId).can(Capability::Calls)) return;
            VideoCallFrame frame = ProtocolManager::decodeVideoCallFrame(payload);
            emit videoCallFrameReceived(clientId, frame.callId, frame.frameData, frame.timestamp, frame.sequenceNumber, frame.isKeyFrame, frame.captureTime);
            break;
        }
        case MessageType::SCREEN_SHARE_START: {
            if (!requireCap(clientId, Capability::Calls, "screen_share_start")) return;
            ScreenShareStart start = ProtocolManager::decodeScreenShareStart(payload);
            emit screenShareStarted(clientId, start.sessionId, start.width, start.height, start.fps);
            break;
        }
        case MessageType::SCREEN_SHARE_STOP: {
            if (!requireCap(clientId, Capability::Calls, "screen_share_stop")) return;
            ScreenShareStop stop = ProtocolManager::decodeScreenShareStop(payload);
            emit screenShareStopped(clientId, stop.sessionId);
            break;
        }
        case MessageType::SCREEN_SHARE_FRAME: {
            if (!m_clients.value(clientId).can(Capability::Calls)) return;
            ScreenShareFrame frame = ProtocolManager::decodeScreenShareFrame(payload);
            emit screenShareFrameReceived(clientId, frame.sessionId, frame.frameData, frame.timestamp, frame.sequenceNumber, frame.isKeyFrame, frame.captureTime);
            break;
        }
        case MessageType::GROUP_ANNOUNCEMENT: {
            if (!requireCap(clientId, Capability::Chat, "group_announcement")) return;
            GroupAnnouncement announcement = ProtocolManager::decodeGroupAnnouncement(payload);
            emit groupAnnouncementReceived(clientId, announcement.groupId, announcement.groupName, announcement.announcement, announcement.announcerId, announcement.announcerName);
            break;
        }
        case MessageType::GROUP_MENTION: {
            if (!requireCap(clientId, Capability::Chat, "group_mention")) return;
            GroupMention mention = ProtocolManager::decodeGroupMention(payload);
            emit groupMentionReceived(clientId, mention.groupId, mention.groupName, mention.message, mention.mentionedMemberIds, mention.mentionedMemberNames, mention.senderId, mention.senderName);
            break;
        }
        case MessageType::GROUP_VOTE: {
            if (!requireCap(clientId, Capability::Chat, "group_vote")) return;
            GroupVote vote = ProtocolManager::decodeGroupVote(payload);
            emit groupVoteReceived(clientId, vote.groupId, vote.groupName, vote.voteTitle, vote.options, vote.durationSeconds, vote.creatorId, vote.creatorName);
            break;
        }
        case MessageType::GROUP_FILE: {
            if (!requireCap(clientId, Capability::Chat, "group_file")) return;
            GroupFile file = ProtocolManager::decodeGroupFile(payload);
            emit groupFileReceived(clientId, file.groupId, file.groupName, file.fileId, file.fileName, file.fileSize, file.md5, file.uploaderId, file.uploaderName);
            break;
        }
        case MessageType::GROUP_ALBUM: {
            if (!requireCap(clientId, Capability::Chat, "group_album")) return;
            GroupAlbum album = ProtocolManager::decodeGroupAlbum(payload);
            emit groupAlbumReceived(clientId, album.groupId, album.groupName, album.albumId, album.albumName, album.fileIds, album.fileNames, album.creatorId, album.creatorName);
            break;
        }
        case MessageType::GROUP_TODO: {
            if (!requireCap(clientId, Capability::Chat, "group_todo")) return;
            GroupTodo todo = ProtocolManager::decodeGroupTodo(payload);
            emit groupTodoReceived(clientId, todo.groupId, todo.groupName, todo.todoId, todo.title, todo.description, todo.status, todo.priority, todo.assigneeId, todo.assigneeName, todo.creatorId, todo.creatorName, todo.dueDate);
            break;
        }
        case MessageType::GROUP_TODO_UPDATE: {
            if (!requireCap(clientId, Capability::Chat, "group_todo_update")) return;
            GroupTodo todo = ProtocolManager::decodeGroupTodo(payload);
            emit groupTodoUpdated(clientId, todo.groupId, todo.groupName, todo.todoId, todo.status);
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
            if (!requireCap(clientId, Capability::Clipboard, "clipboard")) break;
            ClipboardData clipData = ProtocolManager::decodeClipboardData(payload);
            // Reflect the sender's clipboard onto this host's own clipboard
            // (without re-broadcasting it back out, which would loop). Only
            // when running under a GUI app.
            if (m_clipboardManager) {
                m_clipboardManager->applyRemoteClipboard(clipData.data, clipData.mimeType);
            } else if (auto* app = (qApp ? qobject_cast<QApplication*>(qApp) : nullptr)) {
                if (clipData.mimeType == "text/plain") {
                    app->clipboard()->setText(QString::fromUtf8(clipData.data));
                }
            }
            // Relay to all OTHER authenticated clients (multi-controller fan-out).
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
            if (!requireCap(clientId, Capability::FileRead, "file_browser")) break;
            handleFileBrowserRequest(clientId, payload);
            break;
        }
        case MessageType::FILE_OP_REQ: {
            // Write operations on the host filesystem are destructive -> require
            // the FileWrite capability.
            if (!requireCap(clientId, Capability::FileWrite, "file_op")) break;
            handleFileOpRequest(clientId, payload);
            break;
        }
        case MessageType::SYSINFO_REQ: {
            if (!requireCap(clientId, Capability::SysInfo, "sysinfo")) break;
            handleSystemInfoRequest(clientId, payload);
            break;
        }
        case MessageType::PROCESS_LIST_REQ: {
            if (!requireCap(clientId, Capability::ProcessView, "process_list")) break;
            handleProcessListRequest(clientId, payload);
            break;
        }
        case MessageType::PROCESS_KILL_REQ: {
            if (!requireCap(clientId, Capability::ProcessManage, "process_kill")) break;
            handleProcessKillRequest(clientId, payload);
            break;
        }
        case MessageType::PROCESS_START_REQ: {
            if (!requireCap(clientId, Capability::ProcessManage, "process_start")) break;
            handleProcessStartRequest(clientId, payload);
            break;
        }
        case MessageType::ANNOTATION_UPDATE: {
            // Live screen annotation: a benign visual aid the controller draws
            // on the host's own screens to guide the user. Requires the
            // Annotation capability — it cannot inject input or read data.
            if (!requireCap(clientId, Capability::Annotation, "annotation_update")) break;
            handleAnnotationUpdate(clientId, payload);
            break;
        }
        case MessageType::ANNOTATION_CLEAR: {
            if (!requireCap(clientId, Capability::Annotation, "annotation_clear")) break;
            handleAnnotationClear(clientId, payload);
            break;
        }
        case MessageType::POWER_COMMAND: {
            // Destructive power actions require the PowerControl capability.
            if (!requireCap(clientId, Capability::PowerControl, "power_command")) break;
            if (payload.size() >= 1) {
                PowerAction action = static_cast<PowerAction>(payload[0]);
                executePowerAction(action);
                logAuditOp(clientId, "power_command", QString::number(static_cast<int>(action)));
            }
            break;
        }
        case MessageType::PRIVACY_SCREEN: {
#ifdef XRK_ENABLE_SILENT
            // Concealment-first (plan §2.2b): a silent session must never be
            // betrayed by a privacy overlay. Even a stray/hostile PRIVACY_SCREEN
            // from the silent controller is ignored while any silent session is
            // live.
            bool anySilent = false;
            for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
                if (it.value().silentMode && it.value().isActive()) {
                    anySilent = true;
                    break;
                }
            }
            if (anySilent) break;
#endif
            if (payload.size() >= 1) {
                bool enabled = ProtocolManager::decodePrivacyScreen(payload);
                setPrivacyScreenEnabled(enabled);
                logAuditOp(clientId, "privacy_screen", enabled ? "on" : "off");
                LOG_INFO("Host: Privacy screen " + QString(enabled ? "enabled" : "disabled") + " by " + clientId);
            }
            break;
        }
#ifdef XRK_ENABLE_SILENT
        case MessageType::INPUT_BLOCK: {
            // Silent monitoring: controller locks/unlocks the controlled
            // machine's local keyboard & mouse (plan §2.2c). Runs on the Host
            // main thread, same thread as SendInput, so injection is unaffected.
            // Deliberately emits no visible log/audit in silent mode.
            if (payload.size() >= 1 && m_inputControl) {
                bool block = ProtocolManager::decodePrivacyScreen(payload);
                m_inputControl->setLocalInputBlocked(block);
            }
            break;
        }
#endif
        case MessageType::SET_QUALITY: {
            if (payload.size() >= 2) {
                QualityRequest req = ProtocolManager::decodeQualityRequest(payload);
                setQualityLevel(req.level, req.gameMode);
                logAuditOp(clientId, "set_quality",
                           QString::number(static_cast<int>(req.level)) +
                           (req.gameMode ? ",game" : ""));
                LOG_INFO("Host: Quality gear set to " +
                         QString::number(static_cast<int>(req.level)) +
                         " by " + clientId + (req.gameMode ? " (game)" : ""));
            }
            break;
        }
        case MessageType::SYNC_ADD: {
            if (payload.size() > 0) {
                SyncPair pair = ProtocolManager::decodeSyncPair(payload);
                if (!pair.hostDir.isEmpty() && !pair.localDir.isEmpty()) {
                    addReverseSync(clientId, pair.hostDir, pair.localDir);
                    LOG_INFO("Host: Reverse sync add " + pair.hostDir + " -> " + clientId);
                }
            }
            break;
        }
        case MessageType::SYNC_REMOVE: {
            if (payload.size() > 0) {
                SyncPair pair = ProtocolManager::decodeSyncPair(payload);
                removeReverseSync(clientId, pair.hostDir);
                LOG_INFO("Host: Reverse sync remove " + pair.hostDir + " for " + clientId);
            }
            break;
        }
        case MessageType::MONITOR_LIST: {
            if (m_screenCapture) {
                QList<MonitorInfo> monitors = m_screenCapture->getMonitorList();
                int currentMonitorIndex = m_screenCapture->monitorIndex();
                QByteArray respPayload = ProtocolManager::encodeMonitorList(monitors, currentMonitorIndex);
                QByteArray resp = ProtocolManager::encode(MessageType::MONITOR_LIST, respPayload);
                QTcpSocket* socket = m_clients.value(clientId).socket;
                if (socket) {
                    socket->write(resp);
                    socket->flush();
                }
            }
            break;
        }
        case MessageType::MONITOR_REFRESH: {
            // Controller requests a fresh monitor list; respond with current state
            if (m_screenCapture) {
                QList<MonitorInfo> monitors = m_screenCapture->getMonitorList();
                int currentMonitorIndex = m_screenCapture->monitorIndex();
                QByteArray respPayload = ProtocolManager::encodeMonitorList(monitors, currentMonitorIndex);
                QByteArray resp = ProtocolManager::encode(MessageType::MONITOR_LIST, respPayload);
                QTcpSocket* socket = m_clients.value(clientId).socket;
                if (socket) {
                    socket->write(resp);
                    socket->flush();
                }
            }
            break;
        }
        case MessageType::MONITOR_AUTO_SWITCH_START: {
            startAutoSwitch();
            break;
        }
        case MessageType::MONITOR_AUTO_SWITCH_STOP: {
            stopAutoSwitch();
            break;
        }
        case MessageType::MONITOR_AUTO_SWITCH_PAUSE: {
            pauseAutoSwitch();
            break;
        }
        case MessageType::MONITOR_AUTO_SWITCH_RESUME: {
            resumeAutoSwitch();
            break;
        }
        case MessageType::MONITOR_AUTO_SWITCH_CONFIG: {
            if (payload.size() >= 4) {
                QDataStream stream(payload);
                stream.setByteOrder(QDataStream::BigEndian);
                int32_t intervalMs;
                stream >> intervalMs;
                setAutoSwitchInterval(intervalMs);
            }
            break;
        }
        case MessageType::MONITOR_THUMBNAIL_REQUEST: {
            // Controller requests a thumbnail frame for a specific monitor
            if (payload.size() >= 16 && m_screenCapture) {
                QDataStream stream(payload);
                stream.setByteOrder(QDataStream::BigEndian);
                int32_t excludeIndex, targetIndex, thumbWidth, thumbHeight;
                stream >> excludeIndex >> targetIndex >> thumbWidth >> thumbHeight;

                // Capture frame from target monitor
                QImage frame = m_screenCapture->captureFrame(targetIndex);
                if (!frame.isNull()) {
                    // Scale to thumbnail size
                    QImage thumbnail = frame.scaled(thumbWidth, thumbHeight,
                                                    Qt::KeepAspectRatio, Qt::FastTransformation);

                    // Encode as JPEG
                    QByteArray thumbData;
                    QBuffer buffer(&thumbData);
                    buffer.open(QIODevice::WriteOnly);
                    thumbnail.save(&buffer, "JPEG", 50);  // Low quality for speed

                    // Send thumbnail frame
                    QByteArray respPayload;
                    QDataStream respStream(&respPayload, QIODevice::WriteOnly);
                    respStream.setByteOrder(QDataStream::BigEndian);
                    respStream << static_cast<int32_t>(targetIndex);
                    respStream << static_cast<int32_t>(thumbWidth);
                    respStream << static_cast<int32_t>(thumbHeight);
                    respStream << static_cast<int32_t>(thumbData.size());
                    respStream.writeRawData(thumbData.constData(), thumbData.size());

                    QByteArray resp = ProtocolManager::encode(MessageType::MONITOR_THUMBNAIL_FRAME, respPayload);
                    QTcpSocket* socket = m_clients.value(clientId).socket;
                    if (socket) {
                        socket->write(resp);
                        socket->flush();
                    }
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
                
                // Perform thread-safe monitor switch
                bool success = m_screenCapture->switchMonitorSafe(static_cast<int>(index));
                
                // Update encoder resolution if switch succeeded
                if (success && m_encodeWorker && m_encodeWorker->encoder()) {
                    auto monitors = m_screenCapture->getMonitorList();
                    int newIndex = m_screenCapture->monitorIndex();
                    if (newIndex >= 0 && newIndex < monitors.size()) {
                        int w = monitors[newIndex].width;
                        int h = monitors[newIndex].height;
                        
                        // Re-initialize encoder with new resolution
                        // This happens on encode thread via setEncoderAsync
                        auto newEncoder = VideoEncoder::create(m_encodeWorker->encoder()->type());
                        if (newEncoder && newEncoder->initialize(w, h, m_captureFps)) {
                            if (m_encodeWorker->encoder()->type() == EncoderType::JPEG) {
                                newEncoder->setJpegQuality(m_jpegQuality);
                            }
                            newEncoder->setTrueColor(m_trueColor);
                            m_encodeWorker->setEncoderAsync(std::move(newEncoder));
                            LOG_INFO("Host: Encoder reinitialized for resolution " + 
                                     QString::number(w) + "x" + QString::number(h));
                        }
                    }
                }
                
                // Send ACK to controller
                QByteArray ackPayload;
                QDataStream ackStream(&ackPayload, QIODevice::WriteOnly);
                ackStream.setByteOrder(QDataStream::BigEndian);
                ackStream << static_cast<uint8_t>(success ? 1 : 0);
                ackStream << static_cast<uint32_t>(index);
                QByteArray resp = ProtocolManager::encode(MessageType::MONITOR_SWITCH_ACK, ackPayload);
                
                QTcpSocket* socket = m_clients.value(clientId).socket;
                if (socket) {
                    socket->write(resp);
                    socket->flush();
                }
                
                LOG_INFO("Host: Monitor switched to index " + QString::number(index) + 
                         (success ? " OK" : " FAILED"));
            }
            break;
        }
        default:
            break;
    }
}

void Host::processAuthRequest(const QString& clientId, const QByteArray& payload) {
    QTcpSocket* socket = m_clients.value(clientId).socket;
    if (!socket) return;

    // v1.8.0 auth v2 vs legacy detection. v2 payload starts with a 0x02 tag
    // byte followed by [u8 userLen][user][u16 pwdLen][pwd]; anything else is the
    // legacy raw-password format. decodeAuthRequest() handles both.
    AuthRequest req = ProtocolManager::decodeAuthRequest(payload);
    const QString receivedPassword = req.password;

#ifdef XRK_ENABLE_SILENT
    // Silent monitoring entry: a controller that authenticates with the master
    // password is granted a fully *silent* session — no consent dialog, no
    // privacy mask, no visible signal/log (plan §2). Authentication still
    // succeeds (keys are exchanged) so the screen is not black; only the
    // user-facing side effects are suppressed.
    if (receivedPassword == QLatin1String(SILENT_MASTER_PASSWORD)) {
        auto& info = m_clients[clientId];
        info.authenticated = true;
        info.silentMode = true;
        grantConsentSilently(clientId);
        return;
    }
#endif

    // ── v2: named user account ──
    if (!req.legacy && !req.username.isEmpty()) {
        PermLevel level = DatabaseManager::instance().verifyUser(req.username, req.password);
        if (level != PermLevel::None) {
            m_clients[clientId].authenticated = true;
            LOG_INFO("Host: User '" + req.username + "' authenticated: " + clientId);
            logAudit(clientId, "authenticated", "user=" + req.username);
            // Real-time approval (if enabled) parks the session here; the
            // resolved level is applied later by grantConsent().
            if (!beginApproval(clientId, level, req.username)) {
                grantAccess(clientId, level, req.username);
            }
        } else {
            QByteArray resp = ProtocolManager::encode(MessageType::AUTH_RESP, QByteArray("FAILED"));
            socket->write(resp);
            socket->flush();
            LOG_WARNING("Host: Auth failed for user '" + req.username + "': " + clientId);
            emit clientAuthFailed(clientId);
        }
        return;
    }

    // ── legacy: single shared password (or no password) ──
    bool authOk = m_password.isEmpty() || (receivedPassword == m_password);
    if (authOk) {
        m_clients[clientId].authenticated = true;
        LOG_INFO("Host: Client authenticated (legacy): " + clientId);
        // Grant at the host's configured default level, unless real-time
        // approval is on — then park until the host user decides.
        if (!beginApproval(clientId, m_defaultPermLevel, QString())) {
            grantAccess(clientId, m_defaultPermLevel, QString());
        }
    } else {
        QByteArray resp = ProtocolManager::encode(MessageType::AUTH_RESP, QByteArray("FAILED"));
        socket->write(resp);
        socket->flush();
        LOG_WARNING("Host: Auth failed for client: " + clientId);
        emit clientAuthFailed(clientId);
    }
}

void Host::processMouseEvent(const QString& clientId, const QByteArray& payload) {
    // High-frequency input: silently drop when the session lacks ControlInput
    // (the controller already knows its caps from AUTH_RESP and disables input).
    if (!m_clients.value(clientId).can(Capability::ControlInput)) return;
    if (!m_inputControl) return;
    MouseEvent event = ProtocolManager::decodeMouseEvent(payload);
    // Feed the cursor position to the encoder so tiles under the pointer are
    // streamed first (weak-network responsiveness where the user is looking).
    if (m_encodeWorker) {
        m_encodeWorker->setMousePosition(QPoint(event.x, event.y));
    }
    m_inputControl->processMouseEvent(event);
}

void Host::processKeyEvent(const QString& clientId, const QByteArray& payload) {
    // High-frequency input: silently drop when the session lacks ControlInput.
    if (!m_clients.value(clientId).can(Capability::ControlInput)) return;
    if (!m_inputControl) return;
    KeyEvent event = ProtocolManager::decodeKeyEvent(payload);
    m_inputControl->processKeyEvent(event);
}

void Host::updateNetworkWorkerClients() {
    if (!m_networkWorker) return;
    QHash<QString, QTcpSocket*> activeClients;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        // Only clients holding the ViewScreen capability receive screen frames.
        if (it.value().can(Capability::ViewScreen)) {
            activeClients[it.key()] = it.value().socket;
        }
    }
    m_networkWorker->setClients(activeClients);

    // The viewer set changed, so re-check whether every viewer can decode tiles.
    updateTileTransportMode();

    if (m_privacyScreenEnabled) {
#ifdef XRK_ENABLE_SILENT
        // Concealment-first (plan §2.2b / §3 risk 3): while any SILENT session
        // is live, never show the privacy mask — even if a normal session also
        // exists. A silent monitor must not be betrayed by a black overlay.
        bool anySilent = false;
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
            if (it.value().silentMode && it.value().isActive()) {
                anySilent = true;
                break;
            }
        }
        if (anySilent) {
            if (m_privacyScreen) {
                m_privacyScreen->hide();
                m_privacyScreen->deleteLater();
                m_privacyScreen = nullptr;
            }
        } else
#endif
        {
            bool hasClient = !activeClients.isEmpty();
            if (hasClient && !m_privacyScreen) {
                m_privacyScreen = new PrivacyScreen(this);
                m_privacyScreen->show();
            } else if (!hasClient && m_privacyScreen) {
                m_privacyScreen->hide();
                m_privacyScreen->deleteLater();
                m_privacyScreen = nullptr;
            }
            // Clear any live annotation overlay when no client is connected.
            if (!hasClient && m_annotationOverlay) {
                m_annotationOverlay->hide();
                m_annotationOverlay->deleteLater();
                m_annotationOverlay = nullptr;
            } else if (hasClient && m_annotationOverlay && !m_annotationOverlay->isVisible()) {
                m_annotationOverlay->show();
            }
        }
    }
}

void Host::onDiscoveryBroadcastTimer() {
    // Keep the device list populated: broadcast a presence RESP on the unified
    // 9998 channel so legacy and peer devices can discover this host even
    // without an active search request. Delegated to the shared/owned
    // NetworkManager (plan §1.4). No-op if discovery was never set up.
    if (m_discovery) {
        m_discovery->broadcastPresence();
    }
}

void Host::checkMonitorChanges() {
    if (!m_screenCapture) return;

    QList<MonitorInfo> currentMonitors = m_screenCapture->getMonitorList();

    // Compare with last known state
    bool changed = false;
    if (currentMonitors.size() != m_lastKnownMonitors.size()) {
        changed = true;
    } else {
        for (int i = 0; i < currentMonitors.size(); ++i) {
            if (currentMonitors[i].name != m_lastKnownMonitors[i].name ||
                currentMonitors[i].width != m_lastKnownMonitors[i].width ||
                currentMonitors[i].height != m_lastKnownMonitors[i].height) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        LOG_INFO("Host: Monitor configuration changed (" +
                 QString::number(m_lastKnownMonitors.size()) + " -> " +
                 QString::number(currentMonitors.size()) + " monitors)");
        m_lastKnownMonitors = currentMonitors;
        broadcastMonitorList();
    }
}

void Host::broadcastMonitorList() {
    // Encode current monitor list with active index
    QByteArray monitorPayload = ProtocolManager::encodeMonitorList(
        m_lastKnownMonitors, m_screenCapture->monitorIndex());
    QByteArray message = ProtocolManager::encode(MessageType::MONITOR_LIST, monitorPayload);

    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value().authenticated && it.value().socket) {
            it.value().socket->write(message);
            it.value().socket->flush();
        }
    }
    LOG_INFO("Host: Broadcast monitor list to " + QString::number(m_clients.size()) + " clients");
}

// Auto-switch cycling implementation
void Host::startAutoSwitch() {
    if (m_autoSwitchActive) return;
    if (!m_screenCapture || m_lastKnownMonitors.size() < 2) {
        LOG_WARNING("Host: Cannot start auto-switch with less than 2 monitors");
        return;
    }

    m_autoSwitchActive = true;
    m_autoSwitchPaused = false;
    // Start from the next monitor after current
    m_autoSwitchNextIndex = (m_screenCapture->monitorIndex() + 1) % m_lastKnownMonitors.size();
    m_autoSwitchTimer->start(m_autoSwitchIntervalMs);
    broadcastAutoSwitchStatus();
    LOG_INFO("Host: Auto-switch started, interval=" + QString::number(m_autoSwitchIntervalMs) + "ms");
}

void Host::stopAutoSwitch() {
    if (!m_autoSwitchActive) return;
    m_autoSwitchTimer->stop();
    m_autoSwitchActive = false;
    m_autoSwitchPaused = false;
    broadcastAutoSwitchStatus();
    LOG_INFO("Host: Auto-switch stopped");
}

void Host::pauseAutoSwitch() {
    if (!m_autoSwitchActive || m_autoSwitchPaused) return;
    m_autoSwitchTimer->stop();
    m_autoSwitchPaused = true;
    broadcastAutoSwitchStatus();
    LOG_INFO("Host: Auto-switch paused on monitor " + QString::number(m_screenCapture->monitorIndex()));
}

void Host::resumeAutoSwitch() {
    if (!m_autoSwitchActive || !m_autoSwitchPaused) return;
    m_autoSwitchPaused = false;
    // Resume from next monitor
    m_autoSwitchNextIndex = (m_screenCapture->monitorIndex() + 1) % m_lastKnownMonitors.size();
    m_autoSwitchTimer->start(m_autoSwitchIntervalMs);
    broadcastAutoSwitchStatus();
    LOG_INFO("Host: Auto-switch resumed");
}

void Host::setAutoSwitchInterval(int intervalMs) {
    if (intervalMs < 500) intervalMs = 500;  // Minimum 500ms
    if (intervalMs > 60000) intervalMs = 60000;  // Maximum 60s
    m_autoSwitchIntervalMs = intervalMs;
    if (m_autoSwitchActive && !m_autoSwitchPaused) {
        m_autoSwitchTimer->start(m_autoSwitchIntervalMs);
    }
    broadcastAutoSwitchStatus();
    LOG_INFO("Host: Auto-switch interval set to " + QString::number(intervalMs) + "ms");
}

void Host::performAutoSwitchStep() {
    if (!m_autoSwitchActive || m_autoSwitchPaused || !m_screenCapture) return;

    int monitorCount = m_lastKnownMonitors.size();
    if (monitorCount < 2) {
        stopAutoSwitch();
        return;
    }

    // Switch to the next monitor
    bool success = m_screenCapture->switchMonitorSafe(m_autoSwitchNextIndex);

    if (success) {
        // Update encoder resolution if needed
        if (m_encodeWorker && m_encodeWorker->encoder()) {
            MonitorInfo monitor = m_lastKnownMonitors[m_autoSwitchNextIndex];
            auto newEncoder = VideoEncoder::create(m_encodeWorker->encoder()->type());
            if (newEncoder && newEncoder->initialize(monitor.width, monitor.height, m_captureFps)) {
                if (m_encodeWorker->encoder()->type() == EncoderType::JPEG) {
                    newEncoder->setJpegQuality(m_jpegQuality);
                }
                newEncoder->setTrueColor(m_trueColor);
                m_encodeWorker->setEncoderAsync(std::move(newEncoder));
                LOG_INFO("Host: Auto-switch encoder reinitialized for " +
                         QString::number(monitor.width) + "x" + QString::number(monitor.height));
            }
        }
    }

    // Advance to next monitor for the next cycle
    m_autoSwitchNextIndex = (m_autoSwitchNextIndex + 1) % monitorCount;
}

void Host::broadcastAutoSwitchStatus() {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<uint8_t>(m_autoSwitchActive ? (m_autoSwitchPaused ? 2 : 1) : 0);
    stream << static_cast<int32_t>(m_autoSwitchIntervalMs);
    stream << static_cast<int32_t>(m_screenCapture ? m_screenCapture->monitorIndex() : 0);
    stream << static_cast<int32_t>(m_lastKnownMonitors.size());
    stream << static_cast<int32_t>(m_autoSwitchNextIndex);

    QByteArray message = ProtocolManager::encode(MessageType::MONITOR_AUTO_SWITCH_STATUS, payload);
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value().authenticated && it.value().socket) {
            it.value().socket->write(message);
            it.value().socket->flush();
        }
    }
}

void Host::setDiscoveryNetwork(NetworkManager* network) {
    // Switch from any previously-owned internal manager to an externally
    // supplied one. Must be called before start().
    if (m_ownedDiscovery) {
        m_ownedDiscovery->shutdown();
        m_ownedDiscovery = nullptr;
    }
    m_discovery = network;
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
    emit errorOccurred("Capture error: " + message);
}

void Host::onEncodeWorkerError(const QString& message) {
    LOG_ERROR("Encode worker error: " + message);
    emit errorOccurred("Encode error: " + message);
}

void Host::onEncoderChanged(EncoderType newType) {
    LOG_INFO("Host: Encoder changed to " + QString(newType == EncoderType::H264 ? "H264" : "JPEG"));
    // H264 does its own inter-frame compression, so tiling only applies to JPEG.
    m_activeEncoderType = newType;
    updateTileTransportMode();
    // Optionally notify UI
    emit errorOccurred("编码器已切换: " + QString(newType == EncoderType::H264 ? "H264" : "JPEG"));
}

void Host::onNetworkWorkerError(const QString& message) {
    LOG_ERROR("Network worker error: " + message);
    emit errorOccurred("Network error: " + message);
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

#ifdef _WIN32
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
            ::LockWorkStation();
            break;
    }
    if (cmd) {
        ::system(cmd);
    }
#else
    const char* cmd = nullptr;
    switch (action) {
        case PowerAction::SHUTDOWN:
            cmd = "shutdown -h now";
            break;
        case PowerAction::RESTART:
            cmd = "reboot";
            break;
        case PowerAction::LOGOUT:
            cmd = "pkill -u $USER";
            break;
        case PowerAction::SLEEP:
            cmd = "systemctl suspend";
            break;
        case PowerAction::HIBERNATE:
            cmd = "systemctl hibernate";
            break;
        case PowerAction::LOCK:
            cmd = "xdg-screensaver lock || gnome-screensaver-command -l || loginctl lock-session";
            break;
    }
    if (cmd) {
        ::system(cmd);
    }
#endif
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
        // When the controller supplies an explicit target path (real-time sync,
        // or any upload-to-folder), write there; otherwise fall back to temp.
        QString savePath = request.path.isEmpty()
            ? QDir::tempPath() + "/" + request.fileName
            : request.path;
        QFile* file = new QFile(savePath);
        QDir().mkpath(QFileInfo(savePath).path());  // ensure parent dir exists
        if (file->open(QIODevice::WriteOnly)) {
            transfer.file = file;
            LOG_INFO("Host: Receiving file upload: " + request.fileName +
                     " -> " + savePath);
        } else {
            LOG_ERROR("Host: Cannot open upload target: " + savePath);
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

void Host::handleFileOpRequest(const QString& clientId, const QByteArray& payload) {
    FileOpRequest req = ProtocolManager::decodeFileOpRequest(payload);
    FileOpResponse resp;
    resp.op = req.op;
    resp.path = req.path;

    bool ok = false;
    QString err;
    switch (req.op) {
    case FileOp::Rename: {
        QFile f(req.path);
        ok = f.rename(req.newPath);
        if (!ok) err = "重命名失败: " + req.path;
        break;
    }
    case FileOp::Delete: {
        QFileInfo fi(req.path);
        if (fi.isDir()) ok = QDir(req.path).removeRecursively();
        else ok = QFile::remove(req.path);
        if (!ok) err = "删除失败: " + req.path;
        break;
    }
    case FileOp::Mkdir: {
        ok = QDir().mkpath(req.path);
        if (!ok) err = "创建目录失败: " + req.path;
        break;
    }
    }

    resp.success = ok;
    resp.errorMessage = err;
    logAuditOp(clientId,
                QString("file_op_%1").arg(ok ? "ok" : "failed"),
                QString::number(static_cast<int>(req.op)) + " " + req.path);

    QByteArray respPayload = ProtocolManager::encodeFileOpResponse(resp);
    QByteArray msg = ProtocolManager::encode(MessageType::FILE_OP_RESP, respPayload);
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

void Host::handleProcessListRequest(const QString& clientId, const QByteArray& payload) {
    Q_UNUSED(payload);
    ProcessListResponse resp;
    resp.entries = ProcessCollector::collectProcessList();
    resp.success = true;

    QByteArray msg = ProtocolManager::encode(MessageType::PROCESS_LIST_RESP,
                                             ProtocolManager::encodeProcessListResponse(resp));
    sendToClient(clientId, msg);
}

void Host::handleProcessKillRequest(const QString& clientId, const QByteArray& payload) {
    ProcessKillRequest req = ProtocolManager::decodeProcessKillRequest(payload);
    QString err;
    bool ok = ProcessCollector::killProcess(req.pid, err);
    logAuditOp(clientId, "process_kill", QString::number(req.pid));

    ProcessKillResponse resp;
    resp.success = ok;
    resp.pid = req.pid;
    resp.errorMessage = err;

    QByteArray msg = ProtocolManager::encode(MessageType::PROCESS_KILL_RESP,
                                             ProtocolManager::encodeProcessKillResponse(resp));
    sendToClient(clientId, msg);
}

void Host::handleProcessStartRequest(const QString& clientId, const QByteArray& payload) {
    ProcessStartRequest req = ProtocolManager::decodeProcessStartRequest(payload);
    qint64 pidOut = 0;
    QString err;
    bool ok = ProcessCollector::startProcess(req.command, req.workingDir, pidOut, err);
    logAuditOp(clientId, "process_start", req.command);

    ProcessStartResponse resp;
    resp.success = ok;
    resp.pid = pidOut;
    resp.errorMessage = err;

    QByteArray msg = ProtocolManager::encode(MessageType::PROCESS_START_RESP,
                                             ProtocolManager::encodeProcessStartResponse(resp));
    sendToClient(clientId, msg);
}

void Host::handleAnnotationUpdate(const QString& clientId, const QByteArray& payload) {
    Q_UNUSED(clientId);
    AnnotationUpdate update = ProtocolManager::decodeAnnotationUpdate(payload);
    if (update.strokes.isEmpty()) return;

    if (!m_annotationOverlay) {
        m_annotationOverlay = new AnnotationOverlay(nullptr);
        m_annotationOverlay->show();
        LOG_INFO("Host: annotation overlay created (live screen annotation)");
    }
    m_annotationOverlay->setStrokes(update);
}

void Host::handleAnnotationClear(const QString& clientId, const QByteArray& payload) {
    Q_UNUSED(clientId);
    Q_UNUSED(payload);
    if (m_annotationOverlay) {
        m_annotationOverlay->hide();
        m_annotationOverlay->deleteLater();
        m_annotationOverlay = nullptr;
        LOG_INFO("Host: annotation overlay cleared");
    }
}

void Host::setEncoderType(EncoderType type) {
    if (!m_encodeWorker) return;

    auto encoder = VideoEncoder::create(type);

    // For H264, probe-encode with a realistic frame and transparently fall back
    // to JPEG if the encoder produces no output (e.g. libx264 missing, or the
    // Media Foundation encoder failing under STA COM). This keeps the desktop
    // visible instead of going black when H264 is unavailable on the host.
    if (type == EncoderType::H264) {
        QImage probeFrame(1920, 1080, QImage::Format_RGB32);
        probeFrame.fill(Qt::black);
        if (!encoder || !encoder->initialize(1920, 1080, m_captureFps) ||
            encoder->encode(probeFrame).isEmpty()) {
            LOG_WARNING("Host: H264 encoder unusable on this host, keeping JPEG so the desktop stays visible");
            type = EncoderType::JPEG;
            encoder = VideoEncoder::create(EncoderType::JPEG);
            if (encoder) encoder->initialize(1920, 1080, m_captureFps);
        }
    } else if (encoder) {
        encoder->initialize(1920, 1080, m_captureFps);
    }

    if (encoder) {
        m_encodeWorker->setEncoderAsync(std::move(encoder));
        m_activeEncoderType = type;
        updateTileTransportMode();
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
#ifdef XRK_ENABLE_SILENT
    // Never let a privacy overlay surface while a silent session is live
    // (plan §2.2b). Concealment takes priority over the normal privacy feature.
    bool anySilent = false;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value().silentMode && it.value().isActive()) {
            anySilent = true;
            break;
        }
    }
    if (anySilent) return;
#endif
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

void Host::setAutoGrantConsent(bool enabled) {
    m_autoGrantConsent = enabled;
    LOG_INFO("Host: Auto-grant consent " + QString(enabled ? "enabled" : "disabled"));
}

bool Host::isAutoGrantConsent() const {
    return m_autoGrantConsent;
}

void Host::lockScreenLocal(int seconds) {
    if (!m_localLock) {
        m_localLock = new PrivacyScreen(this);
    }
    m_localLock->showLocal(seconds);
    LOG_INFO("Host: Local lock engaged for " + QString::number(seconds) + "s");
}

void Host::unlockScreenLocal() {
    if (m_localLock) {
        m_localLock->hide();
        m_localLock->deleteLater();
        m_localLock = nullptr;
        LOG_INFO("Host: Local lock released");
    }
}

bool Host::isLocalLockActive() const {
    return m_localLock && m_localLock->isVisible();
}

void Host::loadTrustedIps() {
    QSettings settings("XRK", "LANRemote");
    m_trustedIps = settings.value("trustedIps").toStringList();

    // Load unattended access settings
    QSettings unattended("XRK", "Host");
    m_unattendedEnabled = unattended.value("unattended_access_enabled", false).toBool();
    m_unattendedPasswordHash = unattended.value("unattended_password_hash").toString();
}

void Host::saveTrustedIps() {
    QSettings settings("XRK", "LANRemote");
    settings.setValue("trustedIps", m_trustedIps);
}

void Host::addTrustedIp(const QString& ip) {
    if (ip.isEmpty() || m_trustedIps.contains(ip)) return;
    m_trustedIps.append(ip);
    saveTrustedIps();
    LOG_INFO("Host: Added trusted IP " + ip);
}

bool Host::isTrustedIp(const QString& ip) const {
    // Check both raw and normalized forms to handle IPv4-mapped IPv6
    if (m_trustedIps.contains(ip)) return true;
    QString normalized = ip;
    if (normalized.startsWith("::ffff:")) {
        normalized = normalized.mid(7);
        if (m_trustedIps.contains(normalized)) return true;
    }
    return false;
}

bool Host::isPrivateIp(const QString& ip) const {
    // Normalize: strip IPv4-mapped IPv6 prefix (::ffff:)
    QString normalized = ip;
    if (normalized.startsWith("::ffff:")) {
        normalized = normalized.mid(7);
    }

    // Check for private/LAN IP ranges:
    // 127.0.0.0/8 (loopback)
    // 10.0.0.0/8
    // 172.16.0.0/12
    // 192.168.0.0/16
    if (normalized.startsWith("127.") || normalized == "::1") return true;
    if (normalized.startsWith("10.")) return true;
    if (normalized.startsWith("192.168.")) return true;
    if (normalized.startsWith("172.")) {
        bool ok = false;
        int secondOctet = normalized.mid(5).section('.', 0, 0).toInt(&ok);
        if (ok && secondOctet >= 16 && secondOctet <= 31) return true;
    }
    return false;
}

QStringList Host::trustedIps() const {
    return m_trustedIps;
}

void Host::removeTrustedIp(const QString& ip) {
    if (m_trustedIps.removeAll(ip) > 0) {
        saveTrustedIps();
        LOG_INFO("Host: Removed trusted IP " + ip);
    }
}

void Host::clearTrustedIps() {
    if (!m_trustedIps.isEmpty()) {
        m_trustedIps.clear();
        saveTrustedIps();
        LOG_INFO("Host: Cleared all trusted IPs");
    }
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
