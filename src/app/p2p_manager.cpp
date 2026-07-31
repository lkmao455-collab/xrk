#include "p2p_manager.h"
#include "core/logger.h"

namespace xrk {

P2PManager::P2PManager(QObject* parent) : QObject(parent) {
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, &P2PManager::onTimeout);

    m_retryTimer = new QTimer(this);
    m_retryTimer->setSingleShot(true);
    connect(m_retryTimer, &QTimer::timeout, this, &P2PManager::onRetry);
}

P2PManager::~P2PManager() {
    cancel();
}

void P2PManager::beginPunch(const QHostAddress& peerAddress, quint16 peerPort) {
    if (m_punching) return;

    m_peerAddress = peerAddress;
    m_peerPort = peerPort;
    m_attempts = 0;
    m_punching = true;

    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &P2PManager::onConnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &P2PManager::onError);

    // Bind to the dedicated P2P port so the NAT mapping uses a predictable
    // public port (assumed preserved by the relay's PEER_ADDR prediction).
    // Fall back to an ephemeral port if the fixed port is unavailable.
    if (!m_socket->bind(QHostAddress::Any, P2P_PORT)) {
        m_socket->bind();
    }

    LOG_INFO("P2PManager: punching " + peerAddress.toString() + ":" + QString::number(peerPort));
    m_socket->connectToHost(peerAddress, peerPort);
    m_timeoutTimer->start(PUNCH_TIMEOUT_MS);
}

void P2PManager::cancel() {
    m_punching = false;
    m_timeoutTimer->stop();
    m_retryTimer->stop();
    cleanupSocket();
}

void P2PManager::onConnected() {
    if (!m_punching || !m_socket) return;
    m_punching = false;
    m_timeoutTimer->stop();
    m_retryTimer->stop();

    QTcpSocket* established = m_socket;
    m_socket = nullptr; // ownership transferred to receiver
    LOG_INFO("P2PManager: direct connection established");
    emit directConnectionEstablished(established);
}

void P2PManager::onError(QAbstractSocket::SocketError error) {
    Q_UNUSED(error);
    if (!m_punching) return;

    // Transient errors (e.g. peer not ready yet during simultaneous open) are
    // retried until the overall timeout elapses.
    if (m_attempts < MAX_ATTEMPTS) {
        m_attempts++;
        LOG_DEBUG("P2PManager: punch error, retry " + QString::number(m_attempts));
        m_retryTimer->start(PUNCH_RETRY_MS);
    }
    // Otherwise let onTimeout emit punchFailed().
}

void P2PManager::onRetry() {
    if (!m_punching || !m_socket) return;
    m_socket->connectToHost(m_peerAddress, m_peerPort);
}

void P2PManager::onTimeout() {
    if (!m_punching) return;
    m_punching = false;
    LOG_WARNING("P2PManager: punch timed out, falling back");
    cleanupSocket();
    emit punchFailed();
}

void P2PManager::cleanupSocket() {
    if (m_socket) {
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

} // namespace xrk
