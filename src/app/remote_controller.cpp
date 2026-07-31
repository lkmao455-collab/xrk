#include "remote_controller.h"
#include "core/network_manager.h"
#include "core/tcp_connection.h"
#include "core/protocol_manager.h"
#include "session_manager.h"
#include "core/logger.h"
#include "hw/audio_player.h"
#include "hw/audio_capture.h"
#include "nat_traversal.h"
#include "p2p_manager.h"
#include <QThread>
#include <QDataStream>

namespace xrk {

RemoteController::RemoteController(NetworkManager* network, SessionManager* session, QObject* parent)
    : QObject(parent), m_network(network), m_session(session) {
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &RemoteController::sendHeartbeat);
}

RemoteController::~RemoteController() {
    stopRemote();
}

bool RemoteController::startRemote(const QString& ip, uint16_t port) {
    return startRemote(ip, port, QString());
}

bool RemoteController::startRemote(const QString& ip, uint16_t port, const QString& password) {
    if (m_active) {
        return true;
    }

    if (!m_network) {
        LOG_ERROR("Network manager not initialized");
        return false;
    }

    auto conn = m_network->connectTo(ip, port);
    if (!conn) {
        LOG_ERROR("Failed to connect to " + ip + ":" + QString::number(port));
        return false;
    }

    return setupConnection(conn, ip, TransportType::Lan, password);
}

void RemoteController::configureRelay(const QString& host, uint16_t port, const QString& token,
                                      const QString& myDeviceId) {
    m_relayHost = host;
    m_relayPort = port;
    m_relayToken = token;
    m_myDeviceId = myDeviceId;
    m_relayConfigured = !host.isEmpty() && port != 0;
}

bool RemoteController::startRemoteByDevice(const QString& deviceId, const QString& password) {
    if (m_active) {
        return true;
    }
    if (!m_relayConfigured) {
        LOG_ERROR("RemoteController: relay not configured, cannot connect by device");
        emit connectionError("中继未配置");
        return false;
    }

    m_pendingDeviceId = deviceId;
    m_pendingPassword = password;
    m_p2pInProgress = true;
    beginP2PConnect(deviceId, password);
    return true; // connection is established asynchronously
}

bool RemoteController::setupConnection(std::shared_ptr<TcpConnection> conn, const QString& peerLabel,
                                       TransportType transport, const QString& password) {
    if (!conn) {
        LOG_ERROR("RemoteController: null connection");
        return false;
    }

    m_connection = conn;
    // P2P/relay connections are re-established at a higher level; disable the
    // socket-level reconnect which would otherwise aim at the wrong endpoint.
    bool useReconnect = (transport == TransportType::Lan) && m_autoReconnect;
    m_connection->setReconnectEnabled(useReconnect);
    m_connection->setReconnectInterval(3000);
    m_connection->setMaxReconnectAttempts(5);

    connect(m_connection.get(), &TcpConnection::readyRead, this, &RemoteController::onMessageReceived);
    connect(m_connection.get(), &TcpConnection::disconnected, this, &RemoteController::onConnectionLost);
    connect(m_connection.get(), &TcpConnection::reconnecting, this, &RemoteController::onReconnecting);
    connect(m_connection.get(), &TcpConnection::reconnected, this, &RemoteController::onReconnected);
    connect(m_connection.get(), &TcpConnection::reconnectFailed, this, &RemoteController::onReconnectFailed);

    m_currentIp = peerLabel;
    m_currentPort = 0;
    m_password = password;
    m_transport = transport;

    if (m_session) {
        m_currentSessionId = m_session->createSession(peerLabel);
    }

    m_active = true;
    m_p2pInProgress = false;

    startDecodeWorker();

    if (!m_password.isEmpty()) {
        sendAuthRequest(m_password);
    }

    emit transportEstablished(transport);
    emit remoteStarted(peerLabel);

    if (!m_audioPlayer) {
        m_audioPlayer = new AudioPlayer(this);
        if (!m_audioPlayer->initialize()) {
            LOG_WARNING("RemoteController: Audio player init failed");
        }
    }

    LOG_INFO("Remote started (" + QString::number(static_cast<int>(transport)) + ") to " + peerLabel);
    return true;
}

void RemoteController::stopRemote() {
    if (!m_active && !m_p2pInProgress) {
        return;
    }

    m_active = false;
    m_p2pInProgress = false;
    if (m_heartbeatTimer) m_heartbeatTimer->stop();
    if (m_relayFallbackTimer) m_relayFallbackTimer->stop();
    if (m_p2p) m_p2p->cancel();

    stopDecodeWorker();

    if (m_audioPlayer) {
        m_audioPlayer->shutdown();
        delete m_audioPlayer;
        m_audioPlayer = nullptr;
    }

    if (m_micCapture) {
        m_micCapture->shutdown();
        delete m_micCapture;
        m_micCapture = nullptr;
    }

    if (m_connection) {
        m_connection->disconnectFromHost();
        m_connection.reset();
    }

    if (m_session && !m_currentSessionId.isEmpty()) {
        m_session->closeSession(m_currentSessionId);
    }

    QString ip = m_currentIp;
    m_currentIp.clear();
    m_currentPort = 0;
    m_currentSessionId.clear();
    m_password.clear();

    emit remoteStopped();
    LOG_INFO("Remote stopped from " + ip);
}

bool RemoteController::isRemoteActive() const {
    return m_active;
}

QString RemoteController::currentIp() const {
    return m_currentIp;
}

uint16_t RemoteController::currentPort() const {
    return m_currentPort;
}

void RemoteController::setAutoReconnect(bool enabled) {
    m_autoReconnect = enabled;
    if (m_connection) {
        m_connection->setReconnectEnabled(enabled);
    }
}

bool RemoteController::isAutoReconnect() const {
    return m_autoReconnect;
}

void RemoteController::sendMouseEvent(const MouseEvent& event) {
    if (!m_active || !m_connection) {
        return;
    }

    QByteArray payload = ProtocolManager::encodeMouseEvent(event);
    QByteArray message = ProtocolManager::encode(MessageType::MOUSE_EVENT, payload, m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::sendKeyEvent(const KeyEvent& event) {
    if (!m_active || !m_connection) {
        return;
    }

    QByteArray payload = ProtocolManager::encodeKeyEvent(event);
    QByteArray message = ProtocolManager::encode(MessageType::KEY_EVENT, payload, m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::requestScreenFrame() {
    if (!m_active || !m_connection) {
        return;
    }

    QByteArray message = ProtocolManager::encode(MessageType::SCREEN_FRAME_ACK, QByteArray(), m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::sendAuthRequest(const QString& password) {
    if (!m_connection) {
        return;
    }

    QByteArray payload = password.toUtf8();
    QByteArray message = ProtocolManager::encode(MessageType::AUTH_REQ, payload, m_currentSessionId);
    m_connection->send(message);
    LOG_DEBUG("Auth request sent");
}

void RemoteController::sendTerminalStart(const QString& shellType, uint32_t cols, uint32_t rows) {
    if (!m_active || !m_connection) return;
    
    TerminalData data;
    data.shellType = shellType;
    data.cols = cols;
    data.rows = rows;
    
    QByteArray payload = ProtocolManager::encodeTerminalData(data);
    QByteArray message = ProtocolManager::encode(MessageType::TERMINAL_START, payload, m_currentSessionId);
    m_connection->send(message);
    LOG_INFO("Terminal start sent: " + shellType);
}

void RemoteController::sendTerminalInput(const QString& command) {
    if (!m_active || !m_connection) return;
    
    TerminalData data;
    data.data = command;
    
    QByteArray payload = ProtocolManager::encodeTerminalData(data);
    QByteArray message = ProtocolManager::encode(MessageType::TERMINAL_INPUT, payload, m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::sendTerminalStop() {
    if (!m_active || !m_connection) return;
    
    QByteArray message = ProtocolManager::encode(MessageType::TERMINAL_STOP, QByteArray(), m_currentSessionId);
    m_connection->send(message);
    LOG_INFO("Terminal stop sent");
}

void RemoteController::requestScreenshot() {
    if (!m_active || !m_connection) return;
    
    QByteArray message = ProtocolManager::encode(MessageType::SCREENSHOT_REQ, QByteArray(), m_currentSessionId);
    m_connection->send(message);
    LOG_INFO("Screenshot requested");
}

void RemoteController::sendChatMessage(const QString& message) {
    if (!m_active || !m_connection) return;
    
    ChatMessage chat;
    chat.sender = "Me";
    chat.content = message;
    chat.timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
    
    QByteArray payload = ProtocolManager::encodeChatMessage(chat);
    QByteArray msg = ProtocolManager::encode(MessageType::CHAT_MESSAGE, payload, m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::startRecording(const QString& filePath, int fps) {
    if (!m_active || !m_connection) return;
    
    RecordControl control;
    control.filePath = filePath;
    control.fps = qBound(1, fps, 60);
    
    QByteArray payload = ProtocolManager::encodeRecordControl(control);
    QByteArray msg = ProtocolManager::encode(MessageType::RECORD_START, payload, m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::stopRecording() {
    if (!m_active || !m_connection) return;
    
    QByteArray msg = ProtocolManager::encode(MessageType::RECORD_STOP, QByteArray(), m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::setCameraMode(bool enabled) {
    if (!m_active || !m_connection) return;
    
    QByteArray payload(1, enabled ? 1 : 0);
    QByteArray msg = ProtocolManager::encode(MessageType::CAMERA_MODE, payload, m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::setAudioEnabled(bool enabled) {
    if (!m_active || !m_connection) return;
    
    QByteArray payload(1, enabled ? 1 : 0);
    QByteArray msg = ProtocolManager::encode(MessageType::AUDIO_START, payload, m_currentSessionId);
    m_connection->send(msg);
    
    if (!enabled && m_audioPlayer) {
        m_audioPlayer->shutdown();
    }
}

void RemoteController::setMicrophoneEnabled(bool enabled) {
    if (enabled) {
        if (!m_micCapture) {
            m_micCapture = new AudioCapture(this, AudioCapture::Microphone);
            connect(m_micCapture, &AudioCapture::audioDataCaptured,
                    this, &RemoteController::sendMicAudio);
        }
        if (!m_micCapture->isInitialized()) {
            if (!m_micCapture->initialize()) {
                LOG_WARNING("Microphone capture init failed");
                m_micCapture->deleteLater();
                m_micCapture = nullptr;
                emit microphoneStateChanged(false);
                return;
            }
        }
        LOG_INFO("Microphone enabled (two-way voice)");
        emit microphoneStateChanged(true);
    } else {
        if (m_micCapture) {
            m_micCapture->shutdown();
            m_micCapture->deleteLater();
            m_micCapture = nullptr;
        }
        LOG_INFO("Microphone disabled");
        emit microphoneStateChanged(false);
    }
}

void RemoteController::sendMicAudio(const QByteArray& pcm) {
    if (!m_active || !m_connection) return;
    QByteArray message = ProtocolManager::encode(MessageType::AUDIO_DATA, pcm, m_currentSessionId);
    m_connection->send(message);
}

bool RemoteController::isMicrophoneEnabled() const {
    return m_micCapture && m_micCapture->isInitialized();
}

void RemoteController::sendPowerAction(PowerAction action) {
    if (!m_active || !m_connection) return;
    
    QByteArray payload(1, static_cast<uint8_t>(action));
    QByteArray msg = ProtocolManager::encode(MessageType::POWER_COMMAND, payload, m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::sendPrivacyScreen(bool enabled) {
    if (!m_active || !m_connection) return;

    QByteArray payload = ProtocolManager::encodePrivacyScreen(enabled);
    QByteArray msg = ProtocolManager::encode(MessageType::PRIVACY_SCREEN, payload, m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::sendQualityLevel(QualityLevel level, bool gameMode) {
    if (!m_active || !m_connection) return;

    QualityRequest req;
    req.level = level;
    req.gameMode = gameMode;
    QByteArray payload = ProtocolManager::encodeQualityRequest(req);
    QByteArray msg = ProtocolManager::encode(MessageType::SET_QUALITY, payload, m_currentSessionId);
    m_connection->send(msg);
    LOG_INFO("Controller: requested quality gear " + QString::number(static_cast<int>(level)) +
             (gameMode ? " (game/low-latency)" : ""));
}

void RemoteController::sendSyncAdd(const QString& hostDir, const QString& localDir) {
    if (!m_active || !m_connection) return;
    SyncPair pair;
    pair.hostDir = hostDir;
    pair.localDir = localDir;
    QByteArray payload = ProtocolManager::encodeSyncPair(pair);
    QByteArray msg = ProtocolManager::encode(MessageType::SYNC_ADD, payload, m_currentSessionId);
    m_connection->send(msg);
    LOG_INFO("Controller: reverse sync add " + hostDir + " -> " + localDir);
}

void RemoteController::sendSyncRemove(const QString& hostDir) {
    if (!m_active || !m_connection) return;
    SyncPair pair;
    pair.hostDir = hostDir;
    pair.localDir = QString();
    QByteArray payload = ProtocolManager::encodeSyncPair(pair);
    QByteArray msg = ProtocolManager::encode(MessageType::SYNC_REMOVE, payload, m_currentSessionId);
    m_connection->send(msg);
    LOG_INFO("Controller: reverse sync remove " + hostDir);
}

void RemoteController::requestMonitorList() {
    if (!m_active || !m_connection) return;
    QByteArray msg = ProtocolManager::encode(MessageType::MONITOR_LIST, QByteArray(), m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::switchMonitor(int index) {
    if (!m_active || !m_connection) return;
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<uint32_t>(index);
    QByteArray msg = ProtocolManager::encode(MessageType::MONITOR_SWITCH, payload, m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::sendHeartbeat() {
    if (!m_active || !m_connection) return;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << now;
    QByteArray msg = ProtocolManager::encode(MessageType::HEARTBEAT, payload, m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::requestFileBrowser(const QString& path) {
    if (!m_active || !m_connection) return;

    FileBrowserRequest req;
    req.path = path;
    QByteArray payload = ProtocolManager::encodeFileBrowserRequest(req);
    QByteArray msg = ProtocolManager::encode(MessageType::FILE_BROWSER_REQ, payload, m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::requestSystemInfo() {
    if (!m_active || !m_connection) return;
    QByteArray msg = ProtocolManager::encode(MessageType::SYSINFO_REQ, QByteArray(), m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::onMessageReceived(const QByteArray& data) {
    MessageType type;
    QByteArray payload;
    QString sessionId;

    if (!ProtocolManager::decode(data, type, payload, sessionId)) {
        LOG_WARNING("Failed to decode message");
        return;
    }

    processMessage(type, payload);
}

void RemoteController::onConnectionLost(const QString& deviceId) {
    if (m_active) {
        LOG_WARNING("Connection lost, waiting for reconnect...");
    }
}

void RemoteController::onReconnecting(int attempt, int maxAttempts) {
    emit reconnecting(attempt, maxAttempts);
    LOG_INFO("Reconnecting: " + QString::number(attempt) + "/" + QString::number(maxAttempts));
}

void RemoteController::onReconnected() {
    m_active = true;
    if (!m_password.isEmpty()) {
        sendAuthRequest(m_password);
    }
    emit reconnected();
    LOG_INFO("Reconnected to " + m_currentIp);
}

void RemoteController::onReconnectFailed() {
    m_active = false;
    emit reconnectFailed();
    LOG_ERROR("Reconnect failed to " + m_currentIp);
}

void RemoteController::processMessage(MessageType type, const QByteArray& payload) {
    switch (type) {
        case MessageType::SCREEN_FRAME:
            handleScreenFrame(payload);
            break;
        case MessageType::AUTH_RESP:
            handleAuthResponse(payload);
            break;
        case MessageType::HEARTBEAT_RESP: {
            if (payload.size() >= static_cast<int>(sizeof(qint64))) {
                QDataStream stream(payload);
                stream.setByteOrder(QDataStream::BigEndian);
                qint64 sent;
                stream >> sent;
                m_roundTripMs = QDateTime::currentMSecsSinceEpoch() - sent;
                emit latencyUpdated(m_roundTripMs);
            }
            break;
        }
        case MessageType::TERMINAL_OUTPUT: {
            TerminalData data = ProtocolManager::decodeTerminalData(payload);
            emit terminalOutputReceived(data.data);
            break;
        }
        case MessageType::SCREENSHOT_RESP: {
            ScreenFrame frame = ProtocolManager::decodeScreenFrame(payload);
            QImage image;
            if (frame.format == FrameFormat::JPEG && image.loadFromData(frame.data, "JPEG")) {
                emit screenshotReceived(image);
            }
            break;
        }
        case MessageType::CHAT_MESSAGE: {
            ChatMessage chat = ProtocolManager::decodeChatMessage(payload);
            emit chatMessageReceived(chat.sender, chat.content);
            break;
        }
        case MessageType::RECORD_ACK: {
            QString status = QString::fromUtf8(payload);
            if (status == "OK") {
                emit recordingStarted();
            } else {
                emit recordingError(status);
            }
            break;
        }
        case MessageType::FILE_BROWSER_RESP: {
            FileBrowserResponse resp = ProtocolManager::decodeFileBrowserResponse(payload);
            emit fileBrowserReceived(resp);
            break;
        }
        case MessageType::SYSINFO_RESP: {
            SysInfo info = ProtocolManager::decodeSysInfo(payload);
            emit sysInfoReceived(info);
            break;
        }
        case MessageType::QUALITY_INFO: {
            QualityInfo info = ProtocolManager::decodeQualityInfo(payload);
            emit qualityInfoReceived(info);
            break;
        }
        case MessageType::SYNC_NOTIFY: {
            SyncNotify note = ProtocolManager::decodeSyncNotify(payload);
            emit syncNotifyReceived(note);
            break;
        }
        case MessageType::AUDIO_DATA: {
            if (m_audioPlayer && m_audioPlayer->isInitialized()) {
                m_audioPlayer->playAudio(payload);
            }
            break;
        }
        case MessageType::MONITOR_LIST: {
            QList<MonitorInfo> monitors = ProtocolManager::decodeMonitorList(payload);
            emit monitorListReceived(monitors);
            break;
        }
        case MessageType::CONSENT_REQUEST: {
            // Host is asking its user to approve this connection.
            emit consentRequested();
            LOG_INFO("Consent requested by host; waiting for approval");
            break;
        }
        case MessageType::CONSENT_RESPONSE: {
            bool allowed = false;
            QString hostName;
            ProtocolManager::decodeConsent(payload, allowed, hostName);
            if (allowed) {
                emit consentGranted();
                LOG_INFO("Host granted consent");
            } else {
                emit consentDenied();
                LOG_WARNING("Host denied consent; disconnecting");
                stopRemote();
            }
            break;
        }
        default:
            break;
    }
}

void RemoteController::handleScreenFrame(const QByteArray& data) {
    if (m_decodeQueue && m_decodeRunning) {
        if (!m_decodeQueue->enqueueNonBlocking(data)) {
            static int dropCount = 0;
            ++dropCount;
            if (dropCount <= 5 || dropCount % 300 == 0) {
                LOG_WARNING("[Frame] decode queue full, dropped " + QString::number(dropCount));
            }
        }
        return;
    }

    // Synchronous fallback (decode worker not running, e.g. unit tests)
    static int frameCount = 0;
    static int decryptFailCount = 0;
    frameCount++;

    bool hasEncryption = m_encryption && m_encryption->isInitialized();

    QByteArray frameData = data;
    if (hasEncryption) {
        frameData = m_encryption->decrypt(data);
        if (frameData.isEmpty()) {
            ++decryptFailCount;
            if (decryptFailCount <= 5 || decryptFailCount % 300 == 0) {
                LOG_WARNING("[Frame#" + QString::number(frameCount) + "] DECRYPT FAIL #" +
                            QString::number(decryptFailCount));
            }
            return;
        }
    }

    ScreenFrame frame = ProtocolManager::decodeScreenFrame(frameData);
    if (frame.width <= 0 || frame.height <= 0 || frame.data.isEmpty()) {
        return;
    }

    emit screenFrameReceived(frame);
}

void RemoteController::startDecodeWorker() {
    stopDecodeWorker();
    m_decodeQueue = new FrameQueue<QByteArray>(4);
    m_decodeRunning = true;
    m_decodeThread = new QThread(this);

    // Use a context object whose thread affinity is the decode thread so that
    // the lambda runs on the decode thread (not the main thread).
    m_decodeWorkerCtx = new QObject();
    m_decodeWorkerCtx->moveToThread(m_decodeThread);

    QObject::connect(m_decodeThread, &QThread::started, m_decodeWorkerCtx, [this]() {
        while (m_decodeRunning) {
            QByteArray data = m_decodeQueue->dequeue(16);
            if (data.isEmpty()) continue;

            static int frameCount = 0;
            static int decryptFailCount = 0;
            frameCount++;

            bool hasEncryption = m_encryption && m_encryption->isInitialized();

            QByteArray frameData = data;
            if (hasEncryption) {
                frameData = m_encryption->decrypt(data);
                if (frameData.isEmpty()) {
                    ++decryptFailCount;
                    if (decryptFailCount <= 5 || decryptFailCount % 300 == 0) {
                        LOG_WARNING("[Decode#" + QString::number(frameCount) + "] DECRYPT FAIL #" +
                                    QString::number(decryptFailCount));
                    }
                    continue;
                }
            }

            ScreenFrame frame = ProtocolManager::decodeScreenFrame(frameData);
            if (frame.width <= 0 || frame.height <= 0 || frame.data.isEmpty()) {
                if (frameCount <= 5 || frameCount % 300 == 1) {
                    LOG_WARNING("[Decode#" + QString::number(frameCount) + "] INVALID w=" +
                                QString::number(frame.width) + " h=" + QString::number(frame.height) +
                                " dataSz=" + QString::number(frame.data.size()));
                }
                continue;
            }

            if (frameCount <= 5 || frameCount % 300 == 1) {
                LOG_INFO("[Decode#" + QString::number(frameCount) + "] OK " +
                         QString::number(frame.width) + "x" + QString::number(frame.height));
            }
            emit screenFrameReceived(frame);
        }
    });

    QObject::connect(m_decodeThread, &QThread::finished, m_decodeWorkerCtx, &QObject::deleteLater);
    m_decodeThread->start();
    LOG_INFO("RemoteController: decode worker started");
}

void RemoteController::stopDecodeWorker() {
    m_decodeRunning = false;
    if (m_decodeThread) {
        m_decodeThread->quit();
        m_decodeThread->wait(2000);
        delete m_decodeThread;
        m_decodeThread = nullptr;
    }
    m_decodeWorkerCtx = nullptr; // deleted by QThread::finished connection
    if (m_decodeQueue) {
        m_decodeQueue->clear();
        delete m_decodeQueue;
        m_decodeQueue = nullptr;
    }
    LOG_INFO("RemoteController: decode worker stopped");
}

void RemoteController::handleAuthResponse(const QByteArray& data) {
    LOG_INFO("[Auth] Response received, size=" + QString::number(data.size()) +
             " first8=0x" + data.left(8).toHex());
    QString responseStr = QString::fromUtf8(data.left(2));

    if (responseStr == "OK") {
        // Host always sends the session AES key+IV appended to "OK" (2 + 32 + 16
        // = 50 bytes). Note the >= : a strictly-greater check previously excluded
        // the exact-size response, so m_encryption was never initialized and the
        // controller never decrypted the (always-encrypted) screen frames, which
        // is what made the remote desktop render black.
        if (data.size() >= 2 + 32 + 16) {
            QByteArray encKey = data.mid(2, 32);
            QByteArray encIv = data.mid(2 + 32, 16);
            
            LOG_INFO("[Auth] Key=" + QString::number(encKey.size()) + "B IV=" + QString::number(encIv.size()) +
                     " keyHex=" + encKey.left(8).toHex() + " ivHex=" + encIv.left(8).toHex());

            if (encKey.size() == 32 && encIv.size() == 16) {
                m_encryption = std::make_unique<Encryption>();
                if (m_encryption->setKey(encKey, encIv)) {
                    LOG_INFO("[Auth] Encryption initialized");

                    // Roundtrip verification: encrypt then decrypt test data
                    QByteArray testData("XRK-VERIFY-1234567890");
                    QByteArray encrypted = m_encryption->encrypt(testData);
                    QByteArray decrypted = m_encryption->decrypt(encrypted);
                    if (decrypted == testData) {
                        LOG_INFO("[Auth] Encryption roundtrip OK (" + QString::number(testData.size()) +
                                 "B -> " + QString::number(encrypted.size()) + "B -> " +
                                 QString::number(decrypted.size()) + "B)");
                    } else {
                        LOG_ERROR("[Auth] Encryption roundtrip FAILED! enc=" +
                                  QString::number(encrypted.size()) + "B dec=" +
                                  QString::number(decrypted.size()) + "B expected=" +
                                  QString::number(testData.size()) + "B");
                        LOG_ERROR("[Auth] testData=" + testData.toHex());
                        LOG_ERROR("[Auth] encrypted=" + encrypted.left(32).toHex() + "...");
                        LOG_ERROR("[Auth] decrypted=" + decrypted.toHex());
                    }
                } else {
                    LOG_ERROR("[Auth] Failed to set encryption key");
                }
            } else {
                LOG_ERROR("[Auth] Invalid key/iv sizes: key=" + QString::number(encKey.size()) +
                          " iv=" + QString::number(encIv.size()));
            }
        } else {
            LOG_ERROR("[Auth] Response too short for key+IV: " + QString::number(data.size()) + "B (need >= 50)");
        }
        
        emit authSuccess();
        requestMonitorList();
        m_heartbeatTimer->start(2000);
        LOG_INFO("Authentication successful");
    } else if (responseStr == "NO") {
        // "NOT_AUTHORIZED" check
        if (data.size() > 14 && QString::fromUtf8(data.left(14)) == "NOT_AUTHORIZED") {
            emit authRequired();
            LOG_WARNING("Authentication required");
        } else {
            QString full = QString::fromUtf8(data);
            emit authFailed(full);
            LOG_WARNING("Authentication failed: " + full);
        }
    } else {
        QString full = QString::fromUtf8(data);
        emit authFailed(full);
        LOG_WARNING("Authentication failed: " + full);
    }
}

// ---------------- P2P / relay connection path ----------------

void RemoteController::beginP2PConnect(const QString& deviceId, const QString& password) {
    if (!m_nat) {
        m_nat = new NatTraversal(this);
        m_p2p = new P2PManager(this);
        m_relayFallbackTimer = new QTimer(this);
        m_relayFallbackTimer->setSingleShot(true);

        connect(m_nat, &NatTraversal::relayConnected, this, &RemoteController::onRelayConnected);
        connect(m_nat, &NatTraversal::relayError, this, &RemoteController::onNatError);
        connect(m_nat, &NatTraversal::peerAddressReceived, this, &RemoteController::onPeerAddressReceived);
        connect(m_nat, &NatTraversal::bridgeSocketReady, this, &RemoteController::onBridgeSocketReady);
        connect(m_p2p, &P2PManager::directConnectionEstablished, this, &RemoteController::onP2PDirect);
        connect(m_p2p, &P2PManager::punchFailed, this, &RemoteController::onPunchFailed);
        connect(m_relayFallbackTimer, &QTimer::timeout, this, &RemoteController::onRelayFallbackTimeout);
    }

    m_pendingDeviceId = deviceId;
    m_pendingPassword = password;

    m_nat->setRelayServer(m_relayHost, m_relayPort);
    m_nat->setRelayToken(m_relayToken);
    m_nat->setDeviceId(m_myDeviceId);

    if (m_nat->relayState() == RelayState::Registered) {
        onRelayConnected();
    } else {
        m_nat->connectToRelay();
    }
}

void RemoteController::onRelayConnected() {
    if (m_pendingDeviceId.isEmpty()) return;
    // Overall safety timer: if P2P never yields a connection, fall back to relay.
    m_relayFallbackTimer->start(6000);
    m_nat->requestPunch(m_pendingDeviceId);
    LOG_INFO("RemoteController: registered, requesting PUNCH to " + m_pendingDeviceId);
}

void RemoteController::onNatError(const QString& message) {
    if (!m_p2pInProgress) return;
    LOG_ERROR("RemoteController: relay error: " + message);
    // If we haven't established a direct connection yet, try relay fallback.
    fallbackToRelay();
}

void RemoteController::onPeerAddressReceived(const QString& peerId, const QHostAddress& address, quint16 port) {
    if (peerId != m_pendingDeviceId) return;
    if (m_p2p) {
        LOG_INFO("RemoteController: punching peer " + peerId + " " + address.toString() + ":" + QString::number(port));
        m_p2p->beginPunch(address, port);
    }
}


void RemoteController::onP2PDirect(QTcpSocket* socket) {
    if (!socket) return;
    m_relayFallbackTimer->stop();
    auto conn = std::make_shared<TcpConnection>(socket);
    conn->setReconnectEnabled(false);
    LOG_INFO("RemoteController: P2P direct connection established");
    setupConnection(conn, m_pendingDeviceId, TransportType::P2P, m_pendingPassword);
}

void RemoteController::onPunchFailed() {
    LOG_WARNING("RemoteController: P2P punch failed, falling back to relay");
    fallbackToRelay();
}

void RemoteController::onRelayFallbackTimeout() {
    LOG_WARNING("RemoteController: P2P timeout, falling back to relay");
    fallbackToRelay();
}

void RemoteController::fallbackToRelay() {
    if (!m_nat) return;
    if (m_nat->relayState() != RelayState::Registered) {
        m_p2pInProgress = false;
        emit connectionError("P2P 失败且中继不可用");
        return;
    }
    LOG_INFO("RemoteController: requesting relay bridge to " + m_pendingDeviceId);
    m_nat->requestBridge(m_pendingDeviceId);
}

void RemoteController::onBridgeSocketReady(QTcpSocket* socket, const QByteArray& initialData) {
    if (!socket) {
        m_p2pInProgress = false;
        emit connectionError("中继桥接失败");
        return;
    }
    auto conn = std::make_shared<TcpConnection>(socket);
    conn->setReconnectEnabled(false);
    if (!initialData.isEmpty()) {
        conn->injectData(initialData);
    }
    LOG_INFO("RemoteController: relay bridge connection established");
    setupConnection(conn, m_pendingDeviceId, TransportType::Relay, m_pendingPassword);
}

} // namespace xrk
