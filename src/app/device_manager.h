#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include "core/types.h"

namespace xrk {

class DeviceDiscovery;

class DeviceManager : public QObject {
    Q_OBJECT
public:
    explicit DeviceManager(DeviceDiscovery* discovery, QObject* parent = nullptr);
    ~DeviceManager();

    void addDevice(const DeviceInfo& info);
    void removeDevice(const QString& deviceId);
    void updateDevice(const DeviceInfo& info);
    
    QList<DeviceInfo> getDevices() const;
    DeviceInfo getDevice(const QString& deviceId) const;
    bool hasDevice(const QString& deviceId) const;
    int deviceCount() const;

signals:
    void deviceAdded(const DeviceInfo& info);
    void deviceRemoved(const QString& deviceId);
    void deviceUpdated(const DeviceInfo& info);
    void deviceListChanged();

private slots:
    void onDeviceFound(const DeviceInfo& info);
    void onDeviceLost(const QString& deviceId);

private:
    DeviceDiscovery* m_discovery = nullptr;
    QHash<QString, DeviceInfo> m_devices;
};

} // namespace xrk
