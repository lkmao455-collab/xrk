#include "remote_controller.h"
#include "core/network_manager.h"
#include "core/tcp_connection.h"
#include "core/protocol_manager.h"
#include "session_manager.h"
#include "core/logger.h"
#include "hw/audio_player.h"
#include "hw/audio_capture.h"
#include "hw/tile_encoder.h"
#include "nat_traversal.h"
#include "p2p_manager.h"
#include "connection_history_manager.h"
#include <QThread>
#include <QDataStream>
#include <QCryptographicHash>
#include <QPainter>

namespace xrk {

RemoteController::RemoteController(NetworkManager* network, SessionManager* session, QObject* parent)
    : QObject(parent), m_network(network), m_session(session) {
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &RemoteController::sendHeartbeat);

    // Feeds the host's adaptive loop with RTT, tile loss and buffer fullness.
    m_ackTimer = new QTimer(this);
    connect(m_ackTimer, &QTimer::timeout, this, &RemoteController::sendScreenAck);

    // Batches NACK tile-resend requests (see requestTileResend).
    m_nackTimer = new QTimer(this);
    m_nackTimer->setSingleShot(true);
    connect(m_nackTimer, &QTimer::timeout, this, &RemoteController::sendPendingNack);
    
    // Initialize connection history manager
    m_historyManager = new ConnectionHistoryManager(this);
    connect(m_historyManager, &ConnectionHistoryManager::connectionRecorded, this, [this](const ConnectionRecord& record) {
        LOG_DEBUG("Connection recorded: " + record.hostAddress + ":" + QString::number(record.hostPort));
    });
    connect(m_historyManager, &ConnectionHistoryManager::historyCleared, this, [this]() {
        LOG_INFO("Connection history cleared");
    });
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

    // Record connection attempt
    xrk::ConnectionRecord record;
    record.timestamp = QDateTime::currentDateTime();
    record.hostAddress = ip;
    record.hostPort = port;
    record.deviceId = m_network->deviceId();
    record.success = false;
    record.reconnectAttempts = 0;

    auto conn = m_network->connectTo(ip, port);
    if (!conn) {
        record.errorMessage = "Failed to establish TCP connection";
        m_historyManager->recordConnection(record);
        LOG_ERROR("Failed to connect to " + ip + ":" + QString::number(port));
        return false;
    }

    // Store connection start time for duration tracking
    m_connectionStartTime = QDateTime::currentDateTime();
    m_currentConnectionRecord = record;

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
        recordConnectionResult(false, "Null connection");
        return false;
    }

    m_connection = conn;
    // P2P/relay connections are re-established at a higher level; disable the
    // socket-level reconnect which would otherwise aim at the wrong endpoint.
    bool useReconnect = (transport == TransportType::Lan) && m_autoReconnect;
    m_connection->setReconnectEnabled(useReconnect);
    m_connection->setReconnectInterval(3000);
    m_connection->setMaxReconnectAttempts(5);
    // Enhanced reconnection config
    m_connection->setReconnectConfig(1000, 30000, 2.0, 20);

    connect(m_connection.get(), &TcpConnection::readyRead, this, &RemoteController::onMessageReceived);
    connect(m_connection.get(), &TcpConnection::disconnected, this, &RemoteController::onConnectionLost);
    connect(m_connection.get(), &TcpConnection::reconnecting, this, &RemoteController::onReconnecting);
    connect(m_connection.get(), &TcpConnection::reconnected, this, &RemoteController::onReconnected);
    connect(m_connection.get(), &TcpConnection::reconnectFailed, this, &RemoteController::onReconnectFailed);
    // Connect to enhanced signals
    connect(m_connection.get(), &TcpConnection::stateChanged, this, [this](xrk::ConnectionState state) {
        m_currentConnectionRecord.errorMessage = "State: " + QString::number(static_cast<int>(state));
    });
    connect(m_connection.get(), &TcpConnection::errorOccurred, this, [this](const QString& error) {
        m_currentConnectionRecord.errorMessage = error;
    });

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
        sendAuthRequest(m_username, m_password);
    }

    emit transportEstablished(transport);
    emit remoteStarted(peerLabel);

    if (!m_audioPlayer) {
        m_audioPlayer = new AudioPlayer(this);
        if (!m_audioPlayer->initialize()) {
            LOG_WARNING("RemoteController: Audio player init failed");
        }
    }

    // Record successful connection
    recordConnectionResult(true);
    LOG_INFO("Remote started (" + QString::number(static_cast<int>(transport)) + ") to " + peerLabel);
    return true;
}

void RemoteController::stopRemote() {
    if (!m_active && !m_p2pInProgress) {
        return;
    }

    // Record connection result if we had an active connection
    if (m_connectionStartTime.isValid()) {
        recordConnectionResult(true);  // Normal disconnect is success
    }

    m_active = false;
    m_p2pInProgress = false;
    if (m_heartbeatTimer) m_heartbeatTimer->stop();
    if (m_ackTimer) m_ackTimer->stop();
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

    emit remoteStopped();
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
    if (!m_active || !m_connection || !m_inputForwardEnabled) {
        return;
    }

    QByteArray payload = ProtocolManager::encodeMouseEvent(event);
    QByteArray message = ProtocolManager::encode(MessageType::MOUSE_EVENT, payload, m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::sendKeyEvent(const KeyEvent& event) {
    if (!m_active || !m_connection || !m_inputForwardEnabled) {
        return;
    }

    QByteArray payload = ProtocolManager::encodeKeyEvent(event);
    QByteArray message = ProtocolManager::encode(MessageType::KEY_EVENT, payload, m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::setInputForwardingEnabled(bool enabled) {
    m_inputForwardEnabled = enabled;
}

void RemoteController::sendInputBlock(bool blocked) {
    if (!m_active || !m_connection) {
        return;
    }
    QByteArray payload = ProtocolManager::encodePrivacyScreen(blocked);
    QByteArray message = ProtocolManager::encode(MessageType::INPUT_BLOCK, payload, m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::requestScreenFrame() {
    if (!m_active || !m_connection) {
        return;
    }

    QByteArray message = ProtocolManager::encode(MessageType::SCREEN_FRAME_ACK, QByteArray(), m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::requestKeyFrame() {
    if (!m_active || !m_connection) {
        return;
    }
    QByteArray message = ProtocolManager::encode(MessageType::SCREEN_KEYFRAME, QByteArray(), m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::sendScreenAck() {
    if (!m_active || !m_connection) {
        return;
    }

    ScreenAck ack;
    ack.timestamp = m_lastTileTimestamp.load();
    ack.roundTripMs = m_roundTripMs;
    // Consume the window so the host sees per-interval rates, not totals.
    ack.tilesReceived = m_tilesReceived.exchange(0);
    ack.tilesLost = m_tilesLost.exchange(0);
    if (m_tileQueue) {
        const int capacity = qMax(1, m_tileQueue->capacity());
        ack.bufferLevel = static_cast<uint32_t>(qMin(100, m_tileQueue->size() * 100 / capacity));
    }

    QByteArray payload = ProtocolManager::encodeScreenAck(ack);
    QByteArray message = ProtocolManager::encode(MessageType::SCREEN_FRAME_ACK, payload, m_currentSessionId);
    m_connection->send(message);

    if (ack.tilesLost > 0) {
        LOG_INFO("RemoteController: ack — received=" + QString::number(ack.tilesReceived) +
                  " lost=" + QString::number(ack.tilesLost) +
                  " rtt=" + QString::number(ack.roundTripMs) + "ms buffer=" +
                  QString::number(ack.bufferLevel) + "%");
    }
}

void RemoteController::requestTileResend(const QList<QPoint>& tiles) {
    if (!m_active || !m_connection || tiles.isEmpty()) {
        return;
    }

    ScreenTileRequest req;
    req.frameWidth = m_lastFrameWidth.load();
    req.frameHeight = m_lastFrameHeight.load();
    req.tiles = tiles;

    QByteArray payload = ProtocolManager::encodeScreenTileRequest(req);
    QByteArray message = ProtocolManager::encode(MessageType::SCREEN_TILE_REQUEST, payload, m_currentSessionId);
    m_connection->send(message);

    ++m_nackCount;
    LOG_INFO("RemoteController: NACK — requested resend of " + QString::number(tiles.size()) +
             " tile(s) (frame " + QString::number(req.frameWidth) + "x" +
             QString::number(req.frameHeight) + ")");
}

void RemoteController::sendPendingNack() {
    QList<QPoint> batch;
    {
        QMutexLocker lock(&m_nackMutex);
        if (m_pendingNack.isEmpty()) {
            return;
        }
        batch = m_pendingNack;
        m_pendingNack.clear();
    }
    requestTileResend(batch);
}

void RemoteController::sendAuthRequest(const QString& username, const QString& password) {
    if (!m_connection) {
        return;
    }

    // v1.8.0 RBAC: send a v2 AUTH_REQ when a username is supplied, otherwise fall
    // back to the legacy raw-password format so old hosts / shared-password
    // deployments keep working.
    AuthRequest req;
    req.legacy = username.isEmpty();
    req.username = username;
    req.password = password;
    QByteArray payload = ProtocolManager::encodeAuthRequest(req);
    QByteArray message = ProtocolManager::encode(MessageType::AUTH_REQ, payload, m_currentSessionId);
    m_connection->send(message);
    LOG_DEBUG("Auth request sent (v2=" + QString::number(!req.legacy) + ")");
}

bool RemoteController::hasCapability(Capability cap) const {
    // Effective capabilities are whatever the host granted in AUTH_RESP. Before a
    // successful auth m_grantedCaps is 0, so every capability reads as denied.
    return PermissionModel::hasCapability(m_grantedCaps, cap);
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

void RemoteController::sendVoiceMessageProtocol(const QByteArray& voiceData, int duration) {
    if (!m_active || !m_connection) return;
    
    VoiceMessage voiceMsg;
    voiceMsg.messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    voiceMsg.senderId = m_currentSessionId;
    voiceMsg.senderName = "Me";
    voiceMsg.voiceData = voiceData;
    voiceMsg.voiceFileName = "voice_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".pcm";
    voiceMsg.duration = duration;
    voiceMsg.timestamp = QDateTime::currentMSecsSinceEpoch();
    voiceMsg.isRead = false;
    
    QByteArray payload = ProtocolManager::encodeVoiceMessage(voiceMsg);
    QByteArray message = ProtocolManager::encode(MessageType::VOICE_MSG, payload, m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::sendVideoMessageProtocol(const QByteArray& videoData, int duration, double width, double height) {
    if (!m_active || !m_connection) return;
    
    VideoMessage videoMsg;
    videoMsg.messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    videoMsg.senderId = m_currentSessionId;
    videoMsg.senderName = "Me";
    videoMsg.videoData = videoData;
    videoMsg.videoFileName = "video_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".mp4";
    videoMsg.duration = duration;
    videoMsg.width = width;
    videoMsg.height = height;
    videoMsg.timestamp = QDateTime::currentMSecsSinceEpoch();
    videoMsg.isRead = false;
    
    QByteArray payload = ProtocolManager::encodeVideoMessage(videoMsg);
    QByteArray message = ProtocolManager::encode(MessageType::VIDEO_MSG, payload, m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::sendLocationMessageProtocol(double latitude, double longitude, const QString& name) {
    if (!m_active || !m_connection) return;
    
    LocationMessage locMsg;
    locMsg.messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    locMsg.senderId = m_currentSessionId;
    locMsg.senderName = "Me";
    locMsg.latitude = latitude;
    locMsg.longitude = longitude;
    locMsg.locationName = name;
    locMsg.timestamp = QDateTime::currentMSecsSinceEpoch();
    locMsg.isRead = false;
    
    QByteArray payload = ProtocolManager::encodeLocationMessage(locMsg);
    QByteArray message = ProtocolManager::encode(MessageType::LOCATION_MSG, payload, m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::sendCardMessageProtocol(const QString& vCardData) {
    if (!m_active || !m_connection) return;
    
    CardMessage cardMsg;
    cardMsg.messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    cardMsg.senderId = m_currentSessionId;
    cardMsg.senderName = "Me";
    cardMsg.vCardData = vCardData;
    cardMsg.timestamp = QDateTime::currentMSecsSinceEpoch();
    cardMsg.isRead = false;
    
    QByteArray payload = ProtocolManager::encodeCardMessage(cardMsg);
    QByteArray message = ProtocolManager::encode(MessageType::CARD_MSG, payload, m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::sendMergeForwardMessageProtocol(const QList<ForwardedMessage>& messages) {
    if (!m_active || !m_connection) return;
    
    MergeForwardMessage mergeMsg;
    mergeMsg.messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    mergeMsg.senderId = m_currentSessionId;
    mergeMsg.senderName = "Me";
    mergeMsg.messages = messages;
    mergeMsg.timestamp = QDateTime::currentMSecsSinceEpoch();
    mergeMsg.isRead = false;
    
    QByteArray payload = ProtocolManager::encodeMergeForwardMessage(mergeMsg);
    QByteArray message = ProtocolManager::encode(MessageType::MERGE_FORWARD, payload, m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::initiateCall(const QString& callType, const QString& sdp) {
    if (!m_active || !m_connection) return;
    
    CallInvite invite;
    invite.callId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    invite.callerId = m_currentSessionId;
    invite.callerName = "Me";
    invite.callType = callType;
    invite.sdp = sdp;
    invite.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    QByteArray payload = ProtocolManager::encodeCallInvite(invite);
    QByteArray message = ProtocolManager::encode(MessageType::CALL_INVITE, payload, m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::acceptCall(const QString& callId, const QString& sdp) {
    if (!m_active || !m_connection) return;
    
    CallAccept accept;
    accept.callId = callId;
    accept.calleeId = m_currentSessionId;
    accept.sdp = sdp;
    accept.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    QByteArray payload = ProtocolManager::encodeCallAccept(accept);
    QByteArray message = ProtocolManager::encode(MessageType::CALL_ACCEPT, payload, m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::rejectCall(const QString& callId, const QString& reason) {
    if (!m_active || !m_connection) return;
    
    CallReject reject;
    reject.callId = callId;
    reject.calleeId = m_currentSessionId;
    reject.reason = reason;
    reject.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    QByteArray payload = ProtocolManager::encodeCallReject(reject);
    QByteArray message = ProtocolManager::encode(MessageType::CALL_REJECT, payload, m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::endCall(const QString& callId) {
    if (!m_active || !m_connection) return;
    
    CallEnd end;
    end.callId = callId;
    end.peerId = m_currentSessionId;
    end.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    QByteArray payload = ProtocolManager::encodeCallEnd(end);
    QByteArray message = ProtocolManager::encode(MessageType::CALL_END, payload, m_currentSessionId);
    m_connection->send(message);
}

void RemoteController::sendIceCandidate(const QString& callId, const QString& candidate) {
    if (!m_active || !m_connection) return;
    
    IceCandidate ice;
    ice.callId = callId;
    ice.candidate = candidate;
    ice.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    QByteArray payload = ProtocolManager::encodeIceCandidate(ice);
    QByteArray message = ProtocolManager::encode(MessageType::ICE_CANDIDATE, payload, m_currentSessionId);
    m_connection->send(message);
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

void RemoteController::sendAnnotationUpdate(const AnnotationUpdate& update) {
    if (!m_active || !m_connection) return;
    QByteArray payload = ProtocolManager::encodeAnnotationUpdate(update);
    QByteArray msg = ProtocolManager::encode(MessageType::ANNOTATION_UPDATE, payload, m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::sendAnnotationClear() {
    if (!m_active || !m_connection) return;
    QByteArray msg = ProtocolManager::encode(MessageType::ANNOTATION_CLEAR, QByteArray(), m_currentSessionId);
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

void RemoteController::requestMonitorRefresh() {
    if (!m_active || !m_connection) return;
    QByteArray msg = ProtocolManager::encode(MessageType::MONITOR_REFRESH, QByteArray(), m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::startAutoSwitch() {
    if (!m_active || !m_connection) return;
    QByteArray msg = ProtocolManager::encode(MessageType::MONITOR_AUTO_SWITCH_START, QByteArray(), m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::stopAutoSwitch() {
    if (!m_active || !m_connection) return;
    QByteArray msg = ProtocolManager::encode(MessageType::MONITOR_AUTO_SWITCH_STOP, QByteArray(), m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::pauseAutoSwitch() {
    if (!m_active || !m_connection) return;
    QByteArray msg = ProtocolManager::encode(MessageType::MONITOR_AUTO_SWITCH_PAUSE, QByteArray(), m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::resumeAutoSwitch() {
    if (!m_active || !m_connection) return;
    QByteArray msg = ProtocolManager::encode(MessageType::MONITOR_AUTO_SWITCH_RESUME, QByteArray(), m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::setAutoSwitchInterval(int intervalMs) {
    if (!m_active || !m_connection) return;
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<int32_t>(intervalMs);
    QByteArray msg = ProtocolManager::encode(MessageType::MONITOR_AUTO_SWITCH_CONFIG, payload, m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::requestThumbnailFrame(int excludeIndex, int targetIndex, int width, int height) {
    if (!m_active || !m_connection) return;
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<int32_t>(excludeIndex);
    stream << static_cast<int32_t>(targetIndex);
    stream << static_cast<int32_t>(width);
    stream << static_cast<int32_t>(height);
    QByteArray msg = ProtocolManager::encode(MessageType::MONITOR_THUMBNAIL_REQUEST, payload, m_currentSessionId);
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

void RemoteController::sendFileOp(const FileOpRequest& req) {
    if (!m_active || !m_connection) return;
    QByteArray payload = ProtocolManager::encodeFileOpRequest(req);
    QByteArray msg = ProtocolManager::encode(MessageType::FILE_OP_REQ, payload, m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::requestSystemInfo() {
    if (!m_active || !m_connection) return;
    QByteArray msg = ProtocolManager::encode(MessageType::SYSINFO_REQ, QByteArray(), m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::requestProcessList() {
    if (!m_active || !m_connection) return;
    QByteArray msg = ProtocolManager::encode(MessageType::PROCESS_LIST_REQ, QByteArray(), m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::requestKillProcess(qint64 pid) {
    if (!m_active || !m_connection) return;
    ProcessKillRequest req;
    req.pid = pid;
    QByteArray payload = ProtocolManager::encodeProcessKillRequest(req);
    QByteArray msg = ProtocolManager::encode(MessageType::PROCESS_KILL_REQ, payload, m_currentSessionId);
    m_connection->send(msg);
}

void RemoteController::requestStartProcess(const QString& command, const QString& workingDir) {
    if (!m_active || !m_connection) return;
    ProcessStartRequest req;
    req.command = command;
    req.workingDir = workingDir;
    QByteArray payload = ProtocolManager::encodeProcessStartRequest(req);
    QByteArray msg = ProtocolManager::encode(MessageType::PROCESS_START_REQ, payload, m_currentSessionId);
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
        // Record failed connection if not reconnecting
        if (m_connection && !m_connection->isReconnectEnabled()) {
            recordConnectionResult(false, "Connection lost");
        }
    }
}

void RemoteController::onReconnecting(int attempt, int maxAttempts) {
    if (m_connectionStartTime.isValid()) {
        m_currentConnectionRecord.reconnectAttempts = attempt;
    }
    emit reconnecting(attempt, maxAttempts);
    LOG_INFO("Reconnecting: " + QString::number(attempt) + "/" + QString::number(maxAttempts));
}

void RemoteController::onReconnected() {
    m_active = true;
    if (!m_password.isEmpty()) {
        sendAuthRequest(m_username, m_password);
    }
    emit reconnected();
    LOG_INFO("Reconnected to " + m_currentIp);
}

void RemoteController::onReconnectFailed() {
    m_active = false;
    emit reconnectFailed();
    LOG_ERROR("Reconnect failed to " + m_currentIp);
    recordConnectionResult(false, "Reconnection failed after max attempts", m_currentConnectionRecord.reconnectAttempts);
}

void RemoteController::processMessage(MessageType type, const QByteArray& payload) {
    switch (type) {
        case MessageType::SCREEN_FRAME:
            handleScreenFrame(payload);
            break;
        case MessageType::SCREEN_TILE:
            handleScreenTile(payload);
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
        case MessageType::FILE_OP_RESP: {
            FileOpResponse resp = ProtocolManager::decodeFileOpResponse(payload);
            emit fileOpCompleted(resp);
            break;
        }
        case MessageType::SYSINFO_RESP: {
            SysInfo info = ProtocolManager::decodeSysInfo(payload);
            emit sysInfoReceived(info);
            break;
        }
        case MessageType::PROCESS_LIST_RESP: {
            ProcessListResponse resp = ProtocolManager::decodeProcessListResponse(payload);
            emit processListReceived(resp);
            break;
        }
        case MessageType::PROCESS_KILL_RESP: {
            ProcessKillResponse resp = ProtocolManager::decodeProcessKillResponse(payload);
            emit processKillReceived(resp);
            break;
        }
        case MessageType::PROCESS_START_RESP: {
            ProcessStartResponse resp = ProtocolManager::decodeProcessStartResponse(payload);
            emit processStartReceived(resp);
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
        case MessageType::VOICE_MSG: {
            if (m_audioPlayer && m_audioPlayer->isInitialized()) {
                m_audioPlayer->playAudio(payload);
            }
            QByteArray ack = ProtocolManager::encode(MessageType::VOICE_ACK, QByteArray("OK"), m_currentSessionId);
            m_connection->send(ack);
            LOG_INFO("Voice message received and played");
            break;
        }
        case MessageType::VOICE_ACK: {
            LOG_INFO("Voice message delivery confirmed by host");
            break;
        }
        case MessageType::VIDEO_MSG: {
            // Video message received - for now just log and send ACK
            // Full video playback would require video player component
            QByteArray ack = ProtocolManager::encode(MessageType::VIDEO_ACK, QByteArray("OK"), m_currentSessionId);
            m_connection->send(ack);
            LOG_INFO("Video message received");
            break;
        }
        case MessageType::VIDEO_ACK: {
            LOG_INFO("Video message delivery confirmed by host");
            break;
        }
        case MessageType::LOCATION_MSG: {
            QByteArray ack = ProtocolManager::encode(MessageType::LOCATION_ACK, QByteArray("OK"), m_currentSessionId);
            m_connection->send(ack);
            LOG_INFO("Location message received");
            break;
        }
        case MessageType::LOCATION_ACK: {
            LOG_INFO("Location message delivery confirmed by host");
            break;
        }
        case MessageType::CARD_MSG: {
            QByteArray ack = ProtocolManager::encode(MessageType::CARD_ACK, QByteArray("OK"), m_currentSessionId);
            m_connection->send(ack);
            LOG_INFO("Card message received");
            break;
        }
        case MessageType::CARD_ACK: {
            LOG_INFO("Card message delivery confirmed by host");
            break;
        }
        case MessageType::MERGE_FORWARD: {
            QByteArray ack = ProtocolManager::encode(MessageType::MERGE_FORWARD_ACK, QByteArray("OK"), m_currentSessionId);
            m_connection->send(ack);
            LOG_INFO("Merge forward message received");
            break;
        }
        case MessageType::MERGE_FORWARD_ACK: {
            LOG_INFO("Merge forward message delivery confirmed by host");
            break;
        }
        case MessageType::CALL_INVITE: {
            CallInvite invite = ProtocolManager::decodeCallInvite(payload);
            emit incomingCall(invite.callId, invite.callerName, invite.callType, invite.sdp);
            break;
        }
        case MessageType::CALL_ACCEPT: {
            CallAccept accept = ProtocolManager::decodeCallAccept(payload);
            emit callAccepted(accept.callId, accept.sdp);
            break;
        }
        case MessageType::CALL_REJECT: {
            CallReject reject = ProtocolManager::decodeCallReject(payload);
            emit callRejected(reject.callId, reject.reason);
            break;
        }
        case MessageType::CALL_END: {
            CallEnd end = ProtocolManager::decodeCallEnd(payload);
            emit callEnded(end.callId);
            break;
        }
        case MessageType::ICE_CANDIDATE: {
            IceCandidate candidate = ProtocolManager::decodeIceCandidate(payload);
            emit iceCandidateReceived(candidate.callId, candidate.candidate);
            break;
        }
        case MessageType::VIDEO_CALL_START: {
            VideoCallStart start = ProtocolManager::decodeVideoCallStart(payload);
            emit videoCallStarted(start.callId, start.width, start.height, start.fps);
            break;
        }
        case MessageType::VIDEO_CALL_STOP: {
            VideoCallStop stop = ProtocolManager::decodeVideoCallStop(payload);
            emit videoCallStopped(stop.callId);
            break;
        }
        case MessageType::VIDEO_CALL_FRAME: {
            VideoCallFrame frame = ProtocolManager::decodeVideoCallFrame(payload);
            emit videoCallFrameReceived(frame.callId, frame.frameData, frame.timestamp, frame.sequenceNumber, frame.isKeyFrame, frame.captureTime);
            break;
        }
        case MessageType::SCREEN_SHARE_START: {
            ScreenShareStart start = ProtocolManager::decodeScreenShareStart(payload);
            emit screenShareStarted(start.sessionId, start.width, start.height, start.fps);
            break;
        }
        case MessageType::SCREEN_SHARE_STOP: {
            ScreenShareStop stop = ProtocolManager::decodeScreenShareStop(payload);
            emit screenShareStopped(stop.sessionId);
            break;
        }
        case MessageType::SCREEN_SHARE_FRAME: {
            ScreenShareFrame frame = ProtocolManager::decodeScreenShareFrame(payload);
            emit screenShareFrameReceived(frame.sessionId, frame.frameData, frame.timestamp, frame.sequenceNumber, frame.isKeyFrame, frame.captureTime);
            break;
        }
        case MessageType::GROUP_ANNOUNCEMENT: {
            GroupAnnouncement announcement = ProtocolManager::decodeGroupAnnouncement(payload);
            emit groupAnnouncementReceived(announcement.groupId, announcement.groupName, announcement.announcement, announcement.announcerId, announcement.announcerName);
            break;
        }
        case MessageType::GROUP_MENTION: {
            GroupMention mention = ProtocolManager::decodeGroupMention(payload);
            emit groupMentionReceived(mention.groupId, mention.groupName, mention.message, mention.mentionedMemberIds, mention.mentionedMemberNames, mention.senderId, mention.senderName);
            break;
        }
        case MessageType::GROUP_VOTE: {
            GroupVote vote = ProtocolManager::decodeGroupVote(payload);
            emit groupVoteReceived(vote.groupId, vote.groupName, vote.voteTitle, vote.options, vote.durationSeconds, vote.creatorId, vote.creatorName);
            break;
        }
        case MessageType::GROUP_FILE: {
            GroupFile file = ProtocolManager::decodeGroupFile(payload);
            emit groupFileReceived(file.groupId, file.groupName, file.fileId, file.fileName, file.fileSize, file.md5, file.uploaderId, file.uploaderName);
            break;
        }
        case MessageType::GROUP_ALBUM: {
            GroupAlbum album = ProtocolManager::decodeGroupAlbum(payload);
            emit groupAlbumReceived(album.groupId, album.groupName, album.albumId, album.albumName, album.fileIds, album.fileNames, album.creatorId, album.creatorName);
            break;
        }
        case MessageType::GROUP_TODO: {
            GroupTodo todo = ProtocolManager::decodeGroupTodo(payload);
            emit groupTodoReceived(todo.groupId, todo.groupName, todo.todoId, todo.title, todo.description, todo.status, todo.priority, todo.assigneeId, todo.assigneeName, todo.creatorId, todo.creatorName, todo.dueDate);
            break;
        }
        case MessageType::GROUP_TODO_UPDATE: {
            GroupTodo todo = ProtocolManager::decodeGroupTodo(payload);
            emit groupTodoUpdated(todo.groupId, todo.groupName, todo.todoId, todo.status);
            break;
        }
        case MessageType::MONITOR_LIST: {
            int currentMonitorIndex = 0;
            QList<MonitorInfo> monitors = ProtocolManager::decodeMonitorList(payload, currentMonitorIndex);
            emit monitorListReceived(monitors, currentMonitorIndex);
            break;
        }
        case MessageType::MONITOR_SWITCH_ACK: {
            if (payload.size() >= 5) {
                QDataStream stream(payload);
                stream.setByteOrder(QDataStream::BigEndian);
                uint8_t success;
                uint32_t newIndex;
                stream >> success >> newIndex;
                emit monitorSwitchCompleted(success == 1, static_cast<int>(newIndex));
                LOG_INFO("Monitor switch " + QString(success ? "succeeded" : "failed") + 
                         " to index " + QString::number(newIndex));
            }
            break;
        }
        case MessageType::MONITOR_AUTO_SWITCH_STATUS: {
            if (payload.size() >= 20) {
                QDataStream stream(payload);
                stream.setByteOrder(QDataStream::BigEndian);
                uint8_t state;  // 0=stopped, 1=active, 2=paused
                int32_t intervalMs, currentIndex, monitorCount, nextIndex;
                stream >> state >> intervalMs >> currentIndex >> monitorCount >> nextIndex;
                emit autoSwitchStatusReceived(
                    state > 0, state == 2,
                    intervalMs, currentIndex, monitorCount, nextIndex);
                LOG_INFO("Auto-switch status: state=" + QString::number(state) +
                         " interval=" + QString::number(intervalMs) +
                         " current=" + QString::number(currentIndex));
            }
            break;
        }
        case MessageType::MONITOR_THUMBNAIL_FRAME: {
            if (payload.size() >= 16) {
                QDataStream stream(payload);
                stream.setByteOrder(QDataStream::BigEndian);
                int32_t monitorIndex, thumbWidth, thumbHeight, dataSize;
                stream >> monitorIndex >> thumbWidth >> thumbHeight >> dataSize;

                if (payload.size() >= 16 + dataSize) {
                    QByteArray thumbData = payload.mid(16, dataSize);
                    QImage thumbnail;
                    thumbnail.loadFromData(thumbData, "JPEG");
                    if (!thumbnail.isNull()) {
                        emit thumbnailFrameReceived(monitorIndex, thumbnail);
                    }
                }
            }
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
        case MessageType::PERMISSION_DENIED: {
            // v1.8.0 RBAC: host refused a capability the controller tried to use
            // (e.g. terminal without the Terminal bit). Surface it so the UI can
            // notify the operator instead of silently doing nothing.
            PermissionDenied denied = ProtocolManager::decodePermissionDenied(payload);
            LOG_WARNING("[Auth] Host denied capability " + QString::number(denied.capability) +
                        " reason=" + denied.reason);
            emit permissionDenied(static_cast<int>(denied.capability), denied.reason);
            break;
        }
        default:
            break;
    }
}

void RemoteController::handleScreenFrame(const QByteArray& data) {
    static bool firstRecv = false;
    if (!firstRecv) {
        firstRecv = true;
        LOG_INFO("[DIAG] Controller: FIRST SCREEN_FRAME received, size=" +
                 QString::number(data.size()) + "B, decodeWorker=" +
                 QString(m_decodeRunning ? "on" : "off"));
    }

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
        static bool firstDecrypt = false;
        if (!firstDecrypt) {
            firstDecrypt = true;
            LOG_INFO("[DIAG] Controller: FIRST frame decrypt " +
                     QString(frameData.isEmpty() ? "FAILED (empty result)" :
                             "OK, size=" + QString::number(frameData.size()) + "B"));
        }
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

    reportFrameDecodeResult(frame.format, true);
    emit screenFrameReceived(frame);
}

void RemoteController::handleScreenTile(const QByteArray& data) {
    if (m_tileQueue && m_decodeRunning) {
        if (!m_tileQueue->enqueueNonBlocking(data)) {
            // A dropped tile leaves a stale patch on screen, so count it as a
            // loss and ask for a full refresh.
            m_tilesLost.fetch_add(1);
            static int s_warn = 0;
            if (s_warn++ % 100 == 0) {
                LOG_WARNING("RemoteController: tile queue full — dropped tile, requesting keyframe");
            }
            requestKeyFrame();
        }
        return;
    }
    applyTile(data);
}

void RemoteController::applyTile(const QByteArray& data) {
    QByteArray plain = data;
    if (m_encryption && m_encryption->isInitialized()) {
        plain = m_encryption->decrypt(data);
        if (plain.isEmpty()) {
            m_tilesLost.fetch_add(1);
            return;
        }
    }

    ScreenTile tile = ProtocolManager::decodeScreenTile(plain);
    if (tile.w == 0 || tile.h == 0 || tile.data.isEmpty() ||
        tile.frameWidth == 0 || tile.frameHeight == 0) {
        m_tilesLost.fetch_add(1);
        return;
    }

    // Record the frame dimensions up front so a corrupt tile (dropped below)
    // still updates the size used to size the NACK request.
    m_lastFrameWidth.store(tile.frameWidth);
    m_lastFrameHeight.store(tile.frameHeight);

    if (!tile.hash.isEmpty() &&
        QCryptographicHash::hash(tile.data, QCryptographicHash::Md5) != tile.hash) {
        // Corrupted on the wire: drop it and ask the host to resend just this
        // tile (targeted NACK) instead of waiting for a full keyframe.
        m_tilesLost.fetch_add(1);
        {
            QMutexLocker lock(&m_nackMutex);
            m_pendingNack.append(QPoint(static_cast<int>(tile.x), static_cast<int>(tile.y)));
        }
        // applyTile runs on the decode thread; the NACK timer lives on the
        // controller's (main) thread, so start it via a queued call.
        QMetaObject::invokeMethod(m_nackTimer, "start", Qt::QueuedConnection,
                                  Q_ARG(int, 100));
        static int s_warn = 0;
        if (s_warn++ % 100 == 0) {
            LOG_WARNING("RemoteController: tile MD5 mismatch (corrupt on wire) — requesting tile resend");
        }
        return;
    }

    const QImage patch = TileEncoder::decodeTilePixels(
        tile.data, static_cast<TileEncoding>(tile.encoding),
        static_cast<int>(tile.w), static_cast<int>(tile.h));
    if (patch.isNull()) {
        m_tilesLost.fetch_add(1);
        return;
    }

    const QSize frameSize(static_cast<int>(tile.frameWidth), static_cast<int>(tile.frameHeight));
    if (m_tileCanvas.size() != frameSize) {
        m_tileCanvas = QImage(frameSize, QImage::Format_RGB32);
        m_tileCanvas.fill(Qt::black);
    }

    QPainter painter(&m_tileCanvas);
    painter.drawImage(QPoint(static_cast<int>(tile.x), static_cast<int>(tile.y)), patch);
    painter.end();

    m_tilesReceived.fetch_add(1);
    m_lastTileTimestamp.store(tile.timestamp);

    // Coalesce: publish the canvas once a batch has drained or every ~16ms,
    // so a 200-tile keyframe does not trigger 200 full-frame repaints.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool batchDone = !m_tileQueue || m_tileQueue->isEmpty();
    if (batchDone || now - m_lastCanvasEmitMs >= 16) {
        m_lastCanvasEmitMs = now;
        emit screenImageReceived(m_tileCanvas.copy());
    }
}

void RemoteController::startDecodeWorker() {
    stopDecodeWorker();
    m_decodeQueue = new FrameQueue<QByteArray>(4);
    // Tiles are far smaller than whole frames and arrive in bursts (a keyframe
    // is the entire grid at once), so this queue needs a lot more headroom.
    m_tileQueue = new FrameQueue<QByteArray>(256);
    m_decodeRunning = true;
    m_decodeThread = new QThread(this);

    // Use a context object whose thread affinity is the decode thread so that
    // the lambda runs on the decode thread (not the main thread).
    m_decodeWorkerCtx = new QObject();
    m_decodeWorkerCtx->moveToThread(m_decodeThread);

    QObject::connect(m_decodeThread, &QThread::started, m_decodeWorkerCtx, [this]() {
        while (m_decodeRunning) {
            // Tiled updates take priority: they are small and latency-sensitive,
            // and the host never sends both kinds at once.
            QByteArray tileData = m_tileQueue->dequeue(1);
            if (!tileData.isEmpty()) {
                applyTile(tileData);
                continue;
            }

            QByteArray data = m_decodeQueue->dequeue(15);
            if (data.isEmpty()) continue;

            static int frameCount = 0;
            static int decryptFailCount = 0;
            frameCount++;

            bool hasEncryption = m_encryption && m_encryption->isInitialized();

            QByteArray frameData = data;
            if (hasEncryption) {
                frameData = m_encryption->decrypt(data);
                static bool firstDecrypt = false;
                if (!firstDecrypt) {
                    firstDecrypt = true;
                    LOG_INFO("[DIAG] Controller: (decode-worker) FIRST frame decrypt " +
                             QString(frameData.isEmpty() ? "FAILED (empty result)" :
                                     "OK, size=" + QString::number(frameData.size()) + "B"));
                }
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
                reportFrameDecodeResult(frame.format, false);
                continue;
            }

            reportFrameDecodeResult(frame.format, true);
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
    if (m_tileQueue) {
        m_tileQueue->clear();
        delete m_tileQueue;
        m_tileQueue = nullptr;
    }
    m_tileCanvas = QImage();
    m_tilesReceived = 0;
    m_tilesLost = 0;
    LOG_INFO("RemoteController: decode worker stopped");
}

void RemoteController::reportFrameDecodeResult(FrameFormat format, bool ok) {
    if (format != FrameFormat::H264) {
        // JPEG (or unknown) decoded fine -> reset any H264 streak.
        m_h264FailStreak = 0;
        return;
    }
    if (ok) {
        m_h264FailStreak = 0;
        return;
    }
    ++m_h264FailStreak;
    // After a short streak of H264 decode failures, ask the host to switch to
    // JPEG. This is a one-shot request: once sent we stop nagging, and the host
    // only moves to H264 again if the user explicitly picks the game gear.
    if (!m_h264FallbackRequested && m_h264FailStreak >= 30) {
        m_h264FallbackRequested = true;
        LOG_WARNING("[Decode] " + QString::number(m_h264FailStreak) +
                    " consecutive H264 decode failures - requesting host switch to JPEG");
        sendQualityLevel(QualityLevel::MEDIUM, false);
    }
}

void RemoteController::handleAuthResponse(const QByteArray& data) {
    LOG_INFO("[Auth] Response received, size=" + QString::number(data.size()) +
             " first8=0x" + data.left(8).toHex());

    // v1.8.0 RBAC: the host sends a structured AuthResponse carrying the session
    // key/iv AND the granted permission level + effective capability mask.
    AuthResponse resp = ProtocolManager::decodeAuthResponse(data);

    if (!resp.ok) {
        // Legacy failure strings ("NO" / "NOT_AUTHORIZED" / "FAILED").
        if (data.size() >= 14 && QString::fromUtf8(data.left(14)) == "NOT_AUTHORIZED") {
            emit authRequired();
            LOG_WARNING("Authentication required");
        } else {
            QString full = QString::fromUtf8(data);
            emit authFailed(full);
            LOG_WARNING("Authentication failed: " + full);
        }
        return;
    }

    QByteArray encKey = resp.sessionKey;
    QByteArray encIv = resp.iv;
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

    m_grantedLevel = static_cast<PermLevel>(resp.grantedLevel);
    m_grantedCaps = resp.grantedCaps;
    LOG_INFO("[Auth] Granted level=" + QString::number(resp.grantedLevel) +
             " caps=0x" + QString::number(resp.grantedCaps, 16));

    emit authSuccess();
    emit capabilitiesChanged(static_cast<int>(m_grantedLevel), m_grantedCaps);
    requestMonitorList();
    m_heartbeatTimer->start(2000);
    // Advertises tile support and asks for the initial full screen. A host
    // that does not know SCREEN_KEYFRAME simply ignores it and keeps
    // sending whole frames.
    requestKeyFrame();
    m_ackTimer->start(1000);
    // New session: clear any prior H264 decode-fallback state.
    m_h264FailStreak = 0;
    m_h264FallbackRequested = false;
    LOG_INFO("Authentication successful");
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

void RemoteController::recordConnectionResult(bool success, const QString& errorMsg, int reconnectAttempts) {
    if (!m_connectionStartTime.isValid()) {
        return;
    }

    qint64 duration = m_connectionStartTime.msecsTo(QDateTime::currentDateTime());
    
    xrk::ConnectionRecord record = m_currentConnectionRecord;
    record.success = success;
    record.errorMessage = errorMsg;
    record.duration = duration;
    record.reconnectAttempts = reconnectAttempts;
    record.bytesSent = m_connection ? m_connection->bytesWritten() : 0;
    record.bytesReceived = m_connection ? m_connection->bytesAvailable() : 0;
    // Try to get bytes from TcpConnection history if available
    if (m_connection) {
        record.bytesSent = m_connection->bytesWritten();
        // bytesReceived is not directly accessible, use 0
    }

    m_historyManager->recordConnection(record);
    m_connectionStartTime = QDateTime();  // Reset
}

} // namespace xrk
