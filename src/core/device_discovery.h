#pragma once

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QSet>
#include "types.h"

namespace xrk {

class NetworkManager;

class DeviceDiscovery : public QObject {
    Q_OBJECT
public:
    explicit DeviceDiscovery(NetworkManager* network, QObject* parent = nullptr);
    ~DeviceDiscovery();

    void startDiscovery();
    void stopDiscovery();
    
    QList<DeviceInfo> getDevices() const;
    bool hasDevice(const QString& deviceId) const;
    DeviceInfo getDevice(const QString& deviceId) const;

signals:
    void deviceFound(const DeviceInfo& info);
    void deviceLost(const QString& deviceId);
    void deviceUpdated(const DeviceInfo& info);

private slots:
    void onDiscoveryMessage(const DeviceInfo& info);
    void onCleanupOldDevices();

private:
    void broadcastDiscoveryRequest();
    void addOrUpdateDevice(const DeviceInfo& info);
    void resolveMacAsync(const DeviceInfo& info);

    NetworkManager* m_network = nullptr;
    QTimer* m_broadcastTimer = nullptr;
    QTimer* m_cleanupTimer = nullptr;
    QHash<QString, DeviceInfo> m_devices;
    QSet<QString> m_arpPending;   // deviceIds with an in-flight ARP resolution
    static constexpr int BROADCAST_INTERVAL_MS = 5000;
    static constexpr int DEVICE_TIMEOUT_MS = 15000;
    static constexpr int ARP_TIMEOUT_MS = 1000;  // give up on a slow ARP reply
};

} // namespace xrk
