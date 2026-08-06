#include "connection_history_manager.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

namespace xrk {

ConnectionHistoryManager::ConnectionHistoryManager(QObject* parent)
    : QObject(parent)
    , m_settings("XRK", "ConnectionHistory") {
    loadFromSettings();
}

ConnectionHistoryManager::~ConnectionHistoryManager() {
    saveToSettings();
}

ConnectionHistoryManager* ConnectionHistoryManager::instance() {
    static ConnectionHistoryManager* instance = nullptr;
    if (!instance) {
        instance = new ConnectionHistoryManager();
    }
    return instance;
}

void ConnectionHistoryManager::recordConnection(const ConnectionRecord& record) {
    QMutexLocker locker(&m_mutex);
    
    m_history.prepend(record);
    trimHistory();
    saveToSettings();
    
    emit connectionRecorded(record);
}

QList<ConnectionRecord> ConnectionHistoryManager::getRecentConnections(int limit) const {
    QMutexLocker locker(&m_mutex);
    QList<ConnectionRecord> result;
    for (int i = 0; i < m_history.size() && i < limit; ++i) {
        result.append(m_history[i]);
    }
    return result;
}

QList<ConnectionRecord> ConnectionHistoryManager::getConnectionsForHost(const QString& hostAddress, quint16 port) const {
    QMutexLocker locker(&m_mutex);
    QList<ConnectionRecord> result;
    for (const auto& record : m_history) {
        if (record.hostAddress == hostAddress && record.hostPort == port) {
            result.append(record);
        }
    }
    return result;
}

QList<ConnectionRecord> ConnectionHistoryManager::getSuccessfulConnections(int limit) const {
    QMutexLocker locker(&m_mutex);
    QList<ConnectionRecord> result;
    for (const auto& record : m_history) {
        if (record.success) {
            result.append(record);
            if (result.size() >= limit) break;
        }
    }
    return result;
}

QList<ConnectionRecord> ConnectionHistoryManager::getFailedConnections(int limit) const {
    QMutexLocker locker(&m_mutex);
    QList<ConnectionRecord> result;
    for (const auto& record : m_history) {
        if (!record.success) {
            result.append(record);
            if (result.size() >= limit) break;
        }
    }
    return result;
}

void ConnectionHistoryManager::clearHistory() {
    QMutexLocker locker(&m_mutex);
    m_history.clear();
    saveToSettings();
    emit historyCleared();
}

ConnectionHistoryManager::Statistics ConnectionHistoryManager::getStatistics() const {
    QMutexLocker locker(&m_mutex);
    Statistics stats;
    for (const auto& record : m_history) {
        stats.totalConnections++;
        if (record.success) {
            stats.successfulConnections++;
        } else {
            stats.failedConnections++;
        }
        stats.totalDuration += record.duration;
        stats.totalBytesSent += record.bytesSent;
        stats.totalBytesReceived += record.bytesReceived;
        stats.maxReconnectAttempts = qMax(stats.maxReconnectAttempts, record.reconnectAttempts);
    }
    return stats;
}

QByteArray ConnectionHistoryManager::exportToJson() const {
    QMutexLocker locker(&m_mutex);
    QJsonArray array;
    for (const auto& record : m_history) {
        QJsonObject obj;
        obj["timestamp"] = record.timestamp.toString(Qt::ISODate);
        obj["hostAddress"] = record.hostAddress;
        obj["hostPort"] = record.hostPort;
        obj["deviceName"] = record.deviceName;
        obj["deviceId"] = record.deviceId;
        obj["success"] = record.success;
        obj["errorMessage"] = record.errorMessage;
        obj["duration"] = record.duration;
        obj["bytesSent"] = record.bytesSent;
        obj["bytesReceived"] = record.bytesReceived;
        obj["reconnectAttempts"] = record.reconnectAttempts;
        array.append(obj);
    }
    QJsonDocument doc(array);
    return doc.toJson(QJsonDocument::Compact);
}

bool ConnectionHistoryManager::importFromJson(const QByteArray& json) {
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        return false;
    }
    
    QMutexLocker locker(&m_mutex);
    m_history.clear();
    
    QJsonArray array = doc.array();
    for (const auto& value : array) {
        QJsonObject obj = value.toObject();
        ConnectionRecord record;
        record.timestamp = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
        record.hostAddress = obj["hostAddress"].toString();
        record.hostPort = obj["hostPort"].toInt();
        record.deviceName = obj["deviceName"].toString();
        record.deviceId = obj["deviceId"].toString();
        record.success = obj["success"].toBool();
        record.errorMessage = obj["errorMessage"].toString();
        record.duration = obj["duration"].toVariant().toLongLong();
        record.bytesSent = obj["bytesSent"].toVariant().toLongLong();
        record.bytesReceived = obj["bytesReceived"].toVariant().toLongLong();
        record.reconnectAttempts = obj["reconnectAttempts"].toInt();
        m_history.append(record);
    }
    saveToSettings();
    return true;
}

void ConnectionHistoryManager::loadFromSettings() {
    m_history.clear();
    int size = m_settings.beginReadArray("connections");
    for (int i = 0; i < size; ++i) {
        m_settings.setArrayIndex(i);
        ConnectionRecord record;
        record.timestamp = QDateTime::fromString(m_settings.value("timestamp").toString(), Qt::ISODate);
        record.hostAddress = m_settings.value("hostAddress").toString();
        record.hostPort = m_settings.value("hostPort").toInt();
        record.deviceName = m_settings.value("deviceName").toString();
        record.deviceId = m_settings.value("deviceId").toString();
        record.success = m_settings.value("success").toBool();
        record.errorMessage = m_settings.value("errorMessage").toString();
        record.duration = m_settings.value("duration").toLongLong();
        record.bytesSent = m_settings.value("bytesSent").toLongLong();
        record.bytesReceived = m_settings.value("bytesReceived").toLongLong();
        record.reconnectAttempts = m_settings.value("reconnectAttempts").toInt();
        m_history.append(record);
    }
    m_settings.endArray();
}

void ConnectionHistoryManager::saveToSettings() {
    m_settings.beginWriteArray("connections");
    for (int i = 0; i < m_history.size(); ++i) {
        m_settings.setArrayIndex(i);
        const auto& record = m_history[i];
        m_settings.setValue("timestamp", record.timestamp.toString(Qt::ISODate));
        m_settings.setValue("hostAddress", record.hostAddress);
        m_settings.setValue("hostPort", record.hostPort);
        m_settings.setValue("deviceName", record.deviceName);
        m_settings.setValue("deviceId", record.deviceId);
        m_settings.setValue("success", record.success);
        m_settings.setValue("errorMessage", record.errorMessage);
        m_settings.setValue("duration", record.duration);
        m_settings.setValue("bytesSent", record.bytesSent);
        m_settings.setValue("bytesReceived", record.bytesReceived);
        m_settings.setValue("reconnectAttempts", record.reconnectAttempts);
    }
    m_settings.endArray();
}

void ConnectionHistoryManager::trimHistory() {
    while (m_history.size() > m_maxHistorySize) {
        m_history.removeLast();
    }
}

} // namespace xrk