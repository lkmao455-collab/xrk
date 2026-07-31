#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <memory>
#include "types.h"

namespace xrk {

class TcpConnection : public QObject {
    Q_OBJECT
public:
    explicit TcpConnection(QTcpSocket* socket, QObject* parent = nullptr);
    ~TcpConnection();

    void send(const QByteArray& data);
    bool isConnected() const;
    QString deviceId() const;
    void setDeviceId(const QString& id);

    // Feed already-received bytes into the framing parser (used when taking
    // ownership of a socket that already has buffered data, e.g. relay bridge).
    void injectData(const QByteArray& data);

    void setReconnectEnabled(bool enabled);
    bool isReconnectEnabled() const;
    void setReconnectInterval(int ms);
    void setMaxReconnectAttempts(int maxAttempts);
    void reconnect();
    void disconnectFromHost();

    qint64 bytesWritten() const;
    qint64 bytesAvailable() const;

signals:
    void readyRead(const QByteArray& data);
    void disconnected(const QString& deviceId);
    void errorOccurred(const QString& errorString);
    void bytesWritten(qint64 bytes);
    void reconnecting(int attempt, int maxAttempts);
    void reconnected();
    void reconnectFailed();

private slots:
    void onSocketReadyRead();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onSocketBytesWritten(qint64 bytes);
    void onReconnectTimer();

private:
    void processBuffer();
    void setupSocket(QTcpSocket* socket);

    QTcpSocket* m_socket = nullptr;
    QString m_deviceId;
    QByteArray m_buffer;

    bool m_reconnectEnabled = false;
    int m_reconnectInterval = 3000;
    int m_maxReconnectAttempts = 5;
    int m_currentReconnectAttempt = 0;
    bool m_manualDisconnect = false;
    QTimer* m_reconnectTimer = nullptr;

    QString m_hostAddress;
    quint16 m_hostPort = 0;

    static constexpr int HEADER_SIZE = 24;
};

} // namespace xrk
