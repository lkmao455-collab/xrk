#include "device_manager.h"
#include "core/device_discovery.h"
#include "core/logger.h"

namespace xrk {

DeviceManager::DeviceManager(DeviceDiscovery* discovery, QObject* parent)
    : QObject(parent), m_discovery(discovery) {
    
    if (m_discovery) {
        connect(m_discovery, &DeviceDiscovery::deviceFound, this, &DeviceManager::onDeviceFound);
        connect(m_discovery, &DeviceDiscovery::deviceLost, this, &DeviceManager::onDeviceLost);
    }
}

DeviceManager::~DeviceManager() {
}

void DeviceManager::addDevice(const DeviceInfo& info) {
    if (info.deviceId.isEmpty()) {
        return;
    }
    
    bool isNew = !m_devices.contains(info.deviceId);
    m_devices[info.deviceId] = info;
    
    if (isNew) {
        emit deviceAdded(info);
        emit deviceListChanged();
        LOG_DEBUG("Device added: " + info.deviceName);
    }
}

void DeviceManager::removeDevice(const QString& deviceId) {
    if (m_devices.remove(deviceId)) {
        emit deviceRemoved(deviceId);
        emit deviceListChanged();
        LOG_DEBUG("Device removed: " + deviceId);
    }
}

void DeviceManager::updateDevice(const DeviceInfo& info) {
    if (m_devices.contains(info.deviceId)) {
        m_devices[info.deviceId] = info;
        emit deviceUpdated(info);
        emit deviceListChanged();
    }
}

QList<DeviceInfo> DeviceManager::getDevices() const {
    return m_devices.values();
}

DeviceInfo DeviceManager::getDevice(const QString& deviceId) const {
    return m_devices.value(deviceId);
}

bool DeviceManager::hasDevice(const QString& deviceId) const {
    return m_devices.contains(deviceId);
}

int DeviceManager::deviceCount() const {
    return m_devices.size();
}

void DeviceManager::onDeviceFound(const DeviceInfo& info) {
    addDevice(info);
}

void DeviceManager::onDeviceLost(const QString& deviceId) {
    removeDevice(deviceId);
}

} // namespace xrk
