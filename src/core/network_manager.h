#pragma once

#include <QObject>
#include <QTcpServer>
#include <QUdpSocket>
#include <memory>
#include <QHash>
#include <QTimer>
#include "types.h"

namespace xrk {

class TcpConnection;

class NetworkManager : public QObject {
    Q_OBJECT
public:
    explicit NetworkManager(QObject* parent = nullptr);
    ~NetworkManager();

    bool initialize(uint16_t port, bool startTcpServer = true);
    void shutdown();
    
    void broadcastDiscovery();
    void setDiscoveryIdentity(const QString& name, quint16 controlPort, const QString& accessCode = QString());
    void broadcastPresence();
    void sendDiscoveryResponse(const QHostAddress& to, quint16 port);
    static QString localIpv4();
    std::shared_ptr<TcpConnection> connectTo(const QString& ip, uint16_t port);
    void disconnectAll();
    
    bool isRunning() const;
    uint16_t port() const;

signals:
    void connectionEstablished(const QString& deviceId);
    void connectionLost(const QString& deviceId);
    void messageReceived(const QString& deviceId, const QByteArray& data);
    void discoveryReceived(const DeviceInfo& info);

    QString deviceId() const;  // Get local device ID

private slots:
    void onNewConnection();
    void onTcpMessageReceived(const QByteArray& data);
    void onUdpMessageReceived();
    void onConnectionDisconnected(const QString& deviceId);
    void onDiscoveryBroadcastTimer();

private:
    void setupUdpReceiver();
    void processDiscoveryMessage(const QByteArray& data, const QHostAddress& from, quint16 fromPort);

    bool m_running = false;
    uint16_t m_port = 0;
    QString m_deviceId;
    QString m_discoName;
    QString m_discoCode;
    quint16 m_discoPort = 0;
    QTimer* m_discoveryBroadcastTimer = nullptr;
    class QTcpServer* m_tcpServer = nullptr;
    class QUdpSocket* m_udpSocket = nullptr;
    QHash<QString, std::shared_ptr<TcpConnection>> m_connections;
};

} // namespace xrk
