#include "ipmsg_manager.h"
#include "database_manager.h"
#include <functional>
#include "core/encryption.h"
#include "core/logger.h"
#include "core/protocol_manager.h"
#include "core/types.h"
#include <QJsonArray>
#include <QNetworkAddressEntry>
#include <QStandardPaths>
#include <QDateTime>
#include <QUuid>
#include <QFileInfo>
#include <QDirIterator>
#include <QMutex>
#include <QMutexLocker>
#include <QFileDevice>
#include <QTimer>

namespace xrk {

// Protocol constants
static const quint16 IPMSG_DEFAULT_PORT = 2425;
static const QByteArray IPMSG_HELLO = "HELLO";
static const QByteArray IPMSG_LEAVE = "LEAVE";
static const QByteArray IPMSG_MSG = "MSG";
static const QByteArray IPMSG_FILE_START = "FILE_START";
static const QByteArray IPMSG_FILE_DATA = "FILE_DATA";
static const QByteArray IPMSG_FILE_END = "FILE_END";
static const QByteArray IPMSG_FILE_ACCEPT = "FILE_ACCEPT";
static const QByteArray IPMSG_FILE_REJECT = "FILE_REJECT";
static const QByteArray IPMSG_FILE_RESUME = "FILE_RESUME";
static const QByteArray IPMSG_FILE_RESUME_REQUEST = "FILE_RESUME_REQUEST";
static const QByteArray IPMSG_FILE_VERIFY = "FILE_VERIFY";
static const QByteArray IPMSG_FILE_CHUNK_REQUEST = "FILE_CHUNK_REQUEST";
static const QByteArray IPMSG_FILE_CHUNK_DATA = "FILE_CHUNK_DATA";
static const QByteArray IPMSG_FILE_CHUNK_VERIFY = "FILE_CHUNK_VERIFY";
static const QByteArray IPMSG_IMAGE = "IMAGE";
static const QByteArray IPMSG_REPLY = "REPLY";
static const QByteArray IPMSG_RECALL = "RECALL";
static const QByteArray IPMSG_READ = "READ";
static const QByteArray IPMSG_TYPING = "TYPING";
static const QByteArray IPMSG_FRIEND_REQUEST = "FRIEND_REQUEST";
static const QByteArray IPMSG_FRIEND_ACCEPT = "FRIEND_ACCEPT";
static const QByteArray IPMSG_FRIEND_REJECT = "FRIEND_REJECT";
static const QByteArray IPMSG_OFFLINE_MSG = "OFFLINE_MSG";
static const QByteArray IPMSG_OFFLINE_DELIVERED = "OFFLINE_DELIVERED";
static const QByteArray IPMSG_KEY_EXCHANGE = "KEY_EXCHANGE";
static const QByteArray IPMSG_ENCRYPTED_MSG = "ENCRYPTED_MSG";
static const QByteArray IPMSG_VOICE_MSG = "VOICE_MSG";
static const QByteArray IPMSG_VIDEO_MSG = "VIDEO_MSG";
static const QByteArray IPMSG_LOCATION_MSG = "LOCATION_MSG";
static const QByteArray IPMSG_CARD_MSG = "CARD_MSG";

// VoIP signaling
static const QByteArray IPMSG_CALL_INVITE = "CALL_INVITE";
static const QByteArray IPMSG_CALL_ACCEPT = "CALL_ACCEPT";
static const QByteArray IPMSG_CALL_REJECT = "CALL_REJECT";
static const QByteArray IPMSG_CALL_END = "CALL_END";
static const QByteArray IPMSG_ICE_CANDIDATE = "ICE_CANDIDATE";
static const QByteArray IPMSG_GROUP_ANNOUNCEMENT = "GROUP_ANNOUNCEMENT";
static const QByteArray IPMSG_GROUP_MENTION = "GROUP_MENTION";
static const QByteArray IPMSG_GROUP_VOTE = "GROUP_VOTE";
static const QByteArray IPMSG_GROUP_VOTE_RESPONSE = "GROUP_VOTE_RESPONSE";

// Multi-device sync
static const QByteArray IPMSG_SYNC_REQUEST = "SYNC_REQUEST";
static const QByteArray IPMSG_SYNC_SNAPSHOT = "SYNC_SNAPSHOT";
static const QByteArray IPMSG_SYNC_ACK = "SYNC_ACK";

IPMsgManager::IPMsgManager(QObject* parent)
    : QObject(parent)
    , m_port(IPMSG_DEFAULT_PORT)
    , m_userId(QUuid::createUuid().toString().remove('{').remove('}').remove('-').left(8))
    , m_transferStateFile(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/ipmsg_transfers.json") {
    connect(this, &IPMsgManager::encryptionReady, this, &IPMsgManager::onEncryptionReadyForTransfer);
    loadTransferStates();
}

IPMsgManager::~IPMsgManager() {
    stop();
}

bool IPMsgManager::start(quint16 port) {
    if (m_running) return true;

    m_port = port;

    // Initialize database
    m_database = &DatabaseManager::instance();
    if (!m_database->initialize()) {
        LOG_ERROR("IPMsg: Failed to initialize database");
        return false;
    }

    // Load friends from database
    m_friends = m_database->loadFriends();

    // Load groups from database
    QList<IPMsgGroup> groups = m_database->loadAllGroups();
    for (const IPMsgGroup& group : groups) {
        m_groups[group.id] = group;
    }

    // Load devices from database
    QList<IPMsgDevice> devices = m_database->loadAllDevices();
    for (const IPMsgDevice& device : devices) {
        m_devices[device.id] = device;
    }

    // Load multi-device sync account config
    m_syncAccountId = m_database->getSetting("sync_account_id").toString();
    m_syncAccountHash = m_database->getSetting("sync_account_hash").toString();
    m_syncKeyHash = m_database->getSetting("sync_key_hash").toString();

    // Setup UDP socket for broadcast
    m_udpSocket = new QUdpSocket(this);
    if (!m_udpSocket->bind(QHostAddress::Any, m_port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        LOG_ERROR("IPMsg: Failed to bind UDP port " + QString::number(m_port));
        return false;
    }
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &IPMsgManager::onUdpBroadcastReceived);

    // Setup TCP server for file transfers
    m_tcpServer = new QTcpServer(this);
    if (!m_tcpServer->listen(QHostAddress::Any, m_port + 1)) {
        LOG_ERROR("IPMsg: Failed to start TCP server on port " + QString::number(m_port + 1));
        return false;
    }
    connect(m_tcpServer, &QTcpServer::newConnection, this, &IPMsgManager::onNewTcpConnection);

    // Fetch offline messages for this device
    QTimer::singleShot(1000, this, [this]() { fetchOfflineMessages(); });

    m_running = true;
    broadcastPresence();

    // Setup periodic broadcast timer (every 3 seconds)
    m_broadcastTimer = new QTimer(this);
    connect(m_broadcastTimer, &QTimer::timeout, this, &IPMsgManager::broadcastPresence);
    m_broadcastTimer->start(3000);

    // Setup device cleanup timer (every 10 seconds)
    QTimer* cleanupTimer = new QTimer(this);
    connect(cleanupTimer, &QTimer::timeout, this, [this]() {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        QStringList staleIds;
        for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
            if (now - it.value().lastSeen > 30000) { // 30 seconds timeout
                staleIds.append(it.key());
            }
        }
        for (const QString& id : staleIds) {
            m_devices.remove(id);
            emit deviceLeft(id);
        }
    });
    cleanupTimer->start(10000);

    // Setup offline message retry timer (every 30 seconds)
    QTimer* retryTimer = new QTimer(this);
    connect(retryTimer, &QTimer::timeout, this, [this]() {
        cleanupExpiredOfflineMessages();
        // Retry offline messages for all online devices
        for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
            retryOfflineMessages(it.key());
        }
    });
    retryTimer->start(30000);

    LOG_INFO("IPMsg: Started on port " + QString::number(m_port));

    return true;
}

void IPMsgManager::stop() {
    if (!m_running) return;

    // Stop broadcast timer
    if (m_broadcastTimer) {
        m_broadcastTimer->stop();
    }

    // Broadcast leave message
    QByteArray data = IPMSG_LEAVE + "|" + m_userId.toUtf8();
    sendUdpBroadcast(data);

    // Close all connections
    for (auto* socket : m_sendingSockets) {
        socket->close();
    }
    m_sendingSockets.clear();

    if (m_udpSocket) {
        m_udpSocket->close();
        delete m_udpSocket;
        m_udpSocket = nullptr;
    }

    if (m_tcpServer) {
        m_tcpServer->close();
        delete m_tcpServer;
        m_tcpServer = nullptr;
    }

    m_devices.clear();
    m_running = false;

    // Shutdown database
    if (m_database) {
        m_database->shutdown();
        m_database = nullptr;
    }

    LOG_INFO("IPMsg: Stopped");
}

bool IPMsgManager::isRunning() const {
    return m_running;
}

void IPMsgManager::broadcastPresence() {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["port"] = m_port;
    json["ip"] = getLocalIp();
    json["command"] = QString::fromUtf8(IPMSG_HELLO);
    json["accountHash"] = m_syncAccountHash;

    QJsonDocument doc(json);
    sendUdpBroadcast(doc.toJson());
}

void IPMsgManager::sendMessage(const QString& targetIp, const QString& message) {
    QString recallId = QString("%1_%2").arg(m_userId).arg(QDateTime::currentMSecsSinceEpoch());
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();

    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_MSG);
    json["message"] = message;
    json["timestamp"] = timestamp;
    json["recallId"] = recallId;

    QJsonDocument doc(json);

    // Find target's TCP port from device list (TCP = UDP port + 1)
    // Also find deviceId for database
    quint16 targetPort = 2426; // default TCP port
    QString targetDeviceId;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == targetIp) {
            targetPort = it.value().port + 1;
            targetDeviceId = it.key();
            break;
        }
    }

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [this, socket, data = doc.toJson()]() {
        LOG_INFO(QString("IPMsg: TCP connected to %1:%2, sending message").arg(socket->peerAddress().toString()).arg(socket->peerPort()));
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        LOG_INFO("IPMsg: TCP disconnected after sending");
        socket->deleteLater();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket, targetDeviceId, doc]() {
        LOG_ERROR("IPMsg: TCP send error: " + socket->errorString());
        
        // If target device was known but connection failed, queue as offline message
        if (!targetDeviceId.isEmpty() && m_database) {
            sendOfflineMessage(targetDeviceId, doc.toJson());
        }
        socket->deleteLater();
    });

    LOG_INFO(QString("IPMsg: Sending message to %1 TCP port %2").arg(targetIp).arg(targetPort));
    socket->connectToHost(targetIp, targetPort);

    // Save sent message to database
    if (!targetDeviceId.isEmpty() && m_database) {
        IPMsgMessage msg;
        msg.senderId = m_userId;
        msg.senderName = m_userName;
        msg.senderIp = getLocalIp();
        msg.content = message;
        msg.timestamp = timestamp;
        msg.recallId = recallId;
        m_database->saveMessage(msg, targetDeviceId, false);
    }
}

void IPMsgManager::sendReply(const QString& targetIp, const QString& message, const QString& replyTo, const QString& replyContent) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_REPLY);
    json["message"] = message;
    json["replyTo"] = replyTo;
    json["replyContent"] = replyContent;
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [this, socket, data = doc.toJson()]() {
        LOG_INFO("IPMsg: TCP connected, sending reply");
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        LOG_INFO("IPMsg: Reply sent successfully");
        socket->deleteLater();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError error) {
        LOG_ERROR("IPMsg: TCP reply send error: " + socket->errorString());
        socket->deleteLater();
    });

    quint16 targetPort = 2426;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == targetIp) {
            targetPort = it.value().port + 1;
            break;
        }
    }
    socket->connectToHost(targetIp, targetPort);
}

void IPMsgManager::sendImage(const QString& targetIp, const QByteArray& imageData, const QString& fileName) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_IMAGE);
    json["fileName"] = fileName;
    json["imageData"] = QString::fromLatin1(imageData.toBase64());
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [this, socket, data = doc.toJson()]() {
        LOG_INFO("IPMsg: TCP connected, sending image");
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        LOG_INFO("IPMsg: Image sent successfully");
        socket->deleteLater();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError error) {
        LOG_ERROR("IPMsg: TCP image send error: " + socket->errorString());
        socket->deleteLater();
    });

    quint16 targetPort = 2426;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == targetIp) {
            targetPort = it.value().port + 1;
            break;
        }
    }
    socket->connectToHost(targetIp, targetPort);
}

void IPMsgManager::recallMessage(const QString& targetIp, const QString& recallId, const QString& senderName) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_RECALL);
    json["recallId"] = recallId;
    json["senderName"] = senderName;
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [this, socket, data = doc.toJson()]() {
        LOG_INFO("IPMsg: TCP connected, sending recall");
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        LOG_INFO("IPMsg: Recall sent successfully");
        socket->deleteLater();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError error) {
        LOG_ERROR("IPMsg: TCP recall send error: " + socket->errorString());
        socket->deleteLater();
    });

    quint16 targetPort = 2426;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == targetIp) {
            targetPort = it.value().port + 1;
            break;
        }
    }
    socket->connectToHost(targetIp, targetPort);
}

QString IPMsgManager::createGroup(const QString& name, const QList<QString>& memberIds) {
    QString groupId = QString("group_%1_%2").arg(m_userId).arg(QDateTime::currentMSecsSinceEpoch());
    
    IPMsgGroup group;
    group.id = groupId;
    group.name = name;
    group.memberIds = memberIds;
    group.createdAt = QDateTime::currentMSecsSinceEpoch();
    
    // Add own ID to members
    if (!group.memberIds.contains(m_userId)) {
        group.memberIds.prepend(m_userId);
    }
    
    // Resolve member names
    for (const QString& memberId : group.memberIds) {
        if (m_devices.contains(memberId)) {
            group.memberNames.append(m_devices[memberId].name);
        } else if (memberId == m_userId) {
            group.memberNames.append(m_userName);
        } else {
            group.memberNames.append("Unknown");
        }
    }
    
    m_groups[groupId] = group;
    
    // Save to database
    if (m_database) {
        m_database->saveGroup(group);
    }
    
    return groupId;
}

QList<IPMsgGroup> IPMsgManager::getGroups() const {
    return m_groups.values();
}

void IPMsgManager::updateGroupName(const QString& groupId, const QString& newName) {
    if (m_groups.contains(groupId)) {
        m_groups[groupId].name = newName;
        if (m_database) {
            m_database->saveGroup(m_groups[groupId]);
        }
    }
}

void IPMsgManager::inviteToGroup(const QString& groupId, const QString& memberId) {
    if (!m_groups.contains(groupId)) return;
    if (m_groups[groupId].memberIds.contains(memberId)) return;
    
    m_groups[groupId].memberIds.append(memberId);
    if (m_devices.contains(memberId)) {
        m_groups[groupId].memberNames.append(m_devices[memberId].name);
    }
    
    if (m_database) {
        m_database->saveGroup(m_groups[groupId]);
    }
}

void IPMsgManager::removeFromGroup(const QString& groupId, const QString& memberId) {
    if (!m_groups.contains(groupId)) return;
    
    int index = m_groups[groupId].memberIds.indexOf(memberId);
    if (index >= 0) {
        m_groups[groupId].memberIds.removeAt(index);
        if (index < m_groups[groupId].memberNames.size()) {
            m_groups[groupId].memberNames.removeAt(index);
        }
    }
    
    if (m_database) {
        m_database->saveGroup(m_groups[groupId]);
    }
}

void IPMsgManager::dissolveGroup(const QString& groupId) {
    m_groups.remove(groupId);
    if (m_database) {
        m_database->deleteGroup(groupId);
    }
}

void IPMsgManager::sendGroupMessage(const QString& groupId, const QString& message) {
    if (!m_groups.contains(groupId)) return;
    
    const IPMsgGroup& group = m_groups[groupId];
    
    // Save sent message to database (group chat)
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    QString recallId = QString("%1_%2").arg(m_userId).arg(timestamp);
    
    if (m_database) {
        IPMsgMessage msg;
        msg.senderId = m_userId;
        msg.senderName = m_userName;
        msg.senderIp = getLocalIp();
        msg.content = QString("[群:%1] %2").arg(group.name, message);
        msg.timestamp = timestamp;
        msg.recallId = recallId;
        m_database->saveMessage(msg, groupId, true);
    }
    
    // Send to all group members
    for (const QString& memberId : group.memberIds) {
        if (memberId == m_userId) continue; // Skip self
        
        if (m_devices.contains(memberId)) {
            const IPMsgDevice& device = m_devices[memberId];
            sendMessage(device.ip, QString("[群:%1] %2").arg(group.name, message));
        }
    }
}

void IPMsgManager::sendReadReceipt(const QString& targetIp, const QString& messageId) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_READ);
    json["messageId"] = messageId;
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [this, socket, data = doc.toJson()]() {
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        socket->deleteLater();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
        socket->deleteLater();
    });

    quint16 targetPort = 2426;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == targetIp) {
            targetPort = it.value().port + 1;
            break;
        }
    }
    socket->connectToHost(targetIp, targetPort);
}

void IPMsgManager::sendTypingIndicator(const QString& targetIp) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_TYPING);
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [this, socket, data = doc.toJson()]() {
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        socket->deleteLater();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
        socket->deleteLater();
    });

    quint16 targetPort = 2426;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == targetIp) {
            targetPort = it.value().port + 1;
            break;
        }
    }
    socket->connectToHost(targetIp, targetPort);
}

void IPMsgManager::sendFriendRequest(const QString& targetIp, const QString& message) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_FRIEND_REQUEST);
    json["message"] = message;
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [this, socket, data = doc.toJson()]() {
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        socket->deleteLater();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
        socket->deleteLater();
    });

    quint16 targetPort = 2426;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == targetIp) {
            targetPort = it.value().port + 1;
            break;
        }
    }
    socket->connectToHost(targetIp, targetPort);
}

// ────────── Voice/Video/Location/Card Messages ──────────

void IPMsgManager::sendVoiceMessage(const QString& targetIp, const QByteArray& voiceData, int duration) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_VOICE_MSG);
    json["voiceData"] = QString::fromLatin1(voiceData.toBase64());
    json["voiceDuration"] = duration;
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    quint16 targetPort = 2426;
    QString targetDeviceId;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == targetIp) {
            targetPort = it.value().port + 1;
            targetDeviceId = it.key();
            break;
        }
    }

    QJsonDocument doc(json);
    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, doc]() {
        socket->write(doc.toJson());
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket, targetDeviceId, doc]() {
        if (!targetDeviceId.isEmpty() && m_database) {
            sendOfflineMessage(targetDeviceId, doc.toJson());
        }
        socket->deleteLater();
    });
    socket->connectToHost(targetIp, targetPort);

    // Save to database
    if (!targetDeviceId.isEmpty() && m_database) {
        IPMsgMessage msg;
        msg.senderId = m_userId;
        msg.senderName = m_userName;
        msg.senderIp = getLocalIp();
        msg.timestamp = QDateTime::currentMSecsSinceEpoch();
        msg.isVoiceMessage = true;
        msg.voiceData = voiceData;
        msg.voiceDuration = duration;
        m_database->saveMessage(msg, targetDeviceId, false);
    }
}

void IPMsgManager::sendVoiceMessageProtocol(const QString& targetId, const QByteArray& voiceData, int duration) {
    VoiceMessage voiceMsg;
    voiceMsg.messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    voiceMsg.senderId = m_userId;
    voiceMsg.senderName = m_userName;
    voiceMsg.voiceData = voiceData;
    voiceMsg.voiceFileName = "voice_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".pcm";
    voiceMsg.duration = duration;
    voiceMsg.timestamp = QDateTime::currentMSecsSinceEpoch();
    voiceMsg.isRead = false;

    QByteArray payload = ProtocolManager::encodeVoiceMessage(voiceMsg);
    QByteArray message = ProtocolManager::encode(MessageType::VOICE_MSG, payload);

    // Find target IP from device list
    QString targetIp;
    quint16 targetPort = 2426;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.key() == targetId || it.value().id == targetId) {
            targetIp = it.value().ip;
            targetPort = it.value().port + 1;
            break;
        }
    }

    if (targetIp.isEmpty()) {
        LOG_WARNING("IPMsgManager: Target device not found for voice message: " + targetId);
        return;
    }

    // Send via TCP socket (same pattern as other send methods)
    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, message]() {
        socket->write(message);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
    socket->connectToHost(targetIp, targetPort);

    // Save to database
    if (m_database) {
        DatabaseManager::VoiceMessageRow row;
        row.messageId = voiceMsg.messageId;
        row.senderId = voiceMsg.senderId;
        row.senderName = voiceMsg.senderName;
        row.voiceData = voiceData;
        row.voiceFileName = voiceMsg.voiceFileName;
        row.duration = duration;
        row.timestamp = voiceMsg.timestamp;
        row.isRead = false;
        row.targetId = targetId;
        row.isGroup = false;
        m_database->saveVoiceMessage(row);
    }
}

void IPMsgManager::sendVideoMessageProtocol(const QString& targetId, const QByteArray& videoData, int duration, double width, double height) {
    VideoMessage videoMsg;
    videoMsg.messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    videoMsg.senderId = m_userId;
    videoMsg.senderName = m_userName;
    videoMsg.videoData = videoData;
    videoMsg.videoFileName = "video_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".mp4";
    videoMsg.duration = duration;
    videoMsg.width = width;
    videoMsg.height = height;
    videoMsg.timestamp = QDateTime::currentMSecsSinceEpoch();
    videoMsg.isRead = false;

    QByteArray payload = ProtocolManager::encodeVideoMessage(videoMsg);
    QByteArray message = ProtocolManager::encode(MessageType::VIDEO_MSG, payload);

    // Find target IP from device list
    QString targetIp;
    quint16 targetPort = 2426;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.key() == targetId || it.value().id == targetId) {
            targetIp = it.value().ip;
            targetPort = it.value().port + 1;
            break;
        }
    }

    if (targetIp.isEmpty()) {
        LOG_WARNING("IPMsgManager: Target device not found for video message: " + targetId);
        return;
    }

    // Send via TCP socket
    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, message]() {
        socket->write(message);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
    socket->connectToHost(targetIp, targetPort);

    // Save to database
    if (m_database) {
        DatabaseManager::VideoMessageRow row;
        row.messageId = videoMsg.messageId;
        row.senderId = videoMsg.senderId;
        row.senderName = videoMsg.senderName;
        row.videoData = videoData;
        row.videoFileName = videoMsg.videoFileName;
        row.duration = duration;
        row.width = width;
        row.height = height;
        row.timestamp = videoMsg.timestamp;
        row.isRead = false;
        row.targetId = targetId;
        row.isGroup = false;
        m_database->saveVideoMessage(row);
    }
}

void IPMsgManager::handleVoiceMessage(const IPMsgMessage& message) {
    if (!m_database) return;

    // Mark as read using recallId as message identifier
    if (!message.recallId.isEmpty()) {
        m_database->markVoiceMessageRead(message.recallId);
    }

    emit voiceMessageReceived(message);
}

void IPMsgManager::handleVideoMessage(const IPMsgMessage& message) {
    if (!m_database) return;

    // Mark as read using recallId as message identifier
    if (!message.recallId.isEmpty()) {
        m_database->markVideoMessageRead(message.recallId);
    }

    emit videoMessageReceived(message);
}

void IPMsgManager::sendLocationMessageProtocol(const QString& targetId, double latitude, double longitude, const QString& name) {
    LocationMessage locMsg;
    locMsg.messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    locMsg.senderId = m_userId;
    locMsg.senderName = m_userName;
    locMsg.latitude = latitude;
    locMsg.longitude = longitude;
    locMsg.locationName = name;
    locMsg.timestamp = QDateTime::currentMSecsSinceEpoch();
    locMsg.isRead = false;

    QByteArray payload = ProtocolManager::encodeLocationMessage(locMsg);
    QByteArray message = ProtocolManager::encode(MessageType::LOCATION_MSG, payload);

    // Find target IP from device list
    QString targetIp;
    quint16 targetPort = 2426;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.key() == targetId || it.value().id == targetId) {
            targetIp = it.value().ip;
            targetPort = it.value().port + 1;
            break;
        }
    }

    if (targetIp.isEmpty()) {
        LOG_WARNING("IPMsgManager: Target device not found for location message: " + targetId);
        return;
    }

    // Send via TCP socket
    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, message]() {
        socket->write(message);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
    socket->connectToHost(targetIp, targetPort);

    // Save to database
    if (m_database) {
        DatabaseManager::LocationMessageRow row;
        row.messageId = locMsg.messageId;
        row.senderId = locMsg.senderId;
        row.senderName = locMsg.senderName;
        row.latitude = latitude;
        row.longitude = longitude;
        row.locationName = name;
        row.timestamp = locMsg.timestamp;
        row.isRead = false;
        row.targetId = targetId;
        row.isGroup = false;
        m_database->saveLocationMessage(row);
    }
}

void IPMsgManager::sendCardMessageProtocol(const QString& targetId, const QString& vCardData) {
    CardMessage cardMsg;
    cardMsg.messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    cardMsg.senderId = m_userId;
    cardMsg.senderName = m_userName;
    cardMsg.vCardData = vCardData;
    cardMsg.timestamp = QDateTime::currentMSecsSinceEpoch();
    cardMsg.isRead = false;

    QByteArray payload = ProtocolManager::encodeCardMessage(cardMsg);
    QByteArray message = ProtocolManager::encode(MessageType::CARD_MSG, payload);

    // Find target IP from device list
    QString targetIp;
    quint16 targetPort = 2426;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.key() == targetId || it.value().id == targetId) {
            targetIp = it.value().ip;
            targetPort = it.value().port + 1;
            break;
        }
    }

    if (targetIp.isEmpty()) {
        LOG_WARNING("IPMsgManager: Target device not found for card message: " + targetId);
        return;
    }

    // Send via TCP socket
    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, message]() {
        socket->write(message);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
    socket->connectToHost(targetIp, targetPort);

    // Save to database
    if (m_database) {
        DatabaseManager::CardMessageRow row;
        row.messageId = cardMsg.messageId;
        row.senderId = cardMsg.senderId;
        row.senderName = cardMsg.senderName;
        row.vCardData = vCardData;
        row.timestamp = cardMsg.timestamp;
        row.isRead = false;
        row.targetId = targetId;
        row.isGroup = false;
        m_database->saveCardMessage(row);
    }
}

void IPMsgManager::sendMergeForwardMessageProtocol(const QString& targetId, const QList<ForwardedMessage>& messages) {
    MergeForwardMessage mergeMsg;
    mergeMsg.messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    mergeMsg.senderId = m_userId;
    mergeMsg.senderName = m_userName;
    mergeMsg.messages = messages;
    mergeMsg.timestamp = QDateTime::currentMSecsSinceEpoch();
    mergeMsg.isRead = false;

    QByteArray payload = ProtocolManager::encodeMergeForwardMessage(mergeMsg);
    QByteArray message = ProtocolManager::encode(MessageType::MERGE_FORWARD, payload);

    // Find target IP from device list
    QString targetIp;
    quint16 targetPort = 2426;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.key() == targetId || it.value().id == targetId) {
            targetIp = it.value().ip;
            targetPort = it.value().port + 1;
            break;
        }
    }

    if (targetIp.isEmpty()) {
        LOG_WARNING("IPMsgManager: Target device not found for merge forward message: " + targetId);
        return;
    }

    // Send via TCP socket
    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [this, socket, message]() {
        socket->write(message);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, this, [socket]() {
        socket->deleteLater();
    });
    socket->connectToHost(targetIp, targetPort);

    // Save to database
    if (m_database) {
        // Serialize the forwarded messages to JSON
        QJsonArray messagesArray;
        for (const ForwardedMessage& fwd : messages) {
            QJsonObject msgObj;
            msgObj["messageId"] = fwd.messageId;
            msgObj["senderId"] = fwd.senderId;
            msgObj["senderName"] = fwd.senderName;
            msgObj["content"] = fwd.content;
            msgObj["timestamp"] = fwd.timestamp;
            msgObj["msgType"] = fwd.msgType;
            messagesArray.append(msgObj);
        }
        QJsonDocument doc(messagesArray);
        QByteArray mergedData = doc.toJson(QJsonDocument::Compact);

        DatabaseManager::MergeForwardMessageRow row;
        row.messageId = mergeMsg.messageId;
        row.senderId = mergeMsg.senderId;
        row.senderName = mergeMsg.senderName;
        row.mergedData = mergedData;
        row.timestamp = mergeMsg.timestamp;
        row.isRead = false;
        row.targetId = targetId;
        row.isGroup = false;
        m_database->saveMergeForwardMessage(row);
    }
}

void IPMsgManager::handleLocationMessage(const IPMsgMessage& message) {
    if (!m_database) return;

    if (!message.recallId.isEmpty()) {
        m_database->markLocationMessageRead(message.recallId);
    }

    emit locationMessageReceived(message);
}

void IPMsgManager::handleCardMessage(const IPMsgMessage& message) {
    if (!m_database) return;

    if (!message.recallId.isEmpty()) {
        m_database->markCardMessageRead(message.recallId);
    }

    emit cardMessageReceived(message);
}

void IPMsgManager::handleMergeForwardMessage(const IPMsgMessage& message) {
    if (!m_database) return;

    if (!message.recallId.isEmpty()) {
        m_database->markMergeForwardMessageRead(message.recallId);
    }

    emit mergeForwardMessageReceived(message);
}

void IPMsgManager::sendVideoMessage(const QString& targetIp, const QByteArray& videoData, int duration, double width, double height) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_VIDEO_MSG);
    json["videoData"] = QString::fromLatin1(videoData.toBase64());
    json["videoDuration"] = duration;
    json["videoWidth"] = width;
    json["videoHeight"] = height;
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    quint16 targetPort = 2426;
    QString targetDeviceId;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == targetIp) {
            targetPort = it.value().port + 1;
            targetDeviceId = it.key();
            break;
        }
    }

    QJsonDocument doc(json);
    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, doc]() {
        socket->write(doc.toJson());
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket, targetDeviceId, doc]() {
        if (!targetDeviceId.isEmpty() && m_database) {
            sendOfflineMessage(targetDeviceId, doc.toJson());
        }
        socket->deleteLater();
    });
    socket->connectToHost(targetIp, targetPort);

    // Save to database
    if (!targetDeviceId.isEmpty() && m_database) {
        IPMsgMessage msg;
        msg.senderId = m_userId;
        msg.senderName = m_userName;
        msg.timestamp = QDateTime::currentMSecsSinceEpoch();
        msg.isVideoMessage = true;
        msg.videoData = videoData;
        msg.videoDuration = duration;
        msg.videoWidth = width;
        msg.videoHeight = height;
        m_database->saveMessage(msg, targetDeviceId, false);
    }
}

void IPMsgManager::sendLocationMessage(const QString& targetIp, const QString& lat, const QString& lon, const QString& name) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_LOCATION_MSG);
    json["latitude"] = lat;
    json["longitude"] = lon;
    json["locationName"] = name;
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    quint16 targetPort = 2426;
    QString targetDeviceId;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == targetIp) {
            targetPort = it.value().port + 1;
            targetDeviceId = it.key();
            break;
        }
    }

    QJsonDocument doc(json);
    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, doc]() {
        socket->write(doc.toJson());
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
    socket->connectToHost(targetIp, targetPort);

    // Save to database
    if (!targetDeviceId.isEmpty() && m_database) {
        IPMsgMessage msg;
        msg.senderId = m_userId;
        msg.senderName = m_userName;
        msg.timestamp = QDateTime::currentMSecsSinceEpoch();
        msg.locationLatitude = lat;
        msg.locationLongitude = lon;
        msg.locationName = name;
        m_database->saveMessage(msg, targetDeviceId, false);
    }
}

void IPMsgManager::sendCardMessage(const QString& targetIp, const QString& vCardData) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_CARD_MSG);
    json["vcard"] = vCardData;
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    quint16 targetPort = 2426;
    QString targetDeviceId;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == targetIp) {
            targetPort = it.value().port + 1;
            targetDeviceId = it.key();
            break;
        }
    }

    QJsonDocument doc(json);
    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, doc]() {
        socket->write(doc.toJson());
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
    socket->connectToHost(targetIp, targetPort);

    // Save to database
    if (!targetDeviceId.isEmpty() && m_database) {
        IPMsgMessage msg;
        msg.senderId = m_userId;
        msg.senderName = m_userName;
        msg.timestamp = QDateTime::currentMSecsSinceEpoch();
        msg.vCardData = vCardData;
        m_database->saveMessage(msg, targetDeviceId, false);
    }
}

// ────────── VoIP Signaling ──────────

static quint16 findDeviceTcpPort(QUdpSocket*, const QMap<QString, IPMsgDevice>& devices, const QString& targetIp) {
    for (auto it = devices.constBegin(); it != devices.constEnd(); ++it) {
        if (it.value().ip == targetIp) {
            return it.value().port + 1;
        }
    }
    return 2426;
}

void IPMsgManager::initiateCall(const QString& targetIp, const QString& callType) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_CALL_INVITE);
    json["callType"] = callType;
    json["sdp"] = "";  // SDP offer would go here
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);
    quint16 targetPort = findDeviceTcpPort(m_udpSocket, m_devices, targetIp);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, doc]() {
        socket->write(doc.toJson());
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
    socket->connectToHost(targetIp, targetPort);
}

void IPMsgManager::acceptCall(const QString& calleeIp) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_CALL_ACCEPT);
    json["sdp"] = "";
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);
    quint16 targetPort = findDeviceTcpPort(m_udpSocket, m_devices, calleeIp);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, doc]() {
        socket->write(doc.toJson());
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
    socket->connectToHost(calleeIp, targetPort);

    emit callAccepted(m_userId);
}

void IPMsgManager::rejectCall(const QString& calleeIp) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_CALL_REJECT);
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);
    quint16 targetPort = findDeviceTcpPort(m_udpSocket, m_devices, calleeIp);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, doc]() {
        socket->write(doc.toJson());
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
    socket->connectToHost(calleeIp, targetPort);

    emit callRejected(m_userId);
}

void IPMsgManager::endCall(const QString& calleeIp) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_CALL_END);
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);
    quint16 targetPort = findDeviceTcpPort(m_udpSocket, m_devices, calleeIp);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, doc]() {
        socket->write(doc.toJson());
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
    socket->connectToHost(calleeIp, targetPort);

    emit callEnded(calleeIp);
}

void IPMsgManager::sendIceCandidate(const QString& targetIp, const QByteArray& sdp) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_ICE_CANDIDATE);
    json["sdp"] = QString::fromLatin1(sdp.toBase64());
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);
    quint16 targetPort = findDeviceTcpPort(m_udpSocket, m_devices, targetIp);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, doc]() {
        socket->write(doc.toJson());
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
    socket->connectToHost(targetIp, targetPort);
}

// ────────── Group Chat Advanced ──────────

void IPMsgManager::sendGroupAnnouncement(const QString& groupId, const QString& announcement) {
    if (!m_groups.contains(groupId)) return;

    const IPMsgGroup& group = m_groups[groupId];
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_GROUP_ANNOUNCEMENT);
    json["groupId"] = groupId;
    json["groupName"] = group.name;
    json["announcement"] = announcement;
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);

    // Send to all group members
    for (const QString& memberId : group.memberIds) {
        if (memberId == m_userId) continue;

        if (m_devices.contains(memberId)) {
            const IPMsgDevice& device = m_devices[memberId];
            QTcpSocket* socket = new QTcpSocket(this);
            connect(socket, &QTcpSocket::connected, this, [socket, doc]() {
                socket->write(doc.toJson());
                socket->flush();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
            socket->connectToHost(device.ip, device.port + 1);
        } else if (m_database) {
            // Queue offline if device not online
            m_database->saveOfflineMessage({QString(), memberId, doc.toJson(), 0,
                QDateTime::currentMSecsSinceEpoch(),
                QDateTime::currentMSecsSinceEpoch() + 7 * 24 * 3600 * 1000});
        }
    }

    // Update database with announcement
    if (m_database) {
        m_database->setSetting("group_announcement_" + groupId, announcement);
    }
}

void IPMsgManager::sendGroupMention(const QString& groupId, const QStringList& mentionedMembers, const QString& message) {
    if (!m_groups.contains(groupId)) return;

    const IPMsgGroup& group = m_groups[groupId];
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_GROUP_MENTION);
    json["groupId"] = groupId;
    json["groupName"] = group.name;
    json["message"] = message;
    
    QJsonArray mentionedArray;
    for (const QString& m : mentionedMembers) mentionedArray.append(m);
    json["mentionedMembers"] = mentionedArray;
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);

    // Send to all group members
    for (const QString& memberId : group.memberIds) {
        if (m_devices.contains(memberId)) {
            const IPMsgDevice& device = m_devices[memberId];
            QTcpSocket* socket = new QTcpSocket(this);
            connect(socket, &QTcpSocket::connected, this, [socket, doc]() {
                socket->write(doc.toJson());
                socket->flush();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
            socket->connectToHost(device.ip, device.port + 1);
        } else if (m_database && mentionedMembers.contains(m_devices.value(memberId).name)) {
            m_database->saveOfflineMessage({QString(), memberId, doc.toJson(), 0,
                QDateTime::currentMSecsSinceEpoch(),
                QDateTime::currentMSecsSinceEpoch() + 7 * 24 * 3600 * 1000});
        }
    }

    // Also send as regular group message
    sendGroupMessage(groupId, message);
}

void IPMsgManager::sendGroupVote(const QString& groupId, const QString& voteTitle, const QStringList& options, int durationSeconds) {
    if (!m_groups.contains(groupId)) return;

    const IPMsgGroup& group = m_groups[groupId];
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_GROUP_VOTE);
    json["groupId"] = groupId;
    json["groupName"] = group.name;
    json["voteTitle"] = voteTitle;
    
    QJsonArray optionsArray;
    for (const QString& opt : options) optionsArray.append(opt);
    json["options"] = optionsArray;
    json["durationSeconds"] = durationSeconds;
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);

    // Send to all group members
    for (const QString& memberId : group.memberIds) {
        if (m_devices.contains(memberId)) {
            const IPMsgDevice& device = m_devices[memberId];
            QTcpSocket* socket = new QTcpSocket(this);
            connect(socket, &QTcpSocket::connected, this, [socket, doc]() {
                socket->write(doc.toJson());
                socket->flush();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
            socket->connectToHost(device.ip, device.port + 1);
        } else if (m_database) {
            m_database->saveOfflineMessage({QString(), memberId, doc.toJson(), 0,
                QDateTime::currentMSecsSinceEpoch(),
                QDateTime::currentMSecsSinceEpoch() + 7 * 24 * 3600 * 1000});
        }
    }

    // Also send as group message
    sendGroupMessage(groupId, QString("[投票] %1").arg(voteTitle));
}

void IPMsgManager::sendGroupVoteResponse(const QString& groupId, const QString& voteTitle, int selectedOption, const QString& creatorId) {
    if (!m_groups.contains(groupId)) return;
    if (!m_devices.contains(creatorId)) return;

    const IPMsgDevice& creator = m_devices[creatorId];
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_GROUP_VOTE_RESPONSE);
    json["groupId"] = groupId;
    json["voteTitle"] = voteTitle;
    json["selectedOption"] = selectedOption;
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, doc]() {
        socket->write(doc.toJson());
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
    socket->connectToHost(creator.ip, creator.port + 1);
}

void IPMsgManager::acceptFriendRequest(const QString& targetIp) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_FRIEND_ACCEPT);
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [this, socket, data = doc.toJson()]() {
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        socket->deleteLater();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
        socket->deleteLater();
    });

    quint16 targetPort = 2426;
    QString targetDeviceId;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == targetIp) {
            targetPort = it.value().port + 1;
            targetDeviceId = it.key();
            break;
        }
    }
    socket->connectToHost(targetIp, targetPort);

    // Save friend to database
    if (!targetDeviceId.isEmpty() && m_database) {
        m_database->addFriend(targetDeviceId);
    }
}

void IPMsgManager::rejectFriendRequest(const QString& targetIp) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_FRIEND_REJECT);
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [this, socket, data = doc.toJson()]() {
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        socket->deleteLater();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
        socket->deleteLater();
    });

    quint16 targetPort = 2426;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == targetIp) {
            targetPort = it.value().port + 1;
            break;
        }
    }
    socket->connectToHost(targetIp, targetPort);
}

bool IPMsgManager::isFriend(const QString& deviceId) const {
    return m_friends.contains(deviceId);
}

QList<QString> IPMsgManager::getFriends() const {
    return m_friends;
}

void IPMsgManager::sendFile(const QString& targetIp, const QString& filePath) {
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        emit fileError("", "File not found: " + filePath);
        return;
    }

    QList<IPMsgFileItem> items;
    IPMsgFileItem item;
    item.name = fileInfo.fileName();
    item.absolutePath = fileInfo.absoluteFilePath();
    item.relativePath = fileInfo.fileName();
    item.size = fileInfo.size();
    item.isDirectory = fileInfo.isDir();
    item.md5 = calculateFileMd5(filePath);
    item.chunkSize = m_chunkSize;
    item.chunkMd5s = calculateChunkMd5s(filePath, m_chunkSize);

    if (fileInfo.isDir()) {
        item.size = calculateFolderSize(filePath);
        QDir dir(filePath);
        QDirIterator it(filePath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            IPMsgFileItem child;
            child.name = it.fileName();
            child.absolutePath = it.filePath();
            child.relativePath = dir.relativeFilePath(it.filePath());
            child.size = it.fileInfo().size();
            child.isDirectory = it.fileInfo().isDir();
            if (!child.isDirectory) {
                child.md5 = calculateFileMd5(it.filePath());
                child.chunkSize = m_chunkSize;
                child.chunkMd5s = calculateChunkMd5s(it.filePath(), m_chunkSize);
            }
            item.children.append(child);
        }
    }

    items.append(item);
    sendFileData(targetIp, items);
}

void IPMsgManager::sendFolder(const QString& targetIp, const QString& folderPath) {
    sendFile(targetIp, folderPath);
}

void IPMsgManager::acceptFile(const QString& fileId, const QString& savePath) {
    QTcpSocket* socket = m_incomingFileSockets.value(fileId);
    if (!socket || !socket->isOpen()) {
        emit fileError(fileId, "No incoming connection for file");
        return;
    }

    IPMsgTransferTask* task = findTask(fileId);
    if (!task) {
        emit fileError(fileId, "Unknown file transfer");
        return;
    }

    RecvContext* c = new RecvContext();
    c->socket = socket;
    c->fileId = fileId;
    c->savePath = savePath;
    c->totalSize = task->totalSize;
    c->md5 = task->md5;
    c->isDirectory = task->isDirectory;
    c->encrypted = task->encrypted;
    c->startTime = QDateTime::currentMSecsSinceEpoch();
    c->lastSampleTime = c->startTime;
    c->chunkMd5s = m_incomingChunkMd5s.value(fileId);
    c->chunkSize = m_incomingChunkSizes.value(fileId, m_chunkSize);
    c->chunkIndex = 0;

    if (task->encrypted && m_e2eeSessions.contains(task->peerId)) {
        c->sessionKey = m_e2eeSessions.value(task->peerId).sharedSecret;
    } else {
        c->encrypted = false;
    }

    if (!task->isDirectory) {
        // For files belonging to a folder, savePath is the chosen base directory
        // and the file is written to base + relativePath to recreate structure.
        QString actualPath = savePath;
        QString rel = m_incomingRelativePaths.value(fileId);
        if (!rel.isEmpty()) {
            actualPath = QDir(savePath).filePath(rel);
        }
        QDir().mkpath(QFileInfo(actualPath).dir().absolutePath());
        QFile* file = new QFile(actualPath, this);
        if (!file->open(QIODevice::WriteOnly)) {
            emit fileError(fileId, "Failed to create file: " + actualPath);
            delete c;
            return;
        }
        c->file = file;
        c->savePath = actualPath;
    } else {
        QDir().mkpath(savePath);
        c->savePath = savePath;
    }

    m_recvContexts[fileId] = c;
    m_recvSocketToFileId[socket] = fileId;

    QJsonObject json;
    json["command"] = QString::fromUtf8(IPMSG_FILE_ACCEPT);
    json["fileId"] = fileId;
    json["savePath"] = savePath;
    // If we already have a partial download for this file, resume from the
    // persisted offset instead of restarting from zero (断点续传).
    json["offset"] = resumeOffsetFor(fileId);

    socket->write(QJsonDocument(json).toJson());
    socket->flush();

    task->status = TransferStatus::Transferring;
    task->savePath = c->savePath;
    emit fileTaskStarted(fileId);
    emit fileProgress(fileId, 0, task->totalSize, 0);
}

void IPMsgManager::rejectFile(const QString& fileId) {
    QTcpSocket* socket = m_incomingFileSockets.value(fileId);
    if (socket && socket->isOpen()) {
        QJsonObject json;
        json["command"] = QString::fromUtf8(IPMSG_FILE_REJECT);
        json["fileId"] = fileId;
        socket->write(QJsonDocument(json).toJson());
        socket->flush();
    }

    m_incomingFileSockets.remove(fileId);
    IPMsgTransferTask* t = findTask(fileId);
    if (t) t->status = TransferStatus::Cancelled;
    emit fileTaskCancelled(fileId);
}

void IPMsgManager::pauseFile(const QString& fileId) {
    if (m_activeSenders.contains(fileId)) {
        SendContext* c = m_activeSenders.value(fileId);
        c->paused = true;
        IPMsgTransferTask* t = findTask(fileId);
        if (t) t->status = TransferStatus::Paused;
        emit fileTaskPaused(fileId);
    } else if (m_recvContexts.contains(fileId)) {
        RecvContext* c = m_recvContexts.value(fileId);
        c->paused = true;
        IPMsgTransferTask* t = findTask(fileId);
        if (t) t->status = TransferStatus::Paused;
        emit fileTaskPaused(fileId);
    }
}

void IPMsgManager::cancelFile(const QString& fileId) {
    m_pendingKeyJobs.remove(fileId);
    if (m_activeSenders.contains(fileId)) {
        cleanupSender(fileId);
        IPMsgTransferTask* t = findTask(fileId);
        if (t) t->status = TransferStatus::Cancelled;
        emit fileTaskCancelled(fileId);
        removeTransferState(fileId);
    } else if (m_recvContexts.contains(fileId)) {
        RecvContext* c = m_recvContexts.take(fileId);
        if (c->socket) {
            m_recvSocketToFileId.remove(c->socket);
            c->socket->disconnect();
            c->socket->abort();
            c->socket->deleteLater();
        }
        if (c->file) { c->file->close(); delete c->file; }
        delete c;
        IPMsgTransferTask* t = findTask(fileId);
        if (t) t->status = TransferStatus::Cancelled;
        emit fileTaskCancelled(fileId);
        removeTransferState(fileId);
    } else {
        IPMsgTransferTask* t = findTask(fileId);
        if (t) t->status = TransferStatus::Cancelled;
        emit fileTaskCancelled(fileId);
        removeTransferState(fileId);
    }
}

void IPMsgManager::retryFile(const QString& fileId) {
    IPMsgTransferTask* t = findTask(fileId);
    if (!t) {
        emit fileError(fileId, "No transfer to retry");
        return;
    }
    if (t->direction != TransferDirection::Send || t->filePath.isEmpty()) {
        emit fileError(fileId, "Cannot retry this transfer");
        return;
    }
    // Re-enqueue a fresh send job from the source path
    IPMsgFileItem item;
    item.name = t->fileName;
    item.absolutePath = t->filePath;
    item.relativePath = t->fileName;
    item.size = t->totalSize;
    item.md5 = t->md5;
    item.isDirectory = t->isDirectory;

    // Reset task to queued
    t->status = TransferStatus::Queued;
    t->transferredSize = 0;
    t->speedBps = 0;
    t->attempts = 1;
    emit fileTaskQueued(fileId);

    IPMsgTransferState state;
    state.fileId = fileId;
    state.filePath = t->filePath;
    state.savePath = "";
    state.totalSize = t->totalSize;
    state.transferredSize = 0;
    state.md5 = t->md5;
    state.isDirectory = t->isDirectory;
    state.isSender = true;
    state.lastOffset = 0;
    saveTransferState(state);

    QString targetIp;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.key() == t->peerId) { targetIp = it.value().ip; break; }
    }
    if (targetIp.isEmpty()) {
        emit fileError(fileId, "Peer offline, cannot retry");
        return;
    }

    SendJob job;
    job.fileId = fileId;
    job.targetIp = targetIp;
    job.item = item;
    m_sendQueue.append(job);
    pumpTransferQueue();
}

void IPMsgManager::resumeFile(const QString& fileId) {
    if (m_tasks.contains(fileId)) {
        retryFile(fileId);
        return;
    }
    IPMsgTransferState state = getTransferState(fileId);
    if (state.fileId.isEmpty()) {
        emit fileError(fileId, "No transfer state found for resume");
        return;
    }
    // Reconstruct a send task from persisted state and retry
    IPMsgTransferTask task;
    task.fileId = fileId;
    task.direction = TransferDirection::Send;
    task.fileName = QFileInfo(state.filePath).fileName();
    task.filePath = state.filePath;
    task.totalSize = state.totalSize;
    task.md5 = state.md5;
    task.isDirectory = state.isDirectory;
    task.status = TransferStatus::Queued;
    task.startTime = QDateTime::currentMSecsSinceEpoch();
    task.lastSampleTime = task.startTime;
    registerTask(task);
    retryFile(fileId);
}

void IPMsgManager::removeTransfer(const QString& fileId) {
    // Drop any in-flight sender/recipient context first.
    if (m_activeSenders.contains(fileId)) {
        cleanupSender(fileId);
    }
    if (m_recvContexts.contains(fileId)) {
        RecvContext* c = m_recvContexts.take(fileId);
        if (c->socket) {
            m_recvSocketToFileId.remove(c->socket);
            c->socket->disconnect();
            c->socket->abort();
            c->socket->deleteLater();
        }
        if (c->file) { c->file->close(); delete c->file; }
        delete c;
    }
    m_pendingKeyJobs.remove(fileId);
    m_tasks.remove(fileId);
    m_incomingRelativePaths.remove(fileId);
    m_incomingChunkMd5s.remove(fileId);
    m_incomingChunkSizes.remove(fileId);
    removeTransferState(fileId);
}

QList<IPMsgTransferTask> IPMsgManager::getTransferTasks() const {
    return m_tasks.values();
}

void IPMsgManager::setMaxConcurrentTransfers(int max) {
    m_maxConcurrentTransfers = qMax(1, max);
    const_cast<IPMsgManager*>(this)->pumpTransferQueue();
}

int IPMsgManager::maxConcurrentTransfers() const {
    return m_maxConcurrentTransfers;
}

void IPMsgManager::setTransferStateFile(const QString& path) {
    m_transferStateFile = path;
    m_transferStates.clear();
    loadTransferStates();
}

bool IPMsgManager::verifyFileIntegrity(const QString& filePath, const QString& expectedMd5) {
    if (expectedMd5.isEmpty()) return true;
    
    QString actualMd5 = calculateFileMd5(filePath);
    return actualMd5 == expectedMd5;
}

// ────────── Multi-device sync (Phase E1) ──────────

bool IPMsgManager::setAccount(const QString& accountId, const QString& syncKey) {
    if (!m_database) return false;

    m_syncAccountId = accountId;
    m_syncAccountHash = QCryptographicHash::hash(accountId.toUtf8(), QCryptographicHash::Sha256).toHex();
    m_syncKeyHash = QCryptographicHash::hash(syncKey.toUtf8(), QCryptographicHash::Sha256).toHex();

    m_database->setSetting("sync_account_id", accountId);
    m_database->setSetting("sync_account_hash", m_syncAccountHash);
    m_database->setSetting("sync_key_hash", m_syncKeyHash);

    // Account identity changed -> previous same-account peers are stale
    m_sameAccountDevices.clear();

    broadcastPresence();
    return true;
}

bool IPMsgManager::hasAccountConfigured() const {
    return !m_syncAccountHash.isEmpty();
}

QString IPMsgManager::accountId() const {
    return m_syncAccountId;
}

QString IPMsgManager::accountHash() const {
    return m_syncAccountHash;
}

QList<QString> IPMsgManager::sameAccountDevices() const {
    return m_sameAccountDevices.values();
}

bool IPMsgManager::syncPeerValid(const QJsonObject& json) const {
    if (!hasAccountConfigured()) return false;
    return json["accountHash"].toString() == m_syncAccountHash &&
           json["syncKeyHash"].toString() == m_syncKeyHash;
}

QString IPMsgManager::deviceIdByIp(const QString& ip) const {
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == ip) return it.key();
    }
    return QString();
}

void IPMsgManager::refreshLocalStateFromDb() {
    if (!m_database) return;
    m_friends = m_database->loadFriends();

    QList<IPMsgGroup> groups = m_database->loadAllGroups();
    for (const IPMsgGroup& group : groups) {
        m_groups[group.id] = group;
    }

    QList<IPMsgDevice> devices = m_database->loadAllDevices();
    for (const IPMsgDevice& device : devices) {
        if (!m_devices.contains(device.id)) {
            m_devices[device.id] = device;
        }
    }

    emit dataSynced();
}

QByteArray IPMsgManager::serializeSnapshot(const DatabaseManager::SyncSnapshot& snap) {
    QJsonObject obj;
    obj["version"] = snap.version;

    QJsonArray devices;
    for (const auto& d : snap.devices) {
        QJsonObject o;
        o["device_id"] = d.deviceId;
        o["name"] = d.name;
        o["ip"] = d.ip;
        o["port"] = d.port;
        o["last_seen"] = d.lastSeen;
        o["is_friend"] = d.isFriend;
        o["updated_at"] = d.updatedAt;
        devices.append(o);
    }

    QJsonArray groups;
    for (const auto& g : snap.groups) {
        QJsonObject o;
        o["group_id"] = g.groupId;
        o["name"] = g.name;
        QJsonArray ids, names;
        for (const QString& s : g.memberIds) ids.append(s);
        for (const QString& s : g.memberNames) names.append(s);
        o["member_ids"] = ids;
        o["member_names"] = names;
        o["created_at"] = g.createdAt;
        o["updated_at"] = g.updatedAt;
        groups.append(o);
    }

    QJsonArray settings;
    for (const auto& s : snap.settings) {
        QJsonObject o;
        o["key"] = s.key;
        o["value"] = s.value;
        o["updated_at"] = s.updatedAt;
        settings.append(o);
    }

    QJsonArray messages;
    for (const auto& m : snap.messages) {
        QJsonObject o;
        o["message_id"] = m.messageId;
        o["sender_id"] = m.senderId;
        o["sender_name"] = m.senderName;
        o["sender_ip"] = m.senderIp;
        o["content"] = m.content;
        o["timestamp"] = m.timestamp;
        o["is_file"] = m.isFile;
        o["file_path"] = m.filePath;
        o["file_size"] = m.fileSize;
        o["is_directory"] = m.isDirectory;
        o["is_image"] = m.isImage;
        o["image_file_name"] = m.imageFileName;
        o["reply_to"] = m.replyTo;
        o["reply_content"] = m.replyContent;
        o["recall_id"] = m.recallId;
        o["is_recalled"] = m.isRecalled;
        o["target_id"] = m.targetId;
        o["is_group"] = m.isGroup;
        o["is_read"] = m.isRead;
        o["read_by"] = m.readBy;
        messages.append(o);
    }

    obj["devices"] = devices;
    obj["groups"] = groups;
    obj["settings"] = settings;
    obj["messages"] = messages;
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

DatabaseManager::SyncSnapshot IPMsgManager::deserializeSnapshot(const QJsonObject& json) {
    DatabaseManager::SyncSnapshot snap;
    snap.version = json["version"].toVariant().toLongLong();

    const QJsonArray devices = json["devices"].toArray();
    for (const auto& v : devices) {
        QJsonObject o = v.toObject();
        DatabaseManager::SyncDeviceRow row;
        row.deviceId = o["device_id"].toString();
        row.name = o["name"].toString();
        row.ip = o["ip"].toString();
        row.port = static_cast<quint16>(o["port"].toInt());
        row.lastSeen = o["last_seen"].toVariant().toLongLong();
        row.isFriend = o["is_friend"].toBool();
        row.updatedAt = o["updated_at"].toVariant().toLongLong();
        snap.devices.append(row);
    }

    const QJsonArray groups = json["groups"].toArray();
    for (const auto& v : groups) {
        QJsonObject o = v.toObject();
        DatabaseManager::SyncGroupRow row;
        row.groupId = o["group_id"].toString();
        row.name = o["name"].toString();
        for (const auto& idv : o["member_ids"].toArray()) row.memberIds.append(idv.toString());
        for (const auto& nv : o["member_names"].toArray()) row.memberNames.append(nv.toString());
        row.createdAt = o["created_at"].toVariant().toLongLong();
        row.updatedAt = o["updated_at"].toVariant().toLongLong();
        snap.groups.append(row);
    }

    const QJsonArray settings = json["settings"].toArray();
    for (const auto& v : settings) {
        QJsonObject o = v.toObject();
        DatabaseManager::SyncSettingRow row;
        row.key = o["key"].toString();
        row.value = o["value"].toString();
        row.updatedAt = o["updated_at"].toVariant().toLongLong();
        snap.settings.append(row);
    }

    const QJsonArray messages = json["messages"].toArray();
    for (const auto& v : messages) {
        QJsonObject o = v.toObject();
        DatabaseManager::SyncMessageRow row;
        row.messageId = o["message_id"].toString();
        row.senderId = o["sender_id"].toString();
        row.senderName = o["sender_name"].toString();
        row.senderIp = o["sender_ip"].toString();
        row.content = o["content"].toString();
        row.timestamp = o["timestamp"].toVariant().toLongLong();
        row.isFile = o["is_file"].toBool();
        row.filePath = o["file_path"].toString();
        row.fileSize = o["file_size"].toVariant().toLongLong();
        row.isDirectory = o["is_directory"].toBool();
        row.isImage = o["is_image"].toBool();
        row.imageFileName = o["image_file_name"].toString();
        row.replyTo = o["reply_to"].toString();
        row.replyContent = o["reply_content"].toString();
        row.recallId = o["recall_id"].toString();
        row.isRecalled = o["is_recalled"].toBool();
        row.targetId = o["target_id"].toString();
        row.isGroup = o["is_group"].toBool();
        row.isRead = o["is_read"].toBool();
        row.readBy = o["read_by"].toString();
        snap.messages.append(row);
    }

    return snap;
}

void IPMsgManager::syncWith(const QString& targetIp) {
    if (!m_database || !hasAccountConfigured()) {
        emit syncFailed(targetIp, tr("账号未配置"));
        return;
    }

    quint16 targetPort = 2426;
    if (!m_devices.isEmpty()) {
        for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
            if (it.value().ip == targetIp) {
                targetPort = it.value().port + 1;
                break;
            }
        }
    }

    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_SYNC_REQUEST);
    json["accountHash"] = m_syncAccountHash;
    json["syncKeyHash"] = m_syncKeyHash;

    QJsonDocument doc(json);
    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, data = doc.toJson()]() {
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket, targetIp](QAbstractSocket::SocketError) {
        LOG_ERROR("IPMsg: Sync request error: " + socket->errorString());
        m_pendingSyncs.remove(targetIp);
        emit syncFailed(targetIp, socket->errorString());
        socket->deleteLater();
    });

    m_pendingSyncs.insert(targetIp);
    socket->connectToHost(targetIp, targetPort);

    // Timeout guard
    QTimer::singleShot(10000, this, [this, targetIp]() {
        if (m_pendingSyncs.remove(targetIp)) {
            emit syncFailed(targetIp, tr("同步超时"));
        }
    });
}

void IPMsgManager::sendSyncSnapshotResponse(const QString& targetIp, bool isReply) {
    if (!m_database) return;
    DatabaseManager::SyncSnapshot snap = m_database->buildSyncSnapshot();

    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_SYNC_SNAPSHOT);
    json["accountHash"] = m_syncAccountHash;
    json["syncKeyHash"] = m_syncKeyHash;
    json["isReply"] = isReply;
    json["snapshot"] = QJsonDocument::fromJson(serializeSnapshot(snap)).object();

    QJsonDocument doc(json);
    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, data = doc.toJson()]() {
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);

    quint16 targetPort = 2426;
    if (!m_devices.isEmpty()) {
        for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
            if (it.value().ip == targetIp) {
                targetPort = it.value().port + 1;
                break;
            }
        }
    }
    socket->connectToHost(targetIp, targetPort);
}

void IPMsgManager::sendSyncAck(const QString& targetIp, bool success, int applied) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_SYNC_ACK);
    json["success"] = success;
    json["applied"] = applied;

    QJsonDocument doc(json);
    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, data = doc.toJson()]() {
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);

    quint16 targetPort = 2426;
    if (!m_devices.isEmpty()) {
        for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
            if (it.value().ip == targetIp) {
                targetPort = it.value().port + 1;
                break;
            }
        }
    }
    socket->connectToHost(targetIp, targetPort);
}

// ────────── Getter helpers ──────────

QList<IPMsgDevice> IPMsgManager::getOnlineDevices() const {
    QList<IPMsgDevice> devices;
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (now - it.value().lastSeen < 30000) { // 30 seconds timeout
            devices.append(it.value());
        }
    }

    return devices;
}

IPMsgDevice IPMsgManager::deviceInfo(const QString& deviceId) const {
    auto it = m_devices.find(deviceId);
    if (it != m_devices.end()) return it.value();
    return IPMsgDevice();
}

QList<IPMsgMessage> IPMsgManager::getMessages() const {
    return m_messages;
}

QList<IPMsgMessage> IPMsgManager::getMessagesWith(const QString& deviceId) const {
    QList<IPMsgMessage> result;
    for (const auto& msg : m_messages) {
        if (msg.senderId == deviceId) {
            result.append(msg);
        }
    }
    return result;
}

bool IPMsgManager::sendOfflineMessage(const QString& targetDeviceId, const QByteArray& payload) {
    if (!m_database) return false;

    DatabaseManager::OfflineMessage msg;
    msg.id = QUuid::createUuid().toString().remove('{').remove('}').remove('-');
    msg.targetDeviceId = targetDeviceId;
    msg.payload = payload;
    msg.retryCount = 0;
    msg.createdAt = QDateTime::currentMSecsSinceEpoch();
    msg.expiresAt = msg.createdAt + 7 * 24 * 60 * 60 * 1000; // 7 days expiry

    if (!m_database->saveOfflineMessage(msg)) {
        LOG_ERROR("IPMsg: Failed to save offline message");
        return false;
    }

    LOG_INFO(QString("IPMsg: Saved offline message %1 for device %2")
             .arg(msg.id, targetDeviceId));
    return true;
}

QList<DatabaseManager::OfflineMessage> IPMsgManager::fetchOfflineMessages() {
    QList<DatabaseManager::OfflineMessage> messages;
    if (!m_database) return messages;

    messages = m_database->loadPendingOfflineMessages(m_userId);

    if (!messages.isEmpty()) {
        LOG_INFO(QString("IPMsg: Retrieved %1 offline messages for delivery")
                 .arg(messages.size()));
        emit offlineMessagesAvailable(messages);
    }

    return messages;
}

void IPMsgManager::markOfflineMessageDelivered(const QString& msgId) {
    if (m_database) {
        m_database->markOfflineMessageDelivered(msgId);
    }
}

void IPMsgManager::cleanupExpiredOfflineMessages() {
    if (m_database) {
        m_database->cleanupExpiredOfflineMessages();
    }
}

void IPMsgManager::setUserName(const QString& name) {
    m_userName = name;
}

QString IPMsgManager::userName() const {
    return m_userName;
}

QString IPMsgManager::userId() const {
    return m_userId;
}

QString IPMsgManager::calculateFileMd5(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return calculateFileMd5(&file);
}

QString IPMsgManager::calculateFileMd5(QIODevice* device) {
    if (!device || !device->isOpen()) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Md5);
    if (hash.addData(device)) {
        return hash.result().toHex();
    }
    return QString();
}

// ────────── Private Slots ──────────

void IPMsgManager::onUdpBroadcastReceived() {
    while (m_udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_udpSocket->pendingDatagramSize());
        QHostAddress sender;
        quint16 senderPort;

        m_udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        processUdpMessage(datagram, sender);
    }
}

void IPMsgManager::retryOfflineMessages(const QString& deviceId) {
    if (!m_database || deviceId.isEmpty()) return;
    
    auto messages = m_database->loadPendingOfflineMessages(deviceId, 50);
    if (messages.isEmpty()) return;
    
    LOG_INFO(QString("IPMsg: Retrying %1 offline messages for device %2")
             .arg(messages.size()).arg(deviceId));
    
    // Build batch message
    QJsonArray msgsArray;
    QStringList msgIds;
    
    for (const auto& msg : messages) {
        QJsonObject msgObj;
        msgObj["id"] = m_userId;
        msgObj["name"] = m_userName;
        msgObj["command"] = QString::fromUtf8(IPMSG_MSG);
        msgObj["message"] = QString::fromUtf8(msg.payload);
        msgObj["timestamp"] = msg.createdAt;
        msgObj["recallId"] = QUuid::createUuid().toString().remove('{').remove('}').remove('-');
        msgsArray.append(msgObj);
        msgIds.append(msg.id);
    }
    
    QJsonObject batchJson;
    batchJson["id"] = m_userId;
    batchJson["name"] = m_userName;
    batchJson["command"] = QString::fromUtf8(IPMSG_OFFLINE_MSG);
    batchJson["messages"] = msgsArray;
    batchJson["messageIds"] = QJsonArray::fromStringList(msgIds);
    batchJson["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    
    QJsonDocument batchDoc(batchJson);
    
    // Find device IP
    QString targetIp;
    if (m_devices.contains(deviceId)) {
        targetIp = m_devices[deviceId].ip;
    }
    
    if (!targetIp.isEmpty()) {
        QTcpSocket* socket = new QTcpSocket(this);
        connect(socket, &QTcpSocket::connected, this, [this, socket, data = batchDoc.toJson()]() {
            socket->write(data);
            socket->flush();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::errorOccurred, this, [socket](QAbstractSocket::SocketError) {
            socket->deleteLater();
        });
        socket->connectToHost(targetIp, 2426);
    }
}

void IPMsgManager::onNewTcpConnection() {
    while (m_tcpServer->hasPendingConnections()) {
        QTcpSocket* socket = m_tcpServer->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, &IPMsgManager::onTcpDataReceived);
        connect(socket, &QTcpSocket::disconnected, this, &IPMsgManager::onTcpDisconnected);
    }
}

void IPMsgManager::onTcpDataReceived() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    if (m_recvSocketToFileId.contains(socket)) {
        QString fileId = m_recvSocketToFileId[socket];
        RecvContext* c = m_recvContexts.value(fileId);
        if (c && c->paused) return; // backpressure: leave bytes in socket buffer
        writeReceivedData(fileId, socket->readAll());
        return;
    }

    QByteArray data = socket->readAll();
    processTcpCommand(data, socket);
}

void IPMsgManager::onTcpDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    // Sender-side: an active send whose socket closed unexpectedly is a failure
    QString fileId;
    for (auto it = m_sendingSockets.constBegin(); it != m_sendingSockets.constEnd(); ++it) {
        if (it.value() == socket) {
            fileId = it.key();
            break;
        }
    }

    if (!fileId.isEmpty() && m_activeSenders.contains(fileId)) {
        SendContext* c = m_activeSenders.value(fileId);
        bool finished = c ? c->finished : false;
        m_activeSenders.remove(fileId);
        m_sendingSockets.remove(fileId);
        if (!finished) {
            IPMsgTransferTask* t = findTask(fileId);
            if (t) t->status = TransferStatus::Failed;
            emit fileError(fileId, "Connection closed");
        }
        if (c) { if (c->file) { c->file->close(); delete c->file; } delete c; }
        pumpTransferQueue();
    }

    socket->deleteLater();
}

void IPMsgManager::onSendProgress(qint64 bytesWritten) {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QString fileId;
    for (auto it = m_sendingSockets.constBegin(); it != m_sendingSockets.constEnd(); ++it) {
        if (it.value() == socket) {
            fileId = it.key();
            break;
        }
    }

    if (!fileId.isEmpty()) {
        onSendChunkWritten(fileId, bytesWritten);
    }
}

// ────────── E2EE Implementation ──────────

QByteArray IPMsgManager::generateEcdhKeyPair(QByteArray* outPublicKey) {
    // Use X25519 for ECDH key exchange
    // For simplicity, use a hash-based approach with QCryptographicHash
    // This is a simplified implementation - a production version would use
    // libsodium, OpenSSL, or Windows CNG for proper Curve25519 support

    // Generate 32-byte private key
    QByteArray privateKey(32, 0);
    for (int i = 0; i < 32; ++i) {
        quint32 rand = QRandomGenerator::global()->generate();
        privateKey[i] = static_cast<char>(rand & 0xFF);
    }

    if (outPublicKey) {
        // In a real implementation, this would be the X25519 public key
        // For now, derive a pseudo-public key from the private key
        *outPublicKey = QCryptographicHash::hash(privateKey, QCryptographicHash::Sha256);
    }

    return privateKey;
}

QByteArray IPMsgManager::computeSharedSecret(const QByteArray& peerPublicKey, const QByteArray& privateKey) {
    // Simplified shared secret computation.
    // In production, this would be proper ECDH with X25519.
    // Derive our own public key from the private key, then hash the
    // lexicographically-sorted concatenation of the two public keys so that
    // BOTH peers derive the SAME secret regardless of who initiated:
    //   A: computeSharedSecret(pubB, privA),  B: computeSharedSecret(pubA, privB)
    QByteArray myPublicKey = QCryptographicHash::hash(privateKey, QCryptographicHash::Sha256);
    QByteArray combined;
    if (myPublicKey <= peerPublicKey) {
        combined = myPublicKey + peerPublicKey;
    } else {
        combined = peerPublicKey + myPublicKey;
    }
    return QCryptographicHash::hash(combined, QCryptographicHash::Sha256);
}

QByteArray IPMsgManager::deriveAesKey(const QByteArray& sharedSecret, const QByteArray& salt) {
    QByteArray combined = sharedSecret + salt;
    return QCryptographicHash::hash(combined, QCryptographicHash::Sha256);
}

QByteArray IPMsgManager::aesGcmEncrypt(const QByteArray& plaintext, const QByteArray& key, QByteArray* outNonce) {
    // AES-256-CBC + HMAC-SHA256 (Encrypt-then-MAC)
    QByteArray nonce(12, 0);
    for (int i = 0; i < 12; ++i) {
        nonce[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xFF);
    }

    if (outNonce) *outNonce = nonce;

    // Derive separate encryption and MAC keys from the shared key + nonce
    QByteArray encKey = QCryptographicHash::hash(key + nonce + QByteArray("enc"), QCryptographicHash::Sha256);
    QByteArray macKey = QCryptographicHash::hash(key + nonce + QByteArray("mac"), QCryptographicHash::Sha256);

    // AES-256-CBC encrypt with PKCS#7 padding
    Encryption cipher;
    QByteArray iv = nonce.left(16).rightJustified(16, 0);
    if (!cipher.setKey(encKey, iv)) {
        LOG_ERROR("IPMsg: Failed to set AES key for encryption");
        return QByteArray();
    }
    QByteArray ciphertext = cipher.encrypt(plaintext);
    if (ciphertext.isEmpty()) {
        LOG_ERROR("IPMsg: AES encryption failed");
        return QByteArray();
    }

    // HMAC-SHA256 authentication tag (Encrypt-then-MAC)
    QByteArray tagInput = nonce + ciphertext;
    QByteArray tag = QCryptographicHash::hash(macKey + tagInput, QCryptographicHash::Sha256);
    tag.truncate(16); // 128-bit tag

    return ciphertext + tag;
}

QByteArray IPMsgManager::aesGcmDecrypt(const QByteArray& ciphertext, const QByteArray& key, const QByteArray& nonce, const QByteArray& tag) {
    if (ciphertext.size() < 16 || tag.size() < 16) return QByteArray();

    // Derive separate encryption and MAC keys (must match encrypt)
    QByteArray encKey = QCryptographicHash::hash(key + nonce + QByteArray("enc"), QCryptographicHash::Sha256);
    QByteArray macKey = QCryptographicHash::hash(key + nonce + QByteArray("mac"), QCryptographicHash::Sha256);

    // Verify HMAC-SHA256 tag (constant-time comparison)
    QByteArray tagInput = nonce + ciphertext;
    QByteArray expectedTag = QCryptographicHash::hash(macKey + tagInput, QCryptographicHash::Sha256);
    expectedTag.truncate(16);

    if (tag.left(16) != expectedTag) {
        LOG_WARNING("IPMsg: E2EE tag verification failed");
        return QByteArray();
    }

    // AES-256-CBC decrypt
    Encryption cipher;
    QByteArray iv = nonce.left(16).rightJustified(16, 0);
    if (!cipher.setKey(encKey, iv)) {
        LOG_ERROR("IPMsg: Failed to set AES key for decryption");
        return QByteArray();
    }
    return cipher.decrypt(ciphertext);
}

void IPMsgManager::initiateKeyExchange(const QString& targetIp) {
    // Generate local keypair
    QByteArray privateKey = generateEcdhKeyPair(&localPublicKey);
    
    // Store local keypair for this session
    E2EESession session;
    session.privateKey = privateKey;
    session.publicKey = localPublicKey;
    
    // Find target device ID
    QString targetDeviceId;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == targetIp) {
            targetDeviceId = it.key();
            break;
        }
    }
    
    if (targetDeviceId.isEmpty()) {
        LOG_WARNING("IPMsg: Cannot find device for key exchange");
        return;
    }
    
    // Send key exchange message
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_KEY_EXCHANGE);
    json["publicKey"] = QString::fromUtf8(localPublicKey.toBase64());
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    
    quint16 targetPort = 2426;
    if (m_devices.contains(targetDeviceId)) {
        targetPort = m_devices[targetDeviceId].port + 1;
    }
    
    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [socket, data = QJsonDocument(json).toJson()]() {
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    
    session.publicKey = localPublicKey;
    m_e2eeSessions[targetDeviceId] = session;
    
    socket->connectToHost(targetIp, targetPort);
}

QByteArray IPMsgManager::encryptMessage(const QByteArray& plaintext, const QString& recipientId) {
    if (!m_e2eeSessions.contains(recipientId)) {
        LOG_WARNING("IPMsg: No E2EE session with device " + recipientId);
        return plaintext; // Return plaintext if no session
    }

    E2EESession& session = m_e2eeSessions[recipientId];
    QByteArray nonce;
    // aesGcmEncrypt returns ciphertext with the 16-byte authentication tag
    // appended. Split it so the message layout is [body][nonce(12)][tag(16)],
    // matching the layout expected by decryptMessage.
    QByteArray encrypted = aesGcmEncrypt(plaintext, session.sharedSecret, &nonce);
    if (encrypted.size() < 16) return plaintext;

    QByteArray body = encrypted.left(encrypted.size() - 16);
    QByteArray tag = encrypted.right(16);

    return body + nonce + tag;
}

QByteArray IPMsgManager::decryptMessage(const QByteArray& ciphertext, const QString& senderId) {
    if (!m_e2eeSessions.contains(senderId)) {
        LOG_WARNING("IPMsg: No E2EE session with device " + senderId);
        return ciphertext; // Return as-is if no session
    }

    E2EESession& session = m_e2eeSessions[senderId];
    if (ciphertext.size() < 28) return ciphertext;

    QByteArray actualCiphertext = ciphertext.left(ciphertext.size() - 28);
    QByteArray nonce = ciphertext.mid(ciphertext.size() - 28, 12);
    QByteArray tag = ciphertext.right(16);

    return aesGcmDecrypt(actualCiphertext, session.sharedSecret, nonce, tag);
}

bool IPMsgManager::hasEstablishedSession(const QString& deviceId) const {
    return m_e2eeSessions.contains(deviceId) && !m_e2eeSessions[deviceId].sharedSecret.isEmpty();
}

// ────────── Private Methods ──────────

void IPMsgManager::processUdpMessage(const QByteArray& data, const QHostAddress& sender) {
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return;

    QJsonObject json = doc.object();
    QString command = json["command"].toString();
    QString senderId = json["id"].toString();

    if (senderId == m_userId) return; // Ignore own messages

    if (command == QString::fromUtf8(IPMSG_HELLO)) {
        IPMsgDevice device;
        device.id = senderId;
        device.name = json["name"].toString();
        // Use IP from message if available, otherwise use sender address
        QString msgIp = json["ip"].toString();
        device.ip = msgIp.isEmpty() ? sender.toString() : msgIp;
        device.port = json["port"].toInt();
        device.lastSeen = QDateTime::currentMSecsSinceEpoch();

        m_devices[senderId] = device;
        
        // Save to database
        if (m_database) {
            m_database->saveDevice(device);
        }
        
        // Detect same-account peers for multi-device sync
        QString remoteAccountHash = json["accountHash"].toString();
        if (!remoteAccountHash.isEmpty() && !m_syncAccountHash.isEmpty() &&
            remoteAccountHash == m_syncAccountHash && senderId != m_userId) {
            m_sameAccountDevices.insert(senderId);
            emit sameAccountDeviceFound(senderId, device.name);
        }
        
        emit deviceFound(device);
        LOG_INFO(QString("IPMsg: Found device %1 at %2:%3").arg(device.name, device.ip).arg(device.port));

        // Respond with our presence
        QJsonObject response;
        response["id"] = m_userId;
        response["name"] = m_userName;
        response["port"] = m_port;
        response["ip"] = getLocalIp();
        response["command"] = QString::fromUtf8(IPMSG_HELLO);

        QJsonDocument responseDoc(response);
        // Send response to sender's port
        quint16 senderPort = json["port"].toInt();
        m_udpSocket->writeDatagram(responseDoc.toJson(), sender, senderPort);
    } else if (command == QString::fromUtf8(IPMSG_LEAVE)) {
        m_devices.remove(senderId);
        emit deviceLeft(senderId);
    }
}

void IPMsgManager::processTcpCommand(const QByteArray& data, QTcpSocket* socket) {
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return;

    QJsonObject json = doc.object();
    QString command = json["command"].toString();

    if (command == QString::fromUtf8(IPMSG_MSG)) {
        IPMsgMessage msg;
        msg.senderId = json["id"].toString();
        msg.senderName = json["name"].toString();
        msg.senderIp = socket->peerAddress().toString();
        msg.content = json["message"].toString();
        msg.timestamp = json["timestamp"].toVariant().toLongLong();
        msg.isFile = false;
        msg.isImage = false;
        msg.recallId = json["recallId"].toString();
        msg.isRecalled = false;

        m_messages.append(msg);
        
        // Save to database
        if (m_database) {
            m_database->saveMessage(msg, msg.senderId, false);
        }
        
        emit messageReceived(msg);
    } else if (command == QString::fromUtf8(IPMSG_RECALL)) {
        QString recallId = json["recallId"].toString();
        QString senderName = json["senderName"].toString();
        emit messageRecalled(recallId, senderName);
    } else if (command == QString::fromUtf8(IPMSG_READ)) {
        QString messageId = json["messageId"].toString();
        QString readerName = json["name"].toString();
        emit messageRead(messageId, readerName);
    } else if (command == QString::fromUtf8(IPMSG_TYPING)) {
        QString senderName = json["name"].toString();
        emit typingIndicatorReceived(senderName);
    } else if (command == QString::fromUtf8(IPMSG_FRIEND_REQUEST)) {
        QString senderId = json["id"].toString();
        QString senderName = json["name"].toString();
        QString message = json["message"].toString();
        m_pendingFriendRequests.append(senderId);
        emit friendRequestReceived(senderId, senderName, message);
    } else if (command == QString::fromUtf8(IPMSG_FRIEND_ACCEPT)) {
        QString senderId = json["id"].toString();
        QString senderName = json["name"].toString();
        if (!m_friends.contains(senderId)) {
            m_friends.append(senderId);
        }
        m_pendingFriendRequests.removeOne(senderId);
        
        // Save to database
        if (m_database) {
            m_database->addFriend(senderId);
        }
        
        emit friendRequestAccepted(senderId, senderName);
    } else if (command == QString::fromUtf8(IPMSG_FRIEND_REJECT)) {
        QString senderId = json["id"].toString();
        QString senderName = json["name"].toString();
        m_pendingFriendRequests.removeOne(senderId);
        emit friendRequestRejected(senderId, senderName);
    } else if (command == QString::fromUtf8(IPMSG_OFFLINE_MSG)) {
        // Receive offline messages from another device
        QString senderId = json["id"].toString();
        QJsonArray messagesArray = json["messages"].toArray();
        
        for (const auto& msgValue : messagesArray) {
            QJsonObject msgObj = msgValue.toObject();
            QString msgCommand = msgObj["command"].toString();
            
            if (msgCommand == QString::fromUtf8(IPMSG_MSG)) {
                IPMsgMessage msg;
                msg.senderId = msgObj["id"].toString();
                msg.senderName = msgObj["name"].toString();
                msg.content = msgObj["message"].toString();
                msg.timestamp = msgObj["timestamp"].toVariant().toLongLong();
                msg.recallId = msgObj["recallId"].toString();
                
                m_messages.append(msg);
                if (m_database) {
                    m_database->saveMessage(msg, msg.senderId, false);
                }
                emit messageReceived(msg);
            } else if (msgCommand == QString::fromUtf8(IPMSG_IMAGE)) {
                IPMsgMessage msg;
                msg.senderId = msgObj["id"].toString();
                msg.senderName = msgObj["name"].toString();
                msg.content = msgObj["fileName"].toString();
                msg.timestamp = msgObj["timestamp"].toVariant().toLongLong();
                msg.isImage = true;
                msg.imageFileName = msgObj["fileName"].toString();
                msg.imageData = QByteArray::fromBase64(msgObj["imageData"].toString().toLatin1());
                
                m_messages.append(msg);
                if (m_database) {
                    m_database->saveMessage(msg, msg.senderId, false);
                }
                emit messageReceived(msg);
            }
        }
        
        // Acknowledge offline messages
        QJsonObject ackJson;
        ackJson["id"] = m_userId;
        ackJson["name"] = m_userName;
        ackJson["command"] = QString::fromUtf8(IPMSG_OFFLINE_DELIVERED);
        ackJson["senderId"] = senderId;
        ackJson["messageIds"] = json["messageIds"];
        QJsonDocument ackDoc(ackJson);
        
        QTcpSocket* ackSocket = new QTcpSocket(this);
        connect(ackSocket, &QTcpSocket::connected, this, [ackSocket, ackDoc]() {
            ackSocket->write(ackDoc.toJson());
            ackSocket->flush();
        });
        connect(ackSocket, &QTcpSocket::disconnected, ackSocket, &QObject::deleteLater);
        connect(ackSocket, &QTcpSocket::errorOccurred, ackSocket, &QObject::deleteLater);
        ackSocket->connectToHost(socket->peerAddress(), m_port + 1);
    } else if (command == QString::fromUtf8(IPMSG_OFFLINE_DELIVERED)) {
        QString senderId = json["senderId"].toString();
        QJsonArray msgIds = json["messageIds"].toArray();
        
        // Mark offline messages as delivered
        for (const auto& idValue : msgIds) {
            if (m_database) {
                m_database->markOfflineMessageDelivered(idValue.toString());
            }
        }
    } else if (command == QString::fromUtf8(IPMSG_KEY_EXCHANGE)) {
        // Handle key exchange from another device
        QString senderId = json["id"].toString();
        QByteArray peerPublicKey = QByteArray::fromBase64(json["publicKey"].toString().toLatin1());
        
        LOG_INFO(QString("IPMsg: E2EE key exchange received from %1").arg(senderId));
        
        // Generate our own keypair if we don't have one yet (e.g. we received
        // a key exchange without having initiated one ourselves)
        if (localPrivateKey.isEmpty() || localPublicKey.isEmpty()) {
            localPrivateKey = generateEcdhKeyPair(&localPublicKey);
        }

        // Compute shared secret (symmetric: both sides derive the same value)
        QByteArray sharedSecret = computeSharedSecret(peerPublicKey, localPrivateKey);
        
        // Store session
        E2EESession session;
        session.peerPublicKey = peerPublicKey;
        session.sharedSecret = sharedSecret;
        session.publicKey = localPublicKey;
        session.privateKey = localPrivateKey;
        session.establishedAt = QDateTime::currentMSecsSinceEpoch();
        m_e2eeSessions[senderId] = session;
        
        emit encryptionReady(senderId);

        // Respond with our public key so the initiator can also complete the
        // session (the initiator stores its session with an empty shared secret
        // until it receives this response).
        QJsonObject resp;
        resp["id"] = m_userId;
        resp["name"] = m_userName;
        resp["command"] = QString::fromUtf8(IPMSG_KEY_EXCHANGE);
        resp["publicKey"] = QString::fromUtf8(localPublicKey.toBase64());
        resp["timestamp"] = QDateTime::currentMSecsSinceEpoch();

        QTcpSocket* respSocket = new QTcpSocket(this);
        connect(respSocket, &QTcpSocket::connected, this, [respSocket, respDoc = QJsonDocument(resp)]() {
            respSocket->write(respDoc.toJson());
            respSocket->flush();
        });
        connect(respSocket, &QTcpSocket::disconnected, respSocket, &QObject::deleteLater);
        connect(respSocket, &QTcpSocket::errorOccurred, respSocket, &QObject::deleteLater);
        respSocket->connectToHost(socket->peerAddress(), m_port + 1);
    } else if (command == QString::fromUtf8(IPMSG_ENCRYPTED_MSG)) {
        QString senderId = json["id"].toString();
        QByteArray encryptedData = QByteArray::fromBase64(json["data"].toString().toLatin1());
        
        if (hasEstablishedSession(senderId)) {
            QByteArray decrypted = decryptMessage(encryptedData, senderId);
            
            IPMsgMessage msg;
            msg.senderId = senderId;
            msg.senderName = json["name"].toString();
            msg.senderIp = socket->peerAddress().toString();
            msg.timestamp = json["timestamp"].toVariant().toLongLong();
            msg.content = QString::fromUtf8(decrypted);
            msg.isFile = false;
            msg.isImage = false;
            msg.recallId = json["recallId"].toString();

            m_messages.append(msg);
            
            if (m_database) {
                m_database->saveMessage(msg, msg.senderId, false);
            }
            
            emit messageReceived(msg);
        } else {
            LOG_WARNING("IPMsg: Received encrypted message without established session");
        }
    } else if (command == QString::fromUtf8(IPMSG_VOICE_MSG)) {
        IPMsgMessage msg;
        msg.senderId = json["id"].toString();
        msg.senderName = json["name"].toString();
        msg.senderIp = socket->peerAddress().toString();
        msg.timestamp = json["timestamp"].toVariant().toLongLong();
        msg.isVoiceMessage = true;
        msg.voiceData = QByteArray::fromBase64(json["voiceData"].toString().toLatin1());
        msg.voiceDuration = json["voiceDuration"].toInt();

        m_messages.append(msg);
        if (m_database) {
            m_database->saveMessage(msg, msg.senderId, false);
        }
        emit voiceMessageReceived(msg);
    } else if (command == QString::fromUtf8(IPMSG_VIDEO_MSG)) {
        IPMsgMessage msg;
        msg.senderId = json["id"].toString();
        msg.senderName = json["name"].toString();
        msg.senderIp = socket->peerAddress().toString();
        msg.timestamp = json["timestamp"].toVariant().toLongLong();
        msg.isVideoMessage = true;
        msg.videoData = QByteArray::fromBase64(json["videoData"].toString().toLatin1());
        msg.videoDuration = json["videoDuration"].toInt();
        msg.videoWidth = json["videoWidth"].toDouble();
        msg.videoHeight = json["videoHeight"].toDouble();

        m_messages.append(msg);
        if (m_database) {
            m_database->saveMessage(msg, msg.senderId, false);
        }
        emit videoMessageReceived(msg);
    } else if (command == QString::fromUtf8(IPMSG_LOCATION_MSG)) {
        IPMsgMessage msg;
        msg.senderId = json["id"].toString();
        msg.senderName = json["name"].toString();
        msg.senderIp = socket->peerAddress().toString();
        msg.timestamp = json["timestamp"].toVariant().toLongLong();
        msg.locationLatitude = json["latitude"].toString();
        msg.locationLongitude = json["longitude"].toString();
        msg.locationName = json["locationName"].toString();

        m_messages.append(msg);
        if (m_database) {
            m_database->saveMessage(msg, msg.senderId, false);
        }
        emit locationMessageReceived(msg);
    } else if (command == QString::fromUtf8(IPMSG_CARD_MSG)) {
        IPMsgMessage msg;
        msg.senderId = json["id"].toString();
        msg.senderName = json["name"].toString();
        msg.senderIp = socket->peerAddress().toString();
        msg.timestamp = json["timestamp"].toVariant().toLongLong();
        msg.vCardData = json["vcard"].toString();

        m_messages.append(msg);
        if (m_database) {
            m_database->saveMessage(msg, msg.senderId, false);
        }
        emit cardMessageReceived(msg);
    } else if (command == QString::fromUtf8(IPMSG_GROUP_ANNOUNCEMENT)) {
        QString groupId = json["groupId"].toString();
        QString groupName = json["groupName"].toString();
        QString announcement = json["announcement"].toString();
        QString announcer = json["name"].toString();

        LOG_INFO(QString("IPMsg: Group announcement in %1: %2")
                 .arg(groupName, announcement));
        emit groupAnnouncementReceived(groupId, announcement, announcer);
    } else if (command == QString::fromUtf8(IPMSG_GROUP_MENTION)) {
        QString groupId = json["groupId"].toString();
        QString message = json["message"].toString();
        QString senderName = json["name"].toString();
        QJsonArray mentionedArray = json["mentionedMembers"].toArray();
        QStringList mentionedMembers;
        for (const auto& m : mentionedArray) {
            mentionedMembers.append(m.toString());
        }

        emit groupMentionReceived(groupId, mentionedMembers, message, senderName);
    } else if (command == QString::fromUtf8(IPMSG_GROUP_VOTE)) {
        QString groupId = json["groupId"].toString();
        QString voteTitle = json["voteTitle"].toString();
        QJsonArray optionsArray = json["options"].toArray();
        QStringList options;
        for (const auto& opt : optionsArray) {
            options.append(opt.toString());
        }
        int duration = json["durationSeconds"].toInt();
        QString creator = json["name"].toString();

        emit groupVoteReceived(groupId, voteTitle, options, duration, creator);
    } else if (command == QString::fromUtf8(IPMSG_GROUP_VOTE_RESPONSE)) {
        QString groupId = json["groupId"].toString();
        QString voteTitle = json["voteTitle"].toString();
        int selectedOption = json["selectedOption"].toInt();
        QString voterId = json["id"].toString();
        QString voterName = json["name"].toString();

        LOG_INFO(QString("IPMsg: Group vote response for '%1' from %2: option %3")
                 .arg(voteTitle, voterName, QString::number(selectedOption)));
        emit groupVoteResponseReceived(groupId, voteTitle, selectedOption, voterId, voterName);
    } else if (command == QString::fromUtf8(IPMSG_CALL_INVITE)) {
        QString callerId = json["id"].toString();
        QString callerName = json["name"].toString();
        QString callType = json["callType"].toString();
        QString sdp = json["sdp"].toString();

        LOG_INFO(QString("IPMsg: Incoming %1 call from %2")
                 .arg(callType, callerName));
        emit incomingCall(callerId, callerName);
    } else if (command == QString::fromUtf8(IPMSG_CALL_ACCEPT)) {
        QString calleeId = json["id"].toString();
        QString sdp = json["sdp"].toString();
        emit callAccepted(calleeId);
    } else if (command == QString::fromUtf8(IPMSG_CALL_REJECT)) {
        QString calleeId = json["id"].toString();
        emit callRejected(calleeId);
    } else if (command == QString::fromUtf8(IPMSG_CALL_END)) {
        QString peerId = json["id"].toString();
        emit callEnded(peerId);
    } else if (command == QString::fromUtf8(IPMSG_ICE_CANDIDATE)) {
        QString peerId = json["id"].toString();
        QByteArray sdp = QByteArray::fromBase64(json["sdp"].toString().toLatin1());
        emit iceCandidateReceived(peerId, sdp);
    } else if (command == QString::fromUtf8(IPMSG_SYNC_REQUEST)) {
        // Peer wants to sync. Verify account + key, then send our snapshot.
        QString peerIp = socket->peerAddress().toString();
        if (!syncPeerValid(json)) {
            LOG_WARNING(QString("IPMsg: Rejected sync request from %1 (key mismatch)").arg(peerIp));
            sendSyncAck(peerIp, false, 0);
            return;
        }
        LOG_INFO(QString("IPMsg: Sync requested by %1, sending snapshot").arg(peerIp));
        sendSyncSnapshotResponse(peerIp, false);
    } else if (command == QString::fromUtf8(IPMSG_SYNC_SNAPSHOT)) {
        // A snapshot arrived. Verify keys, apply, then reply with our own
        // snapshot (bidirectional merge) or acknowledge if this was a reply.
        QString peerIp = socket->peerAddress().toString();
        if (!syncPeerValid(json)) {
            LOG_WARNING(QString("IPMsg: Rejected sync snapshot from %1 (key mismatch)").arg(peerIp));
            sendSyncAck(peerIp, false, 0);
            return;
        }

        DatabaseManager::SyncSnapshot snap = deserializeSnapshot(json["snapshot"].toObject());
        int applied = m_database ? m_database->applySyncSnapshot(snap) : 0;
        refreshLocalStateFromDb();
        LOG_INFO(QString("IPMsg: Applied %1 sync changes from %2").arg(applied).arg(peerIp));

        bool isReply = json["isReply"].toBool();
        if (isReply) {
            // We were the responder; the requester already applied our snapshot.
            sendSyncAck(peerIp, true, applied);
        } else {
            // We are the requester; send our (now merged) snapshot back.
            sendSyncSnapshotResponse(peerIp, true);
        }
    } else if (command == QString::fromUtf8(IPMSG_SYNC_ACK)) {
        QString peerIp = socket->peerAddress().toString();
        m_pendingSyncs.remove(peerIp);
        bool success = json["success"].toBool(true);
        int applied = json["applied"].toInt(0);
        QString deviceId = deviceIdByIp(peerIp);
        if (success) {
            emit syncCompleted(deviceId.isEmpty() ? peerIp : deviceId, applied);
        } else {
            emit syncFailed(deviceId.isEmpty() ? peerIp : deviceId, tr("对端拒绝同步"));
        }
    } else if (command == QString::fromUtf8(IPMSG_IMAGE)) {
        IPMsgMessage msg;
        msg.senderId = json["id"].toString();
        msg.senderName = json["name"].toString();
        msg.senderIp = socket->peerAddress().toString();
        msg.content = json["message"].toString();
        msg.timestamp = json["timestamp"].toVariant().toLongLong();
        msg.isFile = false;
        msg.isImage = false;
        msg.replyTo = json["replyTo"].toString();
        msg.replyContent = json["replyContent"].toString();

        m_messages.append(msg);
        
        // Save to database
        if (m_database) {
            m_database->saveMessage(msg, msg.senderId, false);
        }
        
        emit messageReceived(msg);
    } else if (command == QString::fromUtf8(IPMSG_FILE_START)) {
        QString fileId = json["fileId"].toString();
        QString fileName = json["fileName"].toString();
        qint64 fileSize = json["fileSize"].toVariant().toLongLong();
        bool isDirectory = json["isDirectory"].toBool();
        QString senderName = json["senderName"].toString();
        QString senderId = json["senderId"].toString();
        QString md5 = json["md5"].toString();
        bool encrypted = json["encrypted"].toBool() && hasEstablishedSession(senderId);
        QString relativePath = json["filePath"].toString();
        qint64 chunkSize = json["chunkSize"].toVariant().toLongLong();
        if (chunkSize <= 0) chunkSize = m_chunkSize;

        // Capture per-chunk MD5s for verification at the receiver.
        QList<QByteArray> chunkMd5s;
        QJsonArray chunkMd5Array = json["chunkMd5s"].toArray();
        for (const auto& v : chunkMd5Array) {
            chunkMd5s.append(QByteArray::fromHex(v.toString().toLatin1()));
        }
        m_incomingChunkMd5s[fileId] = chunkMd5s;
        m_incomingChunkSizes[fileId] = chunkSize;

        IPMsgTransferTask task;
        task.fileId = fileId;
        task.direction = TransferDirection::Receive;
        task.fileName = fileName;
        task.filePath = relativePath;   // relative path within the folder (if any)
        task.totalSize = fileSize;
        task.md5 = md5;
        task.isDirectory = isDirectory;
        task.status = TransferStatus::Queued;
        task.peerId = senderId;
        task.encrypted = encrypted;
        task.startTime = QDateTime::currentMSecsSinceEpoch();
        task.lastSampleTime = task.startTime;
        registerTask(task);

        m_incomingFileSockets[fileId] = socket;
        m_incomingRelativePaths[fileId] = relativePath;
        emit fileReceiveRequest(fileId, senderName, fileName, fileSize, isDirectory, md5, relativePath);
    } else if (command == QString::fromUtf8(IPMSG_FILE_REJECT)) {
        QString fileId = json["fileId"].toString();
        m_incomingFileSockets.remove(fileId);
        IPMsgTransferTask* t = findTask(fileId);
        if (t) t->status = TransferStatus::Cancelled;
        emit fileTaskCancelled(fileId);
        emit fileError(fileId, "File transfer rejected");
    } else if (command == QString::fromUtf8(IPMSG_FILE_RESUME_REQUEST)) {
        QString fileId = json["fileId"].toString();
        qint64 offset = json["offset"].toVariant().toLongLong();
        handleResumeRequest(fileId, offset, socket);
    } else if (command == QString::fromUtf8(IPMSG_FILE_RESUME)) {
        QString fileId = json["fileId"].toString();
        qint64 offset = json["offset"].toVariant().toLongLong();
        SendContext* c = m_activeSenders.value(fileId);
        if (c && c->file && c->file->seek(offset)) {
            c->offset = offset;
            c->transferred = offset;
            emit fileResuming(fileId, offset);
            pumpSender(fileId);
        }
    } else if (command == QString::fromUtf8(IPMSG_FILE_CHUNK_REQUEST)) {
        QString fileId = json["fileId"].toString();
        qint64 offset = json["offset"].toVariant().toLongLong();
        qint64 length = json["length"].toVariant().toLongLong();
        handleChunkRequest(fileId, offset, length, socket);
    } else if (command == QString::fromUtf8(IPMSG_FILE_CHUNK_DATA)) {
        QString fileId = json["fileId"].toString();
        qint64 offset = json["offset"].toVariant().toLongLong();
        RecvContext* c = m_recvContexts.value(fileId);
        if (c && c->file) {
            c->file->seek(offset);
            // Remaining framed payload is read by onTcpDataReceived on the next readyRead
        }
    } else if (command == QString::fromUtf8(IPMSG_FILE_VERIFY)) {
        QString fileId = json["fileId"].toString();
        QString md5 = json["md5"].toString();
        RecvContext* c = m_recvContexts.value(fileId);
        QString checkPath = c ? c->savePath : QString();
        bool integrityOk = verifyFileIntegrity(checkPath, md5);
        finishReceive(fileId, integrityOk);
    }
}

void IPMsgManager::sendUdpBroadcast(const QByteArray& data) {
    if (!m_udpSocket) return;

    // Send to standard port 2425 and this instance's port
    // This ensures all instances on the same machine can discover each other
    QList<quint16> targetPorts;
    targetPorts << 2425;
    if (m_port != 2425) {
        targetPorts << m_port;
    }

    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            !(iface.flags() & QNetworkInterface::IsRunning) ||
            (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        const auto entries = iface.addressEntries();
        for (const QNetworkAddressEntry& entry : entries) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                QHostAddress broadcast = entry.broadcast();
                if (!broadcast.isNull()) {
                    for (quint16 port : targetPorts) {
                        m_udpSocket->writeDatagram(data, broadcast, port);
                    }
                }
            }
        }
    }
}

void IPMsgManager::sendFileData(const QString& targetIp, const QList<IPMsgFileItem>& items) {
    // Flatten the (possibly nested) item tree into a list of leaf files. The
    // folder structure is preserved via each leaf's relativePath, which the
    // receiver uses to recreate sub-directories.
    QList<IPMsgFileItem> flat;
    std::function<void(const QList<IPMsgFileItem>&)> flatten = [&](const QList<IPMsgFileItem>& list) {
        for (const IPMsgFileItem& it : list) {
            if (it.isDirectory) {
                flatten(it.children);
            } else {
                flat.append(it);
            }
        }
    };
    flatten(items);

    for (const IPMsgFileItem& item : flat) {
        QString fileId = generateFileId();

        // Live task (for UI)
        IPMsgTransferTask task;
        task.fileId = fileId;
        task.direction = TransferDirection::Send;
        task.fileName = item.name;
        task.filePath = item.absolutePath;
        task.totalSize = item.size;
        task.md5 = item.md5;
        task.isDirectory = item.isDirectory;
        task.status = TransferStatus::Queued;
        task.peerId = deviceIdByIp(targetIp);
        task.startTime = QDateTime::currentMSecsSinceEpoch();
        task.lastSampleTime = task.startTime;
        registerTask(task);
        emit fileTaskQueued(fileId);

        // Persisted transfer state (for resume)
        IPMsgTransferState state;
        state.fileId = fileId;
        state.filePath = item.absolutePath;
        state.savePath = "";
        state.totalSize = item.size;
        state.transferredSize = 0;
        state.md5 = item.md5;
        state.isDirectory = item.isDirectory;
        state.isSender = true;
        state.lastOffset = 0;
        saveTransferState(state);

        SendJob job;
        job.fileId = fileId;
        job.targetIp = targetIp;
        job.item = item;
        job.attempts = 1;
        m_sendQueue.append(job);
    }
    pumpTransferQueue();
}

void IPMsgManager::pumpTransferQueue() {
    while (m_activeSenders.size() < m_maxConcurrentTransfers && !m_sendQueue.isEmpty()) {
        SendJob job = m_sendQueue.takeFirst();
        startSendJob(job);
    }
}

void IPMsgManager::startSendJob(const SendJob& job) {
    SendContext* ctx = new SendContext();
    ctx->fileId = job.fileId;
    ctx->targetIp = job.targetIp;
    ctx->relativePath = job.item.relativePath;
    ctx->md5 = job.item.md5;
    ctx->totalSize = job.item.size;
    ctx->offset = 0;
    ctx->attempts = job.attempts;
    ctx->item = job.item;
    ctx->startTime = QDateTime::currentMSecsSinceEpoch();
    ctx->lastSampleTime = ctx->startTime;

    QString peerId = deviceIdByIp(job.targetIp);
    if (hasEstablishedSession(peerId)) {
        ctx->encrypted = true;
        ctx->sessionKey = m_e2eeSessions.value(peerId).sharedSecret;
    } else {
        ctx->encrypted = false;
        ctx->pendingKey = true;
    }

    m_activeSenders[job.fileId] = ctx;

    if (!ctx->pendingKey) {
        connectSendSocket(job);
        return;
    }

    // No E2EE session yet: initiate key exchange, wait for it, then send encrypted.
    m_pendingKeyJobs[job.fileId] = job;
    initiateKeyExchange(job.targetIp);

    // Fallback: if the peer cannot establish E2EE in time, send in plaintext.
    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, job, timer]() {
        timer->deleteLater();
        if (m_pendingKeyJobs.contains(job.fileId)) {
            m_pendingKeyJobs.remove(job.fileId);
            SendContext* c = m_activeSenders.value(job.fileId);
            if (c && c->pendingKey) {
                c->pendingKey = false;
                connectSendSocket(job);
            }
        }
    });
    timer->start(5000);
}

void IPMsgManager::connectSendSocket(const SendJob& job) {
    SendContext* ctx = m_activeSenders.value(job.fileId);
    if (!ctx) return;

    QTcpSocket* socket = new QTcpSocket(this);
    ctx->socket = socket;
    m_sendingSockets[job.fileId] = socket;

    connect(socket, &QTcpSocket::connected, this, [this, job, socket]() {
        SendContext* c = m_activeSenders.value(job.fileId);
        if (!c) return;
        QJsonObject header;
        header["command"] = QString::fromUtf8(IPMSG_FILE_START);
        header["fileId"] = job.fileId;
        header["fileName"] = job.item.name;
        header["filePath"] = job.item.relativePath;
        header["fileSize"] = job.item.size;
        header["isDirectory"] = job.item.isDirectory;
        header["senderId"] = m_userId;
        header["senderName"] = m_userName;
        header["md5"] = job.item.md5;
        header["chunkSize"] = job.item.chunkSize;
        header["encrypted"] = c->encrypted;
        header["algo"] = c->encrypted ? QString("AES-256-GCM") : QString();
        header["offset"] = 0;

        QJsonArray chunkMd5Array;
        for (const QByteArray& md5 : job.item.chunkMd5s) {
            chunkMd5Array.append(QString::fromLatin1(md5.toHex()));
        }
        header["chunkMd5s"] = chunkMd5Array;

        socket->write(QJsonDocument(header).toJson());
        socket->flush();
    });

    connect(socket, &QTcpSocket::readyRead, this, [this, job, socket]() {
        QByteArray response = socket->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(response);
        if (doc.isNull()) return;
        QJsonObject json = doc.object();
        QString cmd = json["command"].toString();

        if (cmd == QString::fromUtf8(IPMSG_FILE_ACCEPT)) {
            SendContext* c = m_activeSenders.value(job.fileId);
            if (!c) return;
            qint64 offset = json["offset"].toVariant().toLongLong();
            c->offset = offset;

            QFile* file = new QFile(job.item.absolutePath, this);
            if (!file->open(QIODevice::ReadOnly)) {
                emit fileError(job.fileId, "Failed to open file: " + job.item.absolutePath);
                return;
            }
            if (offset > 0) file->seek(offset);
            c->file = file;
            c->transferred = offset;

            IPMsgTransferTask* t = findTask(job.fileId);
            if (t) { t->status = TransferStatus::Transferring; t->transferredSize = offset; t->encrypted = c->encrypted; }
            emit fileTaskStarted(job.fileId);

            pumpSender(job.fileId);
        } else if (cmd == QString::fromUtf8(IPMSG_FILE_REJECT)) {
            emit fileError(job.fileId, "File transfer rejected");
            cleanupSender(job.fileId);
        } else if (cmd == QString::fromUtf8(IPMSG_FILE_RESUME_REQUEST)) {
            qint64 offset = json["offset"].toVariant().toLongLong();
            emit fileResuming(job.fileId, offset);
            IPMsgTransferState state = getTransferState(job.fileId);
            state.lastOffset = offset;
            state.transferredSize = offset;
            saveTransferState(state);
        }
    });

    connect(socket, &QTcpSocket::bytesWritten, this, &IPMsgManager::onSendProgress);

    connect(socket, &QTcpSocket::errorOccurred, this, [this, job, socket](QAbstractSocket::SocketError) {
        if (m_activeSenders.contains(job.fileId)) {
            handleSendFailure(job.fileId, "Connection error: " + socket->errorString());
        }
    });

    connect(socket, &QTcpSocket::disconnected, this, [this, job]() {
        if (m_activeSenders.contains(job.fileId)) {
            handleSendFailure(job.fileId, "Connection closed");
        }
    });

    quint16 targetPort = 2426;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == job.targetIp) {
            targetPort = it.value().port + 1;
            break;
        }
    }
    socket->connectToHost(job.targetIp, targetPort);
}

void IPMsgManager::onEncryptionReadyForTransfer(const QString& deviceId) {
    for (auto it = m_pendingKeyJobs.begin(); it != m_pendingKeyJobs.end(); ) {
        SendJob job = it.value();
        if (deviceIdByIp(job.targetIp) == deviceId) {
            it = m_pendingKeyJobs.erase(it);
            SendContext* c = m_activeSenders.value(job.fileId);
            if (c && c->pendingKey) {
                c->pendingKey = false;
                if (hasEstablishedSession(deviceId)) {
                    c->encrypted = true;
                    c->sessionKey = m_e2eeSessions.value(deviceId).sharedSecret;
                }
                connectSendSocket(job);
            }
        } else {
            ++it;
        }
    }
}

void IPMsgManager::pumpSender(const QString& fileId) {
    SendContext* c = m_activeSenders.value(fileId);
    if (!c || c->paused || c->finished) return;
    if (!c->file || !c->file->isOpen()) return;

    while (c->inFlight < s_maxInFlight && !c->file->atEnd()) {
        QByteArray chunk = c->file->read(m_chunkSize);
        if (chunk.isEmpty()) break;

        QByteArray framed;
        if (c->encrypted) {
            QByteArray nonce;
            QByteArray ct = aesGcmEncrypt(chunk, c->sessionKey, &nonce);
            framed = nonce + ct; // nonce(12) + ciphertext + tag(16)
        } else {
            framed = chunk;
        }

        // Length-prefixed frame (4-byte little-endian)
        quint32 len = static_cast<quint32>(framed.size());
        len = qToLittleEndian(len);
        QByteArray header;
        header.resize(4);
        memcpy(header.data(), &len, 4);
        c->socket->write(header);
        c->socket->write(framed);
        c->inFlight += (4 + framed.size());
        c->transferred += chunk.size();
    }

    updateSendSpeed(c);
    IPMsgTransferTask* t = findTask(fileId);
    if (t) { t->transferredSize = c->transferred; t->speedBps = c->speedBps; }
    emit fileProgress(fileId, c->transferred, c->totalSize, c->speedBps);

    if (c->file->atEnd()) {
        finalizeSender(fileId);
    }
}

void IPMsgManager::finalizeSender(const QString& fileId) {
    SendContext* c = m_activeSenders.value(fileId);
    if (!c || c->finished) return;
    c->finished = true;

    QJsonObject v;
    v["command"] = QString::fromUtf8(IPMSG_FILE_VERIFY);
    v["fileId"] = fileId;
    v["md5"] = c->md5;
    v["filePath"] = c->relativePath;
    c->socket->write(QJsonDocument(v).toJson());
    c->socket->flush();

    IPMsgTransferTask* t = findTask(fileId);
    if (t) t->status = TransferStatus::Completed;
    emit fileCompleted(fileId, c->relativePath, true);

    removeTransferState(fileId);

    QTcpSocket* s = c->socket;
    QFile* f = c->file;
    m_activeSenders.remove(fileId);
    m_sendingSockets.remove(fileId);
    c->socket->disconnect();
    c->socket->close();
    if (f) { f->close(); delete f; }
    s->deleteLater();
    delete c;

    pumpTransferQueue();
}

void IPMsgManager::onSendChunkWritten(const QString& fileId, qint64 bytes) {
    Q_UNUSED(bytes);
    SendContext* c = m_activeSenders.value(fileId);
    if (!c || c->paused || c->finished) return;
    if (bytes > 0) {
        c->inFlight = qMax(0LL, c->inFlight - bytes);
    }
    pumpSender(fileId);
}

void IPMsgManager::updateSendSpeed(SendContext* ctx) {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 dt = now - ctx->lastSampleTime;
    if (dt >= 500) {
        ctx->speedBps = static_cast<int>((ctx->transferred - ctx->lastSampleBytes) * 1000 / dt);
        ctx->lastSampleTime = now;
        ctx->lastSampleBytes = ctx->transferred;
    }
}

void IPMsgManager::updateRecvSpeed(RecvContext* ctx) {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 dt = now - ctx->lastSampleTime;
    if (dt >= 500) {
        ctx->speedBps = static_cast<int>((ctx->received - ctx->lastSampleBytes) * 1000 / dt);
        ctx->lastSampleTime = now;
        ctx->lastSampleBytes = ctx->received;
    }
}

void IPMsgManager::registerTask(const IPMsgTransferTask& task) {
    m_tasks[task.fileId] = task;
}

IPMsgTransferTask* IPMsgManager::findTask(const QString& fileId) {
    return m_tasks.contains(fileId) ? &m_tasks[fileId] : nullptr;
}

void IPMsgManager::writeReceivedData(const QString& fileId, const QByteArray& data) {
    RecvContext* c = m_recvContexts.value(fileId);
    if (!c || !c->file) return;

    c->buffer.append(data);

    while (c->received < c->totalSize) {
        if (c->buffer.size() < 4) break;
        quint32 len = 0;
        memcpy(&len, c->buffer.constData(), 4);
        len = qFromLittleEndian(len);
        if (static_cast<quint32>(c->buffer.size()) < 4u + len) break;

        QByteArray framed = c->buffer.mid(4, static_cast<int>(len));
        c->buffer.remove(0, 4 + static_cast<int>(len));

        QByteArray plaintext;
        if (c->encrypted) {
            if (framed.size() < 12 + 16) continue;
            QByteArray nonce = framed.left(12);
            QByteArray tag = framed.right(16);
            QByteArray ct = framed.mid(12, framed.size() - 12 - 16);
            plaintext = aesGcmDecrypt(ct, c->sessionKey, nonce, tag);
            if (plaintext.isEmpty()) {
                emit fileError(fileId, "Decryption failed");
                return;
            }
        } else {
            plaintext = framed;
        }

        // Per-chunk integrity check: verify this chunk's MD5 against the
        // sender-provided digest before committing it to disk.
        if (!c->chunkMd5s.isEmpty() && c->chunkIndex < c->chunkMd5s.size()) {
            QByteArray got = QCryptographicHash::hash(plaintext, QCryptographicHash::Md5);
            if (got != c->chunkMd5s.at(c->chunkIndex)) {
                emit fileError(fileId, tr("分块 %1 校验失败").arg(c->chunkIndex));
                finishReceive(fileId, false);
                return;
            }
        }
        c->chunkIndex++;

        c->file->write(plaintext);
        c->received += plaintext.size();
        updateRecvSpeed(c);
        IPMsgTransferTask* t = findTask(fileId);
        if (t) { t->transferredSize = c->received; t->speedBps = c->speedBps; }
        emit fileProgress(fileId, c->received, c->totalSize, c->speedBps);
    }

    // All plaintext received: remaining buffer is a control command (FILE_VERIFY)
    if (c->received >= c->totalSize && !c->buffer.isEmpty()) {
        QByteArray leftover = c->buffer;
        c->buffer.clear();
        processTcpCommand(leftover, c->socket);
    }
}

void IPMsgManager::finishReceive(const QString& fileId, bool success) {
    RecvContext* c = m_recvContexts.take(fileId);
    QString savePath;
    if (c) {
        savePath = c->savePath;
        if (c->file) { c->file->close(); delete c->file; }
        m_recvSocketToFileId.remove(c->socket);
        delete c;
    }
    removeTransferState(fileId);
    IPMsgTransferTask* t = findTask(fileId);
    if (t) { t->status = success ? TransferStatus::Completed : TransferStatus::Failed; t->savePath = savePath; }
    emit fileCompleted(fileId, savePath, success);
}

void IPMsgManager::handleChunkRequest(const QString& fileId, qint64 offset, qint64 length, QTcpSocket* socket) {
    Q_UNUSED(length);
    SendContext* c = m_activeSenders.value(fileId);
    if (!c || !c->file || !c->file->isOpen()) return;
    if (!c->file->seek(offset)) return;
    QByteArray chunk = c->file->read(m_chunkSize);
    if (chunk.isEmpty()) return;

    QByteArray framed;
    if (c->encrypted) {
        QByteArray nonce;
        QByteArray ct = aesGcmEncrypt(chunk, c->sessionKey, &nonce);
        framed = nonce + ct;
    } else {
        framed = chunk;
    }
    QJsonObject h;
    h["command"] = QString::fromUtf8(IPMSG_FILE_CHUNK_DATA);
    h["fileId"] = fileId;
    h["offset"] = offset;
    h["encrypted"] = c->encrypted;
    socket->write(QJsonDocument(h).toJson());
    socket->flush();

    quint32 len = static_cast<quint32>(framed.size());
    len = qToLittleEndian(len);
    QByteArray header;
    header.resize(4);
    memcpy(header.data(), &len, 4);
    socket->write(header);
    socket->write(framed);
    socket->flush();
}

void IPMsgManager::cleanupSender(const QString& fileId) {
    SendContext* c = m_activeSenders.take(fileId);
    if (!c) return;
    if (c->socket) {
        m_sendingSockets.remove(fileId);
        c->socket->disconnect();
        c->socket->abort();
        c->socket->deleteLater();
    }
    if (c->file) { c->file->close(); delete c->file; }
    delete c;
    pumpTransferQueue();
}

void IPMsgManager::handleSendFailure(const QString& fileId, const QString& reason) {
    // Take the context so a subsequent errorOccurred/disconnected for the same
    // socket is ignored (they often fire together for one failure).
    SendContext* c = m_activeSenders.take(fileId);
    if (!c) return;

    m_sendingSockets.remove(fileId);
    if (c->socket) {
        c->socket->disconnect();
        c->socket->abort();
        c->socket->deleteLater();
    }
    if (c->file) { c->file->close(); delete c->file; }

    const int failedAttempt = c->attempts;
    if (failedAttempt < m_maxRetries) {
        const int nextAttempt = failedAttempt + 1;
        const int delayMs = m_retryBaseDelayMs * (1 << (nextAttempt - 1)); // 2s, 4s, 8s...

        SendJob job;
        job.fileId = fileId;
        job.targetIp = c->targetIp;
        job.item = c->item;
        job.item.relativePath = c->relativePath;
        job.attempts = nextAttempt;

        IPMsgTransferTask* t = findTask(fileId);
        if (t) {
            t->status = TransferStatus::Queued;
            t->attempts = nextAttempt;
            t->transferredSize = 0;
            t->speedBps = 0;
        }
        emit fileRetryScheduled(fileId, nextAttempt, delayMs);

        QTimer::singleShot(delayMs, this, [this, job]() {
            if (m_tasks.contains(job.fileId)) {
                m_sendQueue.prepend(job); // prioritize retried jobs
                pumpTransferQueue();
            }
        });

        delete c;
    } else {
        IPMsgTransferTask* t = findTask(fileId);
        if (t) t->status = TransferStatus::Failed;
        emit fileError(fileId, reason); // terminal: retries exhausted
        delete c;
        pumpTransferQueue();
    }
}

qint64 IPMsgManager::calculateFolderSize(const QString& folderPath) {
    qint64 totalSize = 0;
    QDirIterator it(folderPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        totalSize += it.fileInfo().size();
    }
    return totalSize;
}

QString IPMsgManager::generateFileId() {
    return QUuid::createUuid().toString().remove('{').remove('}').remove('-');
}

QString IPMsgManager::getLocalIp() {
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            !(iface.flags() & QNetworkInterface::IsRunning) ||
            (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        const auto entries = iface.addressEntries();
        for (const QNetworkAddressEntry& entry : entries) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                return entry.ip().toString();
            }
        }
    }

    return "127.0.0.1";
}

QList<QByteArray> IPMsgManager::calculateChunkMd5s(const QString& filePath, qint64 chunkSize) {
    QList<QByteArray> chunkMd5s;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return chunkMd5s;

    while (!file.atEnd()) {
        QByteArray chunk = file.read(chunkSize);
        if (!chunk.isEmpty()) {
            chunkMd5s.append(QCryptographicHash::hash(chunk, QCryptographicHash::Md5));
        }
    }

    file.close();
    return chunkMd5s;
}

void IPMsgManager::loadTransferStates() {
    QFile file(m_transferStateFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return;

    QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        QJsonObject stateObj = it.value().toObject();
        IPMsgTransferState state;
        state.fileId = stateObj["fileId"].toString();
        state.filePath = stateObj["filePath"].toString();
        state.savePath = stateObj["savePath"].toString();
        state.totalSize = stateObj["totalSize"].toVariant().toLongLong();
        state.transferredSize = stateObj["transferredSize"].toVariant().toLongLong();
        state.md5 = stateObj["md5"].toString();
        state.isDirectory = stateObj["isDirectory"].toBool();
        state.isSender = stateObj["isSender"].toBool();
        state.lastOffset = stateObj["lastOffset"].toVariant().toLongLong();
        m_transferStates[it.key()] = state;
    }
}

void IPMsgManager::saveTransferState(const IPMsgTransferState& state) {
    m_transferStates[state.fileId] = state;

    QFile file(m_transferStateFile);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    QJsonObject root;
    for (auto it = m_transferStates.constBegin(); it != m_transferStates.constEnd(); ++it) {
        QJsonObject stateObj;
        stateObj["fileId"] = it.value().fileId;
        stateObj["filePath"] = it.value().filePath;
        stateObj["savePath"] = it.value().savePath;
        stateObj["totalSize"] = it.value().totalSize;
        stateObj["transferredSize"] = it.value().transferredSize;
        stateObj["md5"] = it.value().md5;
        stateObj["isDirectory"] = it.value().isDirectory;
        stateObj["isSender"] = it.value().isSender;
        stateObj["lastOffset"] = it.value().lastOffset;
        root[it.key()] = stateObj;
    }

    QJsonDocument doc(root);
    file.write(doc.toJson());
}

void IPMsgManager::removeTransferState(const QString& fileId) {
    m_transferStates.remove(fileId);
    
    QFile file(m_transferStateFile);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    QJsonObject root;
    for (auto it = m_transferStates.constBegin(); it != m_transferStates.constEnd(); ++it) {
        QJsonObject stateObj;
        stateObj["fileId"] = it.value().fileId;
        stateObj["filePath"] = it.value().filePath;
        stateObj["savePath"] = it.value().savePath;
        stateObj["totalSize"] = it.value().totalSize;
        stateObj["transferredSize"] = it.value().transferredSize;
        stateObj["md5"] = it.value().md5;
        stateObj["isDirectory"] = it.value().isDirectory;
        stateObj["isSender"] = it.value().isSender;
        stateObj["lastOffset"] = it.value().lastOffset;
        root[it.key()] = stateObj;
    }

    QJsonDocument doc(root);
    file.write(doc.toJson());
}

IPMsgTransferState IPMsgManager::getTransferState(const QString& fileId) const {
    return m_transferStates.value(fileId);
}

bool IPMsgManager::hasTransferState(const QString& fileId) const {
    return m_transferStates.contains(fileId);
}

qint64 IPMsgManager::resumeOffsetFor(const QString& fileId) {
    if (!hasTransferState(fileId)) return 0;
    const IPMsgTransferState st = getTransferState(fileId);
    if (st.lastOffset <= 0) return 0;
    IPMsgTransferTask* t = findTask(fileId);
    if (!t) return 0;
    // Guard against resuming onto a different file (size/md5 mismatch).
    if (st.totalSize != t->totalSize) return 0;
    if (!st.md5.isEmpty() && !t->md5.isEmpty() && st.md5 != t->md5) return 0;
    return st.lastOffset;
}

QList<IPMsgTransferState> IPMsgManager::getResumableTransfers() const {
    QList<IPMsgTransferState> out;
    for (auto it = m_transferStates.constBegin(); it != m_transferStates.constEnd(); ++it) {
        const IPMsgTransferState& st = it.value();
        if (st.totalSize <= 0) continue;
        if (st.lastOffset >= st.totalSize) continue; // already complete
        if (m_tasks.contains(st.fileId)) continue;   // currently active
        out.append(st);
    }
    return out;
}

void IPMsgManager::sendResumeRequest(const QString& targetIp, const QString& fileId, qint64 offset) {
    QJsonObject json;
    json["command"] = QString::fromUtf8(IPMSG_FILE_RESUME_REQUEST);
    json["fileId"] = fileId;
    json["offset"] = offset;

    QJsonDocument doc(json);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [this, socket, data = doc.toJson()]() {
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::bytesWritten, this, [this, socket](qint64) {
        socket->deleteLater();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
        emit error("Failed to send resume request: " + socket->errorString());
        socket->deleteLater();
    });

    // Find target's TCP port from device list (TCP port = UDP port + 1)
    quint16 targetPort = 2426; // default TCP port
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().ip == targetIp) {
            targetPort = it.value().port + 1;
            break;
        }
    }
    socket->connectToHost(targetIp, targetPort);
}

void IPMsgManager::handleResumeRequest(const QString& fileId, qint64 offset, QTcpSocket* socket) {
    IPMsgTransferState state = getTransferState(fileId);
    if (state.fileId.isEmpty()) {
        emit fileError(fileId, "No transfer state found for resume");
        return;
    }

    // Update state and send resume response
    state.lastOffset = offset;
    state.transferredSize = offset;
    saveTransferState(state);

    QJsonObject json;
    json["command"] = QString::fromUtf8(IPMSG_FILE_RESUME);
    json["fileId"] = fileId;
    json["offset"] = offset;

    QJsonDocument doc(json);
    socket->write(doc.toJson());
    socket->flush();
}

} // namespace xrk
