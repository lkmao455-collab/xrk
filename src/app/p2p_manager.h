#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QHostAddress>
#include "core/types.h"

namespace xrk {

// Performs a TCP hole-punch (simultaneous open) to a peer whose predicted
// public endpoint is learned via the relay. On success the established
// QTcpSocket is handed to the receiver (ownership transferred). On failure or
// timeout, punchFailed() is emitted so the caller can fall back to relay.
class P2PManager : public QObject {
    Q_OBJECT
public:
    explicit P2PManager(QObject* parent = nullptr);
    ~P2PManager();

    // Begin a simultaneous-open connect to the peer. The peer is expected to
    // call beginPunch() to us at roughly the same time.
    void beginPunch(const QHostAddress& peerAddress, quint16 peerPort);
    void cancel();
    bool isPunching() const { return m_punching; }

signals:
    // Emitted with the established socket. Ownership is transferred to the
    // receiver; P2PManager no longer references it after emission.
    void directConnectionEstablished(QTcpSocket* socket);
    void punchFailed();

private slots:
    void onConnected();
    void onError(QAbstractSocket::SocketError error);
    void onTimeout();
    void onRetry();

private:
    void cleanupSocket();

    QTcpSocket* m_socket = nullptr;
    QTimer* m_timeoutTimer = nullptr;
    QTimer* m_retryTimer = nullptr;
    QHostAddress m_peerAddress;
    quint16 m_peerPort = 0;
    int m_attempts = 0;
    bool m_punching = false;

    static constexpr int PUNCH_TIMEOUT_MS = 4000;
    static constexpr int PUNCH_RETRY_MS = 500;
    static constexpr int MAX_ATTEMPTS = 8;
};

} // namespace xrk
