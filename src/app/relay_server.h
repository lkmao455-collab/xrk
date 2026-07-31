#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QHostAddress>
#include <QHash>
#include <QTimer>
#include <memory>

namespace xrk {

class RelayServer : public QObject {
    Q_OBJECT
public:
    explicit RelayServer(QObject* parent = nullptr);
    ~RelayServer();

    bool start(quint16 port);
    void stop();
    bool isRunning() const;
    quint16 port() const;
    QStringList connectedPeers() const;

    // Set the shared secret used to authenticate REGISTER. An empty secret
    // disables token checking (accepts any token) for LAN/local relays.
    void setSecret(const QString& secret);

signals:
    void serverStarted(quint16 port);
    void serverStopped();
    void peerConnected(const QString& deviceId, const QString& ip);
    void peerDisconnected(const QString& deviceId);

private slots:
    void onNewConnection();
    void onClientData();
    void onClientDisconnected();
    void onCleanupTimer();
    void onUdpReadyRead();

private:
    void processLine(QTcpSocket* socket, const QString& line);
    void sendLine(QTcpSocket* socket, const QString& line);

    QTcpServer* m_server = nullptr;
    QUdpSocket* m_udpSock = nullptr;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QHash<QString, QTcpSocket*> m_registeredPeers;
    QHash<QTcpSocket*, QString> m_peerIds;
    QHash<QTcpSocket*, QTcpSocket*> m_bridges;
    QTimer* m_cleanupTimer = nullptr;
    bool m_running = false;
    quint16 m_port = 0;
    QString m_secret;

    // Public (reflexive) IP learned from UDP HELLO, keyed by deviceId.
    QHash<QString, QHostAddress> m_publicIps;

    // Bridge forwarding state
    QHash<QTcpSocket*, QByteArray> m_bridgeBuffers;

    static constexpr int CLEANUP_INTERVAL_MS = 30000;
};

} // namespace xrk
