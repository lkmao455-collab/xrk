#include "relay_server.h"
#include "core/logger.h"
#include "core/types.h"
#include <QDateTime>

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

} // namespace xrk
