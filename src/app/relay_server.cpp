#include "relay_server.h"
#include "device_registry.h"
#include "core/logger.h"
#include "core/types.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace xrk {

RelayServer::RelayServer(QObject* parent) : QObject(parent) {
    m_cleanupTimer = new QTimer(this);
    connect(m_cleanupTimer, &QTimer::timeout, this, &RelayServer::onCleanupTimer);
}

RelayServer::~RelayServer() {
    stop();
}

bool RelayServer::start(quint16 port) {
    if (m_running) return true;

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &RelayServer::onNewConnection);

    if (!m_server->listen(QHostAddress::Any, port)) {
        LOG_ERROR("RelayServer: failed on port " + QString::number(port) +
                  ": " + m_server->errorString());
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }

    // Capture the actual listening port (when port==0 the OS assigns one).
    m_port = m_server->serverPort();

    // UDP rendezvous on the SAME port (separate protocol) for HELLO keepalive
    // and public-endpoint discovery used by P2P hole punching.
    m_udpSock = new QUdpSocket(this);
    if (!m_udpSock->bind(QHostAddress::Any, m_port)) {
        LOG_WARNING("RelayServer: UDP rendezvous bind failed on port " +
                    QString::number(m_port) + ": " + m_udpSock->errorString());
        m_udpSock->deleteLater();
        m_udpSock = nullptr;
    } else {
        connect(m_udpSock, &QUdpSocket::readyRead, this, &RelayServer::onUdpReadyRead);
    }

    m_running = true;
    m_cleanupTimer->start(CLEANUP_INTERVAL_MS);
    LOG_INFO("RelayServer started on port " + QString::number(port));
    emit serverStarted(port);
    return true;
}

void RelayServer::setSecret(const QString& secret) {
    m_secret = secret;
}

void RelayServer::setDeviceRegistry(DeviceRegistry* registry) {
    m_deviceRegistry = registry;
    if (m_deviceRegistry) {
        connect(m_deviceRegistry, &DeviceRegistry::deviceListChanged, this, [this]() {
            LOG_DEBUG("RelayServer: device list changed, peers: " + QString::number(m_registeredPeers.size()));
        });
        connect(m_deviceRegistry, &DeviceRegistry::deviceRegistered, this, [this](const QString& deviceId) {
            LOG_INFO("RelayServer: device registered: " + deviceId);
        });
        connect(m_deviceRegistry, &DeviceRegistry::deviceRemoved, this, [this](const QString& deviceId) {
            LOG_INFO("RelayServer: device removed: " + deviceId);
        });
    }
}

DeviceRegistry* RelayServer::deviceRegistry() const {
    return m_deviceRegistry;
}

void RelayServer::stop() {
    if (!m_running) return;
    m_running = false;
    m_cleanupTimer->stop();

    // Close live connections. The peer/bridge sockets are children of
    // m_server (and thus ultimately of this object), so they are destroyed
    // automatically when m_server / this is destroyed. Deleting them here
    // would double-free, so just close them and clear the bookkeeping.
    for (auto it = m_registeredPeers.begin(); it != m_registeredPeers.end(); ++it) {
        if (it.value()) it.value()->close();
    }
    for (auto it = m_bridges.begin(); it != m_bridges.end(); ++it) {
        if (it.value()) it.value()->close();
        if (it.key()) it.key()->close();
    }
    m_registeredPeers.clear();
    m_peerIds.clear();
    m_buffers.clear();
    m_bridges.clear();
    m_bridgeBuffers.clear();
    m_publicIps.clear();

    if (m_udpSock) {
        m_udpSock->close();   // owned by this object; deleted by ~RelayServer
        m_udpSock = nullptr;
    }
    if (m_server) {
        m_server->close();    // owned by this object; deleted by ~RelayServer
        m_server = nullptr;
    }
    LOG_INFO("RelayServer stopped");
    emit serverStopped();
}

bool RelayServer::isRunning() const { return m_running; }
quint16 RelayServer::port() const { return m_port; }
QStringList RelayServer::connectedPeers() const { return m_registeredPeers.keys(); }

void RelayServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, &RelayServer::onClientData);
        connect(socket, &QTcpSocket::disconnected, this, &RelayServer::onClientDisconnected);
        m_buffers[socket] = QByteArray();
        LOG_DEBUG("RelayServer: new connection from " +
                  socket->peerAddress().toString());
    }
}

void RelayServer::onClientData() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    if (m_bridges.contains(socket)) {
        QTcpSocket* target = m_bridges.value(socket);
        if (target && target->state() == QAbstractSocket::ConnectedState) {
            QByteArray data = socket->readAll();
            target->write(data);
            target->flush();
        }
        return;
    }

    m_buffers[socket].append(socket->readAll());
    QByteArray& buf = m_buffers[socket];
    while (true) {
        int pos = buf.indexOf('\n');
        if (pos < 0) break;
        QString line = QString::fromUtf8(buf.left(pos)).trimmed();
        buf.remove(0, pos + 1);
        if (!line.isEmpty()) processLine(socket, line);
    }
}

void RelayServer::onClientDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QString deviceId = m_peerIds.take(socket);
    if (!deviceId.isEmpty()) {
        m_registeredPeers.remove(deviceId);
        m_publicIps.remove(deviceId);
        
        // Update device registry if available
        if (m_deviceRegistry && m_deviceRegistry->containsDevice(deviceId)) {
            m_deviceRegistry->setDeviceOnline(deviceId, false);
            LOG_INFO("RelayServer: device went offline: " + deviceId);
        }
        
        LOG_INFO("RelayServer: peer left: " + deviceId);
        emit peerDisconnected(deviceId);
    }

    if (m_bridges.contains(socket)) {
        QTcpSocket* other = m_bridges.take(socket);
        m_bridges.remove(other);
        m_bridgeBuffers.remove(other);
        if (other->state() == QAbstractSocket::ConnectedState) {
            sendLine(other, "BRIDGE_CLOSED");
            other->flush();
        }
        other->close();
        other->deleteLater();
    }

    m_buffers.remove(socket);
    socket->deleteLater();
}

void RelayServer::onCleanupTimer() {
    QList<QTcpSocket*> stale;
    for (auto it = m_registeredPeers.begin(); it != m_registeredPeers.end(); ++it) {
        if (it.value()->state() == QAbstractSocket::UnconnectedState)
            stale.append(it.value());
    }
    for (QTcpSocket* s : stale) {
        QString id = m_peerIds.value(s);
        m_registeredPeers.remove(id);
        m_peerIds.remove(s);
        m_buffers.remove(s);
        s->deleteLater();
    }
}

void RelayServer::processLine(QTcpSocket* socket, const QString& line) {
    QStringList parts = line.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return;

    QString cmd = parts[0];

    if (cmd == "REGISTER" && parts.size() >= 2) {
        QString deviceId = parts[1];
        QString token = parts.size() >= 3 ? parts[2] : QString();
        if (deviceId.isEmpty()) { sendLine(socket, "ERROR INVALID_ID"); return; }

        // Token check (skip when no secret is configured).
        if (!m_secret.isEmpty() && token != m_secret) {
            sendLine(socket, "ERROR AUTH_FAILED");
            LOG_WARNING("RelayServer: auth failed for " + deviceId);
            return;
        }

        if (m_registeredPeers.contains(deviceId)) {
            QTcpSocket* old = m_registeredPeers[deviceId];
            if (old && old != socket) {
                sendLine(old, "KICKED");
                old->close();
            }
        }
        m_registeredPeers[deviceId] = socket;
        m_peerIds[socket] = deviceId;
        sendLine(socket, "REGISTERED");
        LOG_INFO("RelayServer: registered " + deviceId +
                 " from " + socket->peerAddress().toString());
        emit peerConnected(deviceId, socket->peerAddress().toString());

    } else if (cmd == "CONNECT" && parts.size() >= 2) {
        QString targetId = parts[1];
        QString sourceId = m_peerIds.value(socket);
        if (sourceId.isEmpty()) { sendLine(socket, "ERROR NOT_REGISTERED"); return; }

        QTcpSocket* target = m_registeredPeers.value(targetId);
        if (!target) { sendLine(socket, "ERROR PEER_NOT_FOUND " + targetId); return; }

        // Create a bridge: connect two new sockets to both peers
        // Peer A (the requester) gets its connection bridged to a new connection to Peer B
        // Simple approach: just forward raw data between existing connections
        m_bridges[socket] = target;
        m_bridges[target] = socket;
        m_bridgeBuffers[socket] = QByteArray();
        m_bridgeBuffers[target] = QByteArray();

        // Tell target to switch to bridge mode (raw forwarding)
        sendLine(target, "BRIDGE " + sourceId);

        // Enable raw forwarding: disconnect text handlers
        disconnect(target, &QTcpSocket::readyRead, this, &RelayServer::onClientData);
        connect(target, &QTcpSocket::readyRead, this, [this, target]() {
            QTcpSocket* other = m_bridges.value(target);
            if (other && other->state() == QAbstractSocket::ConnectedState) {
                QByteArray d = target->readAll();
                other->write(d);
                other->flush();
            }
        });

        sendLine(socket, "BRIDGED " + targetId);
        LOG_INFO("RelayServer: bridged " + sourceId + " <-> " + targetId);

    } else if (cmd == "PUNCH" && parts.size() >= 2) {
        QString targetId = parts[1];
        QString sourceId = m_peerIds.value(socket);
        if (sourceId.isEmpty()) { sendLine(socket, "ERROR NOT_REGISTERED"); return; }

        if (sourceId == targetId) { sendLine(socket, "ERROR SAME_PEER"); return; }

        QTcpSocket* target = m_registeredPeers.value(targetId);
        if (!target) { sendLine(socket, "ERROR PEER_NOT_FOUND " + targetId); return; }

        QHostAddress srcIp = m_publicIps.value(sourceId, socket->peerAddress());
        QHostAddress dstIp = m_publicIps.value(targetId, target->peerAddress());

        // Exchange predicted P2P endpoints (public IP + dedicated P2P_PORT,
        // assuming the NAT preserves the source port).
        sendLine(target, QString("PEER_ADDR %1 %2 %3")
                 .arg(sourceId, dstIp.toString(), QString::number(P2P_PORT)));
        sendLine(socket, QString("PEER_ADDR %1 %2 %3")
                 .arg(targetId, srcIp.toString(), QString::number(P2P_PORT)));
        LOG_INFO("RelayServer: PUNCH " + sourceId + " <-> " + targetId);

    } else if (cmd == "LIST") {
        QStringList peers = m_registeredPeers.keys();
        sendLine(socket, "PEERS " + peers.join(','));
    } else if (cmd == "DEVICE_REGISTER" && parts.size() >= 2) {
        handleDeviceRegister(socket, parts);
    } else if (cmd == "DEVICE_LIST") {
        handleDeviceList(socket, parts);
    } else if (cmd == "DEVICE_UPDATE" && parts.size() >= 2) {
        handleDeviceUpdate(socket, parts);
    } else if (cmd == "DEVICE_REMOVE" && parts.size() >= 2) {
        handleDeviceRemove(socket, parts);
    } else if (cmd == "DEVICE_QUERY" && parts.size() >= 2) {
        handleDeviceQuery(socket, parts);
    } else if (cmd == "HEARTBEAT" && parts.size() >= 2) {
        QString deviceId = parts[1];
        if (m_deviceRegistry && m_deviceRegistry->containsDevice(deviceId)) {
            m_deviceRegistry->updateHeartbeat(deviceId);
            sendLine(socket, "HEARTBEAT_OK");
        } else {
            sendLine(socket, "ERROR DEVICE_NOT_FOUND");
        }
    } else {
        sendLine(socket, "ERROR UNKNOWN " + cmd);
    }
}

void RelayServer::onUdpReadyRead() {
    if (!m_udpSock) return;
    while (m_udpSock->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_udpSock->pendingDatagramSize()));
        QHostAddress senderAddr;
        quint16 senderPort = 0;
        m_udpSock->readDatagram(datagram.data(), datagram.size(), &senderAddr, &senderPort);

        QString line = QString::fromUtf8(datagram).trimmed();
        QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;

        if (parts[0] == "HELLO" && parts.size() >= 2) {
            QString deviceId = parts[1];
            m_publicIps[deviceId] = senderAddr;
            // Echo the reflexive address so the peer learns its own public IP.
            QByteArray resp = QString("YOURADDR %1 %2\n")
                              .arg(senderAddr.toString(), QString::number(senderPort))
                              .toUtf8();
            m_udpSock->writeDatagram(resp, senderAddr, senderPort);
        }
    }
}

void RelayServer::sendLine(QTcpSocket* socket, const QString& line) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
    socket->write((line + "\n").toUtf8());
    socket->flush();
}

void RelayServer::handleDeviceRegister(QTcpSocket* socket, const QStringList& parts) {
    if (!m_deviceRegistry) {
        sendLine(socket, "ERROR DEVICE_REGISTRY_NOT_AVAILABLE");
        return;
    }
    
    // Format: DEVICE_REGISTER <deviceId> [name] [ip] [port] [version] [group] [mac] [notes] [tags]
    if (parts.size() < 2) {
        sendLine(socket, "ERROR INVALID_PARAMETERS");
        return;
    }
    
    RegisteredDevice device;
    device.deviceId = parts[1];
    
    if (parts.size() > 2) device.deviceName = parts[2];
    if (parts.size() > 3) device.ipAddress = parts[3];
    if (parts.size() > 4) device.port = static_cast<uint16_t>(parts[4].toUShort());
    if (parts.size() > 5) device.version = parts[5];
    if (parts.size() > 6) device.group = parts[6];
    if (parts.size() > 7) device.mac = parts[7];
    if (parts.size() > 8) device.notes = parts[8];
    if (parts.size() > 9) device.tags = parts[9].split(',', Qt::SkipEmptyParts);
    
    // Use socket IP if not provided
    if (device.ipAddress.isEmpty()) {
        device.ipAddress = socket->peerAddress().toString();
    }
    
    m_deviceRegistry->registerDevice(device);
    sendLine(socket, "DEVICE_REGISTERED " + device.deviceId);
    LOG_INFO("RelayServer: device registered via protocol: " + device.deviceId);
}

void RelayServer::handleDeviceList(QTcpSocket* socket, const QStringList& parts) {
    if (!m_deviceRegistry) {
        sendLine(socket, "ERROR DEVICE_REGISTRY_NOT_AVAILABLE");
        return;
    }
    
    QJsonArray devicesArray;
    QList<RegisteredDevice> devices;
    
    if (parts.size() > 1 && parts[1] == "online") {
        devices = m_deviceRegistry->onlineDevices();
    } else if (parts.size() > 2 && parts[1] == "group") {
        devices = m_deviceRegistry->devicesByGroup(parts[2]);
    } else if (parts.size() > 2 && parts[1] == "search") {
        devices = m_deviceRegistry->searchDevices(parts[2]);
    } else {
        devices = m_deviceRegistry->allDevices();
    }
    
    for (const RegisteredDevice& device : devices) {
        devicesArray.append(device.toJson());
    }
    
    QJsonObject response;
    response["devices"] = devicesArray;
    response["count"] = devices.size();
    response["total"] = m_deviceRegistry->deviceCount();
    response["online"] = m_deviceRegistry->onlineDeviceCount();
    
    QJsonDocument doc(response);
    sendLine(socket, "DEVICE_LIST " + doc.toJson(QJsonDocument::Compact).toBase64());
}

void RelayServer::handleDeviceUpdate(QTcpSocket* socket, const QStringList& parts) {
    if (!m_deviceRegistry) {
        sendLine(socket, "ERROR DEVICE_REGISTRY_NOT_AVAILABLE");
        return;
    }
    
    // Format: DEVICE_UPDATE <deviceId> <jsonBase64>
    if (parts.size() < 3) {
        sendLine(socket, "ERROR INVALID_PARAMETERS");
        return;
    }
    
    QString deviceId = parts[1];
    QByteArray jsonData = QByteArray::fromBase64(parts[2].toUtf8());
    
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.isNull() || !doc.isObject()) {
        sendLine(socket, "ERROR INVALID_JSON");
        return;
    }
    
    QJsonObject obj = doc.object();
    RegisteredDevice device = m_deviceRegistry->device(deviceId);
    
    if (device.deviceId.isEmpty()) {
        sendLine(socket, "ERROR DEVICE_NOT_FOUND");
        return;
    }
    
    // Update fields from JSON
    if (obj.contains("deviceName")) device.deviceName = obj["deviceName"].toString();
    if (obj.contains("ipAddress")) device.ipAddress = obj["ipAddress"].toString();
    if (obj.contains("port")) device.port = static_cast<uint16_t>(obj["port"].toInt());
    if (obj.contains("version")) device.version = obj["version"].toString();
    if (obj.contains("group")) device.group = obj["group"].toString();
    if (obj.contains("mac")) device.mac = obj["mac"].toString();
    if (obj.contains("notes")) device.notes = obj["notes"].toString();
    if (obj.contains("tags")) {
        device.tags.clear();
        QJsonArray tagsArray = obj["tags"].toArray();
        for (const QJsonValue& v : tagsArray) {
            device.tags.append(v.toString());
        }
    }
    
    m_deviceRegistry->updateDevice(device);
    sendLine(socket, "DEVICE_UPDATED " + deviceId);
    LOG_INFO("RelayServer: device updated via protocol: " + deviceId);
}

void RelayServer::handleDeviceRemove(QTcpSocket* socket, const QStringList& parts) {
    if (!m_deviceRegistry) {
        sendLine(socket, "ERROR DEVICE_REGISTRY_NOT_AVAILABLE");
        return;
    }
    
    QString deviceId = parts[1];
    if (!m_deviceRegistry->containsDevice(deviceId)) {
        sendLine(socket, "ERROR DEVICE_NOT_FOUND");
        return;
    }
    
    m_deviceRegistry->removeDevice(deviceId);
    sendLine(socket, "DEVICE_REMOVED " + deviceId);
    LOG_INFO("RelayServer: device removed via protocol: " + deviceId);
}

void RelayServer::handleDeviceQuery(QTcpSocket* socket, const QStringList& parts) {
    if (!m_deviceRegistry) {
        sendLine(socket, "ERROR DEVICE_REGISTRY_NOT_AVAILABLE");
        return;
    }
    
    QString deviceId = parts[1];
    if (!m_deviceRegistry->containsDevice(deviceId)) {
        sendLine(socket, "ERROR DEVICE_NOT_FOUND");
        return;
    }
    
    RegisteredDevice device = m_deviceRegistry->device(deviceId);
    QJsonObject response = device.toJson();
    response["online"] = device.online;
    response["lastSeen"] = device.lastSeen.toSecsSinceEpoch();
    response["lastConnected"] = device.lastConnected.toSecsSinceEpoch();
    
    QJsonDocument doc(response);
    sendLine(socket, "DEVICE_INFO " + doc.toJson(QJsonDocument::Compact).toBase64());
}

} // namespace xrk
