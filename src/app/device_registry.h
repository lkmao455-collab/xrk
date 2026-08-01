#pragma once

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QString>
#include <QMutex>
#include <QTimer>

namespace xrk {

struct RegisteredDevice {
    QString deviceId;
    QString deviceName;
    QString ipAddress;
    uint16_t port = 9999;
    QString version;
    QString group;
    QString mac;
    QString notes;
    QStringList tags;
    QDateTime lastSeen;
    QDateTime lastConnected;
    bool online = false;
    qint64 lastHeartbeat = 0;

    QJsonObject toJson() const;
    static RegisteredDevice fromJson(const QJsonObject& obj);
};

class DeviceRegistry : public QObject {
    Q_OBJECT
public:
    explicit DeviceRegistry(const QString& storagePath = QString(), QObject* parent = nullptr);
    ~DeviceRegistry();

    void setStoragePath(const QString& path);
    QString storagePath() const;

    void load();
    void save();

    // Device management
    void registerDevice(const RegisteredDevice& device);
    void updateDevice(const RegisteredDevice& device);
    void removeDevice(const QString& deviceId);
    bool containsDevice(const QString& deviceId) const;
    RegisteredDevice device(const QString& deviceId) const;
    QList<RegisteredDevice> allDevices() const;
    QList<RegisteredDevice> devicesByGroup(const QString& group) const;
    QList<RegisteredDevice> onlineDevices() const;
    QList<RegisteredDevice> searchDevices(const QString& keyword) const;

    // Group management
    QStringList groups() const;
    bool renameGroup(const QString& oldName, const QString& newName);
    bool removeGroup(const QString& name);

    // Status updates
    void setDeviceOnline(const QString& deviceId, bool online);
    void updateHeartbeat(const QString& deviceId);

    // Statistics
    int deviceCount() const;
    int onlineDeviceCount() const;
    int groupDeviceCount(const QString& group) const;

signals:
    void deviceRegistered(const QString& deviceId);
    void deviceUpdated(const QString& deviceId);
    void deviceRemoved(const QString& deviceId);
    void deviceOnlineStatusChanged(const QString& deviceId, bool online);
    void deviceListChanged();

private slots:
    void onCleanupTimer();

private:
    void cleanupStaleDevices();
    bool isDeviceStale(const RegisteredDevice& device) const;

    QHash<QString, RegisteredDevice> m_devices;
    QString m_storagePath;
    mutable QMutex m_mutex;
    QTimer* m_cleanupTimer = nullptr;

    static constexpr int CLEANUP_INTERVAL_MS = 60000;  // 1 minute
    static constexpr int DEVICE_TIMEOUT_MS = 300000;   // 5 minutes without heartbeat = offline
};

} // namespace xrk