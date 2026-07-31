#include "tcp_connection.h"
#include "message_codec.h"
#include "logger.h"
#include <QDataStream>

namespace xrk {

TcpConnection::TcpConnection(QTcpSocket* socket, QObject* parent)
    : QObject(parent), m_socket(socket) {
    setupSocket(socket);

    m_reconnectTimer = new QTimer(this);
    connect(m_reconnectTimer, &QTimer::timeout, this, &TcpConnection::onReconnectTimer);
}

TcpConnection::~TcpConnection() {
    if (m_socket) {
        m_socket->close();
        m_socket->deleteLater();
    }
}

void TcpConnection::send(const QByteArray& data) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    m_socket->write(data);
    m_socket->flush();
}

bool TcpConnection::isConnected() const {
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

QString TcpConnection::deviceId() const {
    return m_deviceId;
}

void TcpConnection::setDeviceId(const QString& id) {
    m_deviceId = id;
}

void TcpConnection::injectData(const QByteArray& data) {
    if (data.isEmpty()) return;
    m_buffer.append(data);
    processBuffer();
}

void TcpConnection::setReconnectEnabled(bool enabled) {
    m_reconnectEnabled = enabled;
}

bool TcpConnection::isReconnectEnabled() const {
    return m_reconnectEnabled;
}

void TcpConnection::setReconnectInterval(int ms) {
    m_reconnectInterval = qBound(1000, ms, 30000);
}

void TcpConnection::setMaxReconnectAttempts(int maxAttempts) {
    m_maxReconnectAttempts = qBound(1, maxAttempts, 100);
}

void TcpConnection::reconnect() {
    if (m_reconnectTimer->isActive()) {
        return;
    }

    m_manualDisconnect = false;
    m_currentReconnectAttempt = 0;
    m_reconnectTimer->start(m_reconnectInterval);
    LOG_INFO("Reconnect started");
}

void TcpConnection::disconnectFromHost() {
    m_manualDisconnect = true;
    m_reconnectTimer->stop();
    m_currentReconnectAttempt = 0;

    if (m_socket) {
        m_socket->disconnectFromHost();
    }
}

qint64 TcpConnection::bytesWritten() const {
    return m_socket ? m_socket->bytesToWrite() : 0;
}

qint64 TcpConnection::bytesAvailable() const {
    return m_socket ? m_socket->bytesAvailable() : 0;
}

void TcpConnection::onSocketReadyRead() {
    processBuffer();
}

void TcpConnection::onSocketDisconnected() {
    emit disconnected(m_deviceId);

    if (m_reconnectEnabled && !m_manualDisconnect) {
        if (m_currentReconnectAttempt < m_maxReconnectAttempts) {
            if (!m_reconnectTimer->isActive()) {
                m_reconnectTimer->start(m_reconnectInterval);
            }
        } else {
            emit reconnectFailed();
        }
    }
}

void TcpConnection::onSocketError(QAbstractSocket::SocketError error) {
    if (error == QAbstractSocket::ConnectionRefusedError ||
        error == QAbstractSocket::RemoteHostClosedError ||
        error == QAbstractSocket::NetworkError) {

        emit errorOccurred(m_socket->errorString());

        if (m_reconnectEnabled && !m_manualDisconnect) {
            if (!m_reconnectTimer->isActive()) {
                m_reconnectTimer->start(m_reconnectInterval);
            }
        }
    } else {
        emit errorOccurred(m_socket->errorString());
    }
}

void TcpConnection::onSocketBytesWritten(qint64 bytes) {
    emit bytesWritten(bytes);
}

void TcpConnection::onReconnectTimer() {
    if (m_currentReconnectAttempt >= m_maxReconnectAttempts) {
        m_reconnectTimer->stop();
        emit reconnectFailed();
        LOG_ERROR("Reconnect failed after " + QString::number(m_maxReconnectAttempts) + " attempts");
        return;
    }

    m_currentReconnectAttempt++;
    emit reconnecting(m_currentReconnectAttempt, m_maxReconnectAttempts);
    LOG_INFO("Reconnect attempt " + QString::number(m_currentReconnectAttempt) + "/" + QString::number(m_maxReconnectAttempts));

    if (m_socket) {
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    QTcpSocket* newSocket = new QTcpSocket(this);
    setupSocket(newSocket);
    m_socket = newSocket;

    newSocket->connectToHost(m_hostAddress, m_hostPort);
}

void TcpConnection::setupSocket(QTcpSocket* socket) {
    if (!socket) return;

    // Disable Nagle's algorithm for lowest latency on real-time screen streaming.
    // Without this, small frames are buffered for up to 40ms before being sent.
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    connect(socket, &QTcpSocket::readyRead, this, &TcpConnection::onSocketReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &TcpConnection::onSocketDisconnected);
    connect(socket, &QTcpSocket::errorOccurred, this, &TcpConnection::onSocketError);
    connect(socket, &QTcpSocket::bytesWritten, this, &TcpConnection::onSocketBytesWritten);

    if (m_hostAddress.isEmpty()) {
        m_hostAddress = socket->peerAddress().toString();
        m_hostPort = socket->peerPort();
    }
}

void TcpConnection::processBuffer() {
    if (!m_socket) return;

    m_buffer.append(m_socket->readAll());

    while (static_cast<size_t>(m_buffer.size()) >= MessageCodec::MIN_MESSAGE_SIZE) {
        MessageHeader header;
        uint32_t sessionIdLen = 0;
        if (!MessageCodec::parseHeader(m_buffer, header, sessionIdLen)) {
            break;
        }

        size_t totalSize = MessageCodec::HEADER_FIXED_SIZE + 4 + sessionIdLen + header.length + MessageCodec::CHECKSUM_SIZE;
        if (static_cast<size_t>(m_buffer.size()) < totalSize) {
            break;
        }

        QByteArray messageData = m_buffer.left(totalSize);
        m_buffer.remove(0, totalSize);

        if (!MessageCodec::verifyChecksum(messageData)) {
            LOG_WARNING("Invalid checksum, discarding message");
            continue;
        }

        emit readyRead(messageData);
    }
}

} // namespace xrk
