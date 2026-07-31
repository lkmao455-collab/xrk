#include "nat_traversal.h"
#include "core/logger.h"

namespace xrk {

NatTraversal::NatTraversal(QObject* parent) : QObject(parent) {
    m_reconnectTimer = new QTimer(this);
    connect(m_reconnectTimer, &QTimer::timeout, this, &NatTraversal::onReconnectTimer);

    m_udpSock = new QUdpSocket(this);
    connect(m_udpSock, &QUdpSocket::readyRead, this, &NatTraversal::onUdpReadyRead);

    m_udpKeepaliveTimer = new QTimer(this);
    connect(m_udpKeepaliveTimer, &QTimer::timeout, this, &NatTraversal::onUdpKeepalive);
}

NatTraversal::~NatTraversal() {
    disconnectFromRelay();
}

void NatTraversal::setRelayServer(const QString& host, quint16 port) {
    m_relayHost = host;
    m_relayPort = port;
}

void NatTraversal::setDeviceId(const QString& deviceId) {
    m_deviceId = deviceId;
}

void NatTraversal::setRelayToken(const QString& token) {
    m_relayToken = token;
}

void NatTraversal::connectToRelay() {
    if (m_relayHost.isEmpty() || m_relayPort == 0) {
        LOG_WARNING("NatTraversal: relay not configured");
        return;
    }

    if (m_socket) {
        m_socket->abort();
        m_socket->deleteLater();
    }

    m_state = RelayState::Connecting;
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &NatTraversal::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &NatTraversal::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &NatTraversal::onError);
    connect(m_socket, &QTcpSocket::readyRead, this, &NatTraversal::onData);

    m_socket->connectToHost(m_relayHost, m_relayPort);
    LOG_INFO("NatTraversal: connecting to relay " + m_relayHost + ":" + QString::number(m_relayPort));
}

void NatTraversal::disconnectFromRelay() {
    m_reconnectTimer->stop();
    m_udpKeepaliveTimer->stop();
    m_state = RelayState::Disconnected;
    if (m_socket) {
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_buffer.clear();
    LOG_INFO("NatTraversal: disconnected from relay");
    emit relayDisconnected();
}

void NatTraversal::requestBridge(const QString& targetDeviceId) {
    if (m_state != RelayState::Registered) {
        LOG_WARNING("NatTraversal: not registered, cannot bridge");
        return;
    }
    sendLine("CONNECT " + targetDeviceId);
    LOG_INFO("NatTraversal: requesting bridge to " + targetDeviceId);
}

void NatTraversal::requestPunch(const QString& targetDeviceId) {
    if (m_state != RelayState::Registered) {
        LOG_WARNING("NatTraversal: not registered, cannot punch");
        return;
    }
    sendLine("PUNCH " + targetDeviceId);
    LOG_INFO("NatTraversal: requesting PUNCH to " + targetDeviceId);
}

void NatTraversal::sendUdpHello() {
    if (m_relayHost.isEmpty() || m_relayPort == 0 || m_deviceId.isEmpty()) return;
    if (m_udpSock->state() != QAbstractSocket::BoundState &&
        m_udpSock->state() != QAbstractSocket::UnconnectedState) {
        // socket is fine; just send
    }
    QByteArray data = QString("HELLO %1\n").arg(m_deviceId).toUtf8();
    m_udpSock->writeDatagram(data, QHostAddress(m_relayHost), m_relayPort);
}

bool NatTraversal::isRelayConnected() const {
    return m_state == RelayState::Registered || m_state == RelayState::Bridged;
}

RelayState NatTraversal::relayState() const { return m_state; }
QString NatTraversal::relayServerHost() const { return m_relayHost; }
quint16 NatTraversal::relayServerPort() const { return m_relayPort; }

void NatTraversal::onConnected() {
    LOG_INFO("NatTraversal: connected to relay");
    if (!m_deviceId.isEmpty()) {
        if (m_relayToken.isEmpty()) {
            sendLine("REGISTER " + m_deviceId);
        } else {
            sendLine("REGISTER " + m_deviceId + " " + m_relayToken);
        }
        // Start NAT keep-alive + public-endpoint discovery via UDP.
        sendUdpHello();
        if (!m_udpKeepaliveTimer->isActive()) {
            m_udpKeepaliveTimer->start(UDP_KEEPALIVE_MS);
        }
    }
}

void NatTraversal::onDisconnected() {
    if (m_state == RelayState::Bridged) {
        LOG_INFO("NatTraversal: bridge closed");
        m_state = RelayState::Disconnected;
        emit relayDisconnected();
        return;
    }

    LOG_WARNING("NatTraversal: relay disconnected");
    m_state = RelayState::Disconnected;
    emit relayDisconnected();

    if (!m_reconnectTimer->isActive()) {
        m_reconnectTimer->start(RECONNECT_INTERVAL_MS);
    }
}

void NatTraversal::onError(QAbstractSocket::SocketError error) {
    Q_UNUSED(error);
    QString err = m_socket ? m_socket->errorString() : "unknown";
    LOG_ERROR("NatTraversal: error: " + err);
    m_state = RelayState::Error;
    emit relayError(err);
}

void NatTraversal::onData() {
    if (!m_socket) return;

    m_buffer.append(m_socket->readAll());
    while (true) {
        int pos = m_buffer.indexOf('\n');
        if (pos < 0) break;
        QString line = QString::fromUtf8(m_buffer.left(pos)).trimmed();
        m_buffer.remove(0, pos + 1);
        if (!line.isEmpty()) processLine(line);
    }
}

void NatTraversal::onReconnectTimer() {
    LOG_INFO("NatTraversal: reconnecting...");
    connectToRelay();
}

void NatTraversal::onUdpReadyRead() {
    while (m_udpSock->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_udpSock->pendingDatagramSize()));
        m_udpSock->readDatagram(datagram.data(), datagram.size());
        QString line = QString::fromUtf8(datagram).trimmed();
        QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;

        if (parts[0] == "YOURADDR" && parts.size() >= 2) {
            QHostAddress addr(parts[1]);
            if (!addr.isNull()) {
                m_selfAddress = addr;
                emit selfAddressUpdated(addr);
                LOG_DEBUG("NatTraversal: self public address " + addr.toString());
            }
        }
    }
}

void NatTraversal::onUdpKeepalive() {
    sendUdpHello();
}

void NatTraversal::processLine(const QString& line) {
    QStringList parts = line.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return;

    QString cmd = parts[0];

    if (cmd == "REGISTERED") {
        m_state = RelayState::Registered;
        LOG_INFO("NatTraversal: registered as " + m_deviceId);
        emit relayConnected();
    } else if ((cmd == "BRIDGED" || cmd == "BRIDGE") && parts.size() >= 2) {
        m_bridgedPeer = parts[1];
        LOG_INFO("NatTraversal: bridge established to " + m_bridgedPeer);
        // Detach the relay socket and hand it to the receiver. Any XRK
        // protocol bytes that arrived in the same segment (after this line)
        // are passed as initialData so nothing is lost.
        QByteArray consumed = (line + "\n").toUtf8();
        QByteArray initialData;
        if (m_buffer.startsWith(consumed)) {
            m_buffer.remove(0, consumed.size());
        }
        initialData = m_buffer;
        m_buffer.clear();

        QTcpSocket* sock = m_socket;
        sock->disconnect(this);
        m_socket = nullptr;
        m_state = RelayState::Disconnected; // relay control channel consumed
        emit bridgeSocketReady(sock, initialData);
    } else if (cmd == "BRIDGE_CLOSED") {
        LOG_INFO("NatTraversal: bridge closed by peer");
        m_state = RelayState::Disconnected;
        emit relayDisconnected();
    } else if (cmd == "KICKED") {
        LOG_WARNING("NatTraversal: kicked from relay");
        m_state = RelayState::Disconnected;
        emit relayDisconnected();
        if (m_socket) {
            m_socket->close();
        }
    } else if (cmd == "ERROR" && parts.size() >= 2) {
        QString msg = parts.mid(1).join(' ');
        LOG_ERROR("NatTraversal: relay error: " + msg);
        if (msg.startsWith("AUTH_FAILED")) {
            m_state = RelayState::Error;
        }
        emit relayError(msg);
    } else if (cmd == "PEER_ADDR" && parts.size() >= 4) {
        QString peerId = parts[1];
        QHostAddress addr(parts[2]);
        quint16 port = static_cast<quint16>(parts[3].toUInt());
        if (!addr.isNull() && port != 0) {
            LOG_INFO("NatTraversal: peer " + peerId + " addr " + addr.toString() + ":" + QString::number(port));
            emit peerAddressReceived(peerId, addr, port);
        }
    } else if (cmd == "PEERS" && parts.size() >= 2) {
        LOG_DEBUG("NatTraversal: online peers: " + parts[1]);
    }
}

void NatTraversal::sendLine(const QString& line) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return;
    m_socket->write((line + "\n").toUtf8());
    m_socket->flush();
}

} // namespace xrk
