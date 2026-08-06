#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QList>
#include <QSettings>
#include <QMutex>

namespace xrk {

// Connection record for persistent history
struct ConnectionRecord {
    QDateTime timestamp;
    QString hostAddress;
    quint16 hostPort;
    QString deviceName;
    QString deviceId;
    bool success;
    QString errorMessage;
    qint64 duration = 0;  // in ms
    qint64 bytesSent = 0;
    qint64 bytesReceived = 0;
    int reconnectAttempts = 0;
};

class ConnectionHistoryManager : public QObject {
    Q_OBJECT
public:
    explicit ConnectionHistoryManager(QObject* parent = nullptr);
    ~ConnectionHistoryManager();

    static ConnectionHistoryManager* instance();

    // Record a connection attempt
    void recordConnection(const ConnectionRecord& record);

    // Get recent connections
    QList<ConnectionRecord> getRecentConnections(int limit = 50) const;
    
    // Get connections for a specific host
    QList<ConnectionRecord> getConnectionsForHost(const QString& hostAddress, quint16 port) const;
    
    // Get successful connections
    QList<ConnectionRecord> getSuccessfulConnections(int limit = 20) const;
    
    // Get failed connections
    QList<ConnectionRecord> getFailedConnections(int limit = 20) const;
    
    // Clear all history
    void clearHistory();
    
    // Get statistics
    struct Statistics {
        int totalConnections = 0;
        int successfulConnections = 0;
        int failedConnections = 0;
        qint64 totalDuration = 0;
        qint64 totalBytesSent = 0;
        qint64 totalBytesReceived = 0;
        int maxReconnectAttempts = 0;
    };
    Statistics getStatistics() const;

    // Export to JSON
    QByteArray exportToJson() const;
    
    // Import from JSON
    bool importFromJson(const QByteArray& json);

signals:
    void connectionRecorded(const ConnectionRecord& record);
    void historyCleared();

private:
    void loadFromSettings();
    void saveToSettings();
    void trimHistory();

    QList<ConnectionRecord> m_history;
    int m_maxHistorySize = 1000;
    QSettings m_settings;
    mutable QMutex m_mutex;
};

} // namespace xrk