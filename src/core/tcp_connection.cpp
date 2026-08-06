#include "tcp_connection.h"
#include "message_codec.h"
#include "logger.h"
#include <QDataStream>
#include <QRandomGenerator>

namespace xrk {

TcpConnection::TcpConnection(QTcpSocket* socket, QObject* parent)
    : QObject(parent), m_socket(socket) {
    m_currentInterval = m_reconnectInterval;

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &TcpConnection::onReconnectTimer);

    // Wire the socket's receive path. When a pre-connected socket is handed in
    // (e.g. NetworkManager::connectTo), the receiving side is otherwise never
    // connected to processBuffer, so the peer's messages (AUTH_RESP, SCREEN_FRAME,
    // ...) are never delivered -> the remote desktop stays black while outbound
    // input still works. (reconnect() wires its own freshly-created socket too.)
    if (m_socket) {
        setupSocket(m_socket);
    }

    setState(xrk::ConnectionState::Disconnected);
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
    m_bytesSent += data.size();
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
    m_reconnectInterval = qBound(100, ms, 60000);
    m_currentInterval = m_reconnectInterval;
}

void TcpConnection::setMaxReconnectAttempts(int maxAttempts) {
    m_maxReconnectAttempts = qBound(1, maxAttempts, 100);
}

void TcpConnection::setReconnectConfig(int minInterval, int maxInterval, double backoffMultiplier, int jitterPercent) {
    m_minReconnectInterval = qBound(100, minInterval, 60000);
    m_maxReconnectInterval = qBound(m_minReconnectInterval, maxInterval, 300000);
    m_backoffMultiplier = qBound(1.0, backoffMultiplier, 5.0);
    m_jitterPercent = qBound(0, jitterPercent, 50);
}

void TcpConnection::reconnect() {
    if (m_reconnectTimer->isActive()) {
        return;
    }

    m_manualDisconnect = false;
    m_currentReconnectAttempt = 0;
    m_currentInterval = m_reconnectInterval;
    m_reconnectTimer->start(m_reconnectInterval);
    LOG_INFO("Reconnect started for " + m_deviceId);
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

xrk::ConnectionState TcpConnection::state() const {
    return m_state;
}

xrk::ConnectionHistoryEntry TcpConnection::lastHistoryEntry() const {
    if (m_history.isEmpty()) {
        return xrk::ConnectionHistoryEntry();
    }
    return m_history.last();
}

QList<xrk::ConnectionHistoryEntry> TcpConnection::history() const {
    return m_history;
}

void TcpConnection::clearHistory() {
    m_history.clear();
    emit connectionHistoryUpdated();
}

void TcpConnection::onSocketReadyRead() {
    processBuffer();
}

void TcpConnection::onSocketDisconnected() {
    emit disconnected(m_deviceId);

    if (m_state == xrk::ConnectionState::Connected) {
        recordHistory(xrk::ConnectionState::Disconnected);
    }

    if (m_reconnectEnabled && !m_manualDisconnect) {
        if (m_currentReconnectAttempt < m_maxReconnectAttempts) {
            if (!m_reconnectTimer->isActive()) {
                m_currentInterval = m_reconnectInterval;
                m_reconnectTimer->start(m_reconnectInterval);
            }
        } else {
            setState(xrk::ConnectionState::Failed);
            emit reconnectFailed();
        }
    } else {
        setState(xrk::ConnectionState::Disconnected);
    }
}

void TcpConnection::onSocketError(QAbstractSocket::SocketError error) {
    categorizeError(error);
    
    QString errorStr = m_socket->errorString();
    emit errorOccurred(errorStr);

    if (error == QAbstractSocket::ConnectionRefusedError ||
        error == QAbstractSocket::RemoteHostClosedError ||
        error == QAbstractSocket::NetworkError) {

        if (m_reconnectEnabled && !m_manualDisconnect) {
            if (!m_reconnectTimer->isActive()) {
                m_currentInterval = m_reconnectInterval;
                m_reconnectTimer->start(m_reconnectInterval);
            }
        }
    }
}

void TcpConnection::onSocketBytesWritten(qint64 bytes) {
    m_bytesSent += bytes;
    emit bytesWritten(bytes);
}

void TcpConnection::onReconnectTimer() {
    if (m_currentReconnectAttempt >= m_maxReconnectAttempts) {
        m_reconnectTimer->stop();
        setState(xrk::ConnectionState::Failed);
        emit reconnectFailed();
        recordHistory(xrk::ConnectionState::Failed, xrk::ConnectionError::Timeout, 
                      QString("Failed after %1 attempts").arg(m_maxReconnectAttempts));
        LOG_ERROR("Reconnect failed for " + m_deviceId + " after " + QString::number(m_maxReconnectAttempts) + " attempts");
        return;
    }

    m_currentReconnectAttempt++;
    int nextInterval = calculateNextInterval();
    
    setState(xrk::ConnectionState::Reconnecting);
    emit reconnecting(m_currentReconnectAttempt, m_maxReconnectAttempts);
    LOG_INFO("Reconnect attempt " + QString::number(m_currentReconnectAttempt) + "/" + QString::number(m_maxReconnectAttempts) + 
             " for " + m_deviceId + " (next in " + QString::number(nextInterval) + "ms)");

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
    m_bytesReceived += m_buffer.size();

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

void TcpConnection::setState(xrk::ConnectionState newState) {
    if (m_state == newState) return;
    
    m_state = newState;
    emit stateChanged(newState);

    switch (newState) {
        case xrk::ConnectionState::Connecting:
        case xrk::ConnectionState::Connected:
            m_connectionStartTime = QDateTime::currentDateTime();
            m_bytesSent = 0;
            m_bytesReceived = 0;
            break;
        case xrk::ConnectionState::Disconnected:
        case xrk::ConnectionState::Failed:
            if (m_connectionStartTime.isValid()) {
                qint64 duration = m_connectionStartTime.msecsTo(QDateTime::currentDateTime());
                recordHistory(xrk::ConnectionState::Disconnected, xrk::ConnectionError::None, "", duration, m_bytesSent, m_bytesReceived);
            }
            break;
        default:
            break;
    }
}

void TcpConnection::recordHistory(xrk::ConnectionState state, xrk::ConnectionError error, 
                                  const QString& errorMsg, qint64 duration, qint64 bytesSent, qint64 bytesReceived) {
    xrk::ConnectionHistoryEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.hostAddress = m_hostAddress;
    entry.hostPort = m_hostPort;
    entry.state = state;
    entry.error = error;
    entry.errorMessage = errorMsg;
    entry.duration = duration > 0 ? duration : m_connectionStartTime.msecsTo(QDateTime::currentDateTime());
    entry.bytesSent = bytesSent > 0 ? bytesSent : m_bytesSent;
    entry.bytesReceived = bytesReceived > 0 ? bytesReceived : m_bytesReceived;

    m_history.prepend(entry);
    while (m_history.size() > m_maxHistorySize) {
        m_history.removeLast();
    }
    emit connectionHistoryUpdated();
}

int TcpConnection::calculateNextInterval() {
    // Exponential backoff with jitter
    double nextInterval = m_currentInterval * m_backoffMultiplier;
    
    // Cap at max interval
    nextInterval = qMin(nextInterval, static_cast<double>(m_maxReconnectInterval));
    
    // Add jitter
    int jitterRange = static_cast<int>(nextInterval * m_jitterPercent / 100.0);
    if (jitterRange > 0) {
        int jitter = QRandomGenerator::global()->bounded(-jitterRange, jitterRange + 1);
        nextInterval += jitter;
    }
    
    // Cap at max and min
    nextInterval = qBound(static_cast<double>(m_minReconnectInterval), nextInterval, 
                          static_cast<double>(m_maxReconnectInterval));
    
    m_currentInterval = static_cast<int>(nextInterval);
    return m_currentInterval;
}

void TcpConnection::categorizeError(QAbstractSocket::SocketError socketError) {
    xrk::ConnectionError error = xrk::ConnectionError::Unknown;
    
    switch (socketError) {
        case QAbstractSocket::ConnectionRefusedError:
            error = xrk::ConnectionError::ConnectionRefused;
            break;
        case QAbstractSocket::RemoteHostClosedError:
            error = xrk::ConnectionError::RemoteHostClosed;
            break;
        case QAbstractSocket::NetworkError:
        case QAbstractSocket::HostNotFoundError:
        case QAbstractSocket::SocketAccessError:
            error = xrk::ConnectionError::NetworkError;
            break;
        case QAbstractSocket::SocketTimeoutError:
            error = xrk::ConnectionError::Timeout;
            break;
        default:
error = xrk::ConnectionError::Unknown;
            break;
        }
        
        recordHistory(xrk::ConnectionState::Failed, error, m_socket->errorString());
    }

} // namespace xrk