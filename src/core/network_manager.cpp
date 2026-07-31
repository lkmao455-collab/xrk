#include "network_manager.h"
#include "tcp_connection.h"
#include "protocol_manager.h"
#include "message_codec.h"
#include "logger.h"
#include <QHostInfo>
#include <QNetworkInterface>

namespace xrk {

NetworkManager::NetworkManager(QObject* parent) : QObject(parent) {
}

NetworkManager::~NetworkManager() {
    shutdown();
}

bool NetworkManager::initialize(uint16_t port, bool startTcpServer) {
    if (m_running) {
        return true;
    }

    m_port = port;

    if (startTcpServer) {
        m_tcpServer = new QTcpServer(this);
        connect(m_tcpServer, &QTcpServer::newConnection, this, &NetworkManager::onNewConnection);

        if (!m_tcpServer->listen(QHostAddress::Any, port)) {
            LOG_ERROR("Failed to start TCP server: " + m_tcpServer->errorString());
            return false;
        }
    }

    setupUdpReceiver();

    m_running = true;
    LOG_INFO("NetworkManager initialized on port " + QString::number(port) +
             (startTcpServer ? " (tcp+udp)" : " (udp only)"));
    return true;
}

void NetworkManager::shutdown() {
    if (!m_running) {
        return;
    }
    
    m_running = false;
    
    disconnectAll();
    
    if (m_tcpServer) {
        m_tcpServer->close();
        m_tcpServer->deleteLater();
        m_tcpServer = nullptr;
    }
    
    if (m_udpSocket) {
        m_udpSocket->close();
        m_udpSocket->deleteLater();
        m_udpSocket = nullptr;
    }
    
    LOG_INFO("NetworkManager shutdown");
}

void NetworkManager::broadcastDiscovery() {
    if (!m_udpSocket) {
        return;
    }
    
    DeviceInfo info;
    info.deviceId = QString();
    info.deviceName = QHostInfo::localHostName();
    info.ipAddress = QHostAddress(QHostAddress::LocalHost).toString();
    info.port = m_port;
    info.version = "1.0.0";
    info.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    QByteArray payload = ProtocolManager::encodeDeviceInfo(info);
    QByteArray message = MessageCodec::encode(MessageType::DEVICE_DISCOVER_REQ, payload);
    
    m_udpSocket->writeDatagram(message, QHostAddress::Broadcast, UDP_BROADCAST_PORT);
    LOG_DEBUG("Broadcast discovery sent");
}

std::shared_ptr<TcpConnection> NetworkManager::connectTo(const QString& ip, uint16_t port) {
    QTcpSocket* socket = new QTcpSocket(this);
    
    socket->connectToHost(ip, port);
    
    if (!socket->waitForConnected(5000)) {
        LOG_ERROR("Failed to connect to " + ip + ":" + QString::number(port));
        socket->deleteLater();
        return nullptr;
    }
    
    // Low latency for real-time screen streaming
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    
    auto connection = std::make_shared<TcpConnection>(socket, this);
    
    connect(connection.get(), &TcpConnection::disconnected, this, &NetworkManager::onConnectionDisconnected);
    connect(connection.get(), &TcpConnection::readyRead, this, &NetworkManager::onTcpMessageReceived);
    
    QString deviceId = socket->peerAddress().toString();
    connection->setDeviceId(deviceId);
    m_connections[deviceId] = connection;
    
    emit connectionEstablished(deviceId);
    LOG_INFO("Connected to " + ip + ":" + QString::number(port));
    
    return connection;
}

void NetworkManager::disconnectAll() {
    for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
        it.value()->disconnect();
    }
    m_connections.clear();
}

bool NetworkManager::isRunning() const {
    return m_running;
}

uint16_t NetworkManager::port() const {
    return m_port;
}

void NetworkManager::onNewConnection() {
    while (m_tcpServer->hasPendingConnections()) {
        QTcpSocket* socket = m_tcpServer->nextPendingConnection();
        
        // Low latency for real-time screen streaming
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        
        auto connection = std::make_shared<TcpConnection>(socket, this);
        
        connect(connection.get(), &TcpConnection::disconnected, this, &NetworkManager::onConnectionDisconnected);
        connect(connection.get(), &TcpConnection::readyRead, this, &NetworkManager::onTcpMessageReceived);
        
        QString deviceId = socket->peerAddress().toString();
        connection->setDeviceId(deviceId);
        m_connections[deviceId] = connection;
        
        emit connectionEstablished(deviceId);
        LOG_INFO("New connection from " + deviceId);
    }
}

void NetworkManager::onTcpMessageReceived(const QByteArray& data) {
    TcpConnection* conn = qobject_cast<TcpConnection*>(sender());
    if (!conn) {
        return;
    }
    
    MessageType type;
    QByteArray payload;
    QString sessionId;
    
    if (!ProtocolManager::decode(data, type, payload, sessionId)) {
        LOG_WARNING("Failed to decode message from " + conn->deviceId());
        return;
    }
    
    if (type == MessageType::DEVICE_DISCOVER_REQ) {
        DeviceInfo info = ProtocolManager::decodeDeviceInfo(payload);
        emit discoveryReceived(info);
    }
    
    emit messageReceived(conn->deviceId(), data);
}

void NetworkManager::onUdpMessageReceived() {
    while (m_udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_udpSocket->pendingDatagramSize());
        
        QHostAddress sender;
        quint16 senderPort;
        
        m_udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        
        processDiscoveryMessage(datagram);
    }
}

void NetworkManager::onConnectionDisconnected(const QString& deviceId) {
    m_connections.remove(deviceId);
    emit connectionLost(deviceId);
    LOG_INFO("Connection lost: " + deviceId);
}

void NetworkManager::setupUdpReceiver() {
    m_udpSocket = new QUdpSocket(this);
    
    if (!m_udpSocket->bind(QHostAddress::Any, UDP_BROADCAST_PORT)) {
        LOG_WARNING("Failed to bind UDP socket: " + m_udpSocket->errorString());
    }
    
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &NetworkManager::onUdpMessageReceived);
}

void NetworkManager::processDiscoveryMessage(const QByteArray& data) {
    MessageType type;
    QByteArray payload;
    QString sessionId;
    
    if (!ProtocolManager::decode(data, type, payload, sessionId)) {
        return;
    }
    
    if (type == MessageType::DEVICE_DISCOVER_REQ || type == MessageType::DEVICE_DISCOVER_RESP) {
        DeviceInfo info = ProtocolManager::decodeDeviceInfo(payload);
        emit discoveryReceived(info);
    }
}

} // namespace xrk
