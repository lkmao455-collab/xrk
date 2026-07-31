#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QHostAddress>
#include <QTimer>
#include <memory>
#include "core/types.h"

namespace xrk {

enum class RelayState {
    Disconnected,
    Connecting,
    Registered,
    Bridged,
    Error
};

class NatTraversal : public QObject {
    Q_OBJECT
public:
    explicit NatTraversal(QObject* parent = nullptr);
    ~NatTraversal();

    void setRelayServer(const QString& host, quint16 port);
    void setDeviceId(const QString& deviceId);
    void setRelayToken(const QString& token);

    void connectToRelay();
    void disconnectFromRelay();
    void requestBridge(const QString& targetDeviceId);

    // Request the relay to exchange P2P endpoints with the target peer.
    void requestPunch(const QString& targetDeviceId);

    // Send a UDP HELLO to the relay to refresh the NAT mapping and report
    // our public endpoint. Called periodically by the keep-alive timer.
    void sendUdpHello();

    bool isRelayConnected() const;
    RelayState relayState() const;
    QString relayServerHost() const;
    quint16 relayServerPort() const;

signals:
    void relayConnected();
    void relayDisconnected();
    void relayError(const QString& message);

    // Emitted once when the relay bridge is established. The relay socket is
    // detached and handed to the receiver (it owns the socket and must delete
    // it). initialData holds any XRK protocol bytes that arrived in the same
    // segment as the bridge command, so the receiver can feed them straight
    // into its protocol parser.
    void bridgeSocketReady(QTcpSocket* socket, const QByteArray& initialData);

    // Our own reflexive public address as learned from the relay (YOURADDR).
    void selfAddressUpdated(const QHostAddress& address);
    // A peer's predicted public P2P endpoint, exchanged via the relay.
    void peerAddressReceived(const QString& peerId, const QHostAddress& address, quint16 port);

private slots:
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError error);
    void onData();
    void onReconnectTimer();
    void onUdpReadyRead();
    void onUdpKeepalive();

private:
    void processLine(const QString& line);
    void sendLine(const QString& line);

    QTcpSocket* m_socket = nullptr;
    QUdpSocket* m_udpSock = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    QTimer* m_udpKeepaliveTimer = nullptr;
    QByteArray m_buffer;

    QString m_relayHost;
    quint16 m_relayPort = 0;
    QString m_deviceId;
    QString m_relayToken;

    RelayState m_state = RelayState::Disconnected;
    QString m_bridgedPeer;
    QHostAddress m_selfAddress;

    static constexpr int RECONNECT_INTERVAL_MS = 5000;
    static constexpr int UDP_KEEPALIVE_MS = 20000;
};

} // namespace xrk
