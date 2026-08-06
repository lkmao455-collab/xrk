#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <memory>
#include <QDateTime>
#include "types.h"

namespace xrk {

// Connection state enum for better tracking
enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Failed
};

// Error categories for better error handling
enum class ConnectionError {
    None,
    ConnectionRefused,
    RemoteHostClosed,
    NetworkError,
    Timeout,
    AuthFailed,
    ProtocolError,
    Unknown
};

// Connection history entry
struct ConnectionHistoryEntry {
    QDateTime timestamp;
    QString hostAddress;
    quint16 hostPort;
    ConnectionState state;
    ConnectionError error;
    QString errorMessage;
    qint64 duration = 0;  // connection duration in ms
    qint64 bytesSent = 0;
    qint64 bytesReceived = 0;
};

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
    void setReconnectConfig(int minInterval, int maxInterval, double backoffMultiplier, int jitterPercent);
    void reconnect();
    void disconnectFromHost();

    qint64 bytesWritten() const;
    qint64 bytesAvailable() const;
    
    ConnectionState state() const;
    ConnectionHistoryEntry lastHistoryEntry() const;
    QList<ConnectionHistoryEntry> history() const;
    void clearHistory();

signals:
    void readyRead(const QByteArray& data);
    void disconnected(const QString& deviceId);
    void errorOccurred(const QString& errorString);
    void bytesWritten(qint64 bytes);
    void reconnecting(int attempt, int maxAttempts);
    void reconnected();
    void reconnectFailed();
    void stateChanged(ConnectionState newState);
    void connectionHistoryUpdated();

private slots:
    void onSocketReadyRead();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onSocketBytesWritten(qint64 bytes);
    void onReconnectTimer();

private:
    void processBuffer();
    void setupSocket(QTcpSocket* socket);
    void setState(ConnectionState newState);
    void recordHistory(ConnectionState state, ConnectionError error = ConnectionError::None, 
                       const QString& errorMsg = "", qint64 duration = 0, qint64 bytesSent = 0, qint64 bytesReceived = 0);
    int calculateNextInterval();
    void categorizeError(QAbstractSocket::SocketError socketError);

    QTcpSocket* m_socket = nullptr;
    QString m_deviceId;
    QByteArray m_buffer;

    bool m_reconnectEnabled = false;
    int m_reconnectInterval = 3000;          // base interval
    int m_minReconnectInterval = 1000;       // minimum interval for exponential backoff
    int m_maxReconnectInterval = 30000;      // maximum interval
    double m_backoffMultiplier = 2.0;        // exponential backoff multiplier
    int m_jitterPercent = 20;                // jitter percentage (0-100)
    int m_maxReconnectAttempts = 5;
    int m_currentReconnectAttempt = 0;
    bool m_manualDisconnect = false;
    QTimer* m_reconnectTimer = nullptr;

    QString m_hostAddress;
    quint16 m_hostPort = 0;

    // Exponential backoff state
    int m_currentInterval = 3000;
    
    // Connection state tracking
    ConnectionState m_state = ConnectionState::Disconnected;
    QDateTime m_connectionStartTime;
    qint64 m_bytesSent = 0;
    qint64 m_bytesReceived = 0;
    QList<ConnectionHistoryEntry> m_history;
    int m_maxHistorySize = 100;

    static constexpr int HEADER_SIZE = 24;
};

} // namespace xrk
