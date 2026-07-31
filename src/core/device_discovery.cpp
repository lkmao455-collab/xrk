#include "device_discovery.h"
#include "network_manager.h"
#include "protocol_manager.h"
#include "arp_resolver.h"
#include "logger.h"
#include <QTimer>
#include <QMetaObject>
#include <QElapsedTimer>
#include <thread>

namespace xrk {

DeviceDiscovery::DeviceDiscovery(NetworkManager* network, QObject* parent)
    : QObject(parent), m_network(network) {
    
    m_broadcastTimer = new QTimer(this);
    connect(m_broadcastTimer, &QTimer::timeout, this, &DeviceDiscovery::broadcastDiscoveryRequest);
    
    m_cleanupTimer = new QTimer(this);
    connect(m_cleanupTimer, &QTimer::timeout, this, &DeviceDiscovery::onCleanupOldDevices);
    
    if (m_network) {
        connect(m_network, &NetworkManager::discoveryReceived, this, &DeviceDiscovery::onDiscoveryMessage);
    }
}

DeviceDiscovery::~DeviceDiscovery() {
    stopDiscovery();
}

void DeviceDiscovery::startDiscovery() {
    if (m_broadcastTimer->isActive()) {
        return;
    }
    
    m_broadcastTimer->start(BROADCAST_INTERVAL_MS);
    m_cleanupTimer->start(DEVICE_TIMEOUT_MS / 2);
    
    broadcastDiscoveryRequest();
    LOG_INFO("Device discovery started");
}

void DeviceDiscovery::stopDiscovery() {
    m_broadcastTimer->stop();
    m_cleanupTimer->stop();
    LOG_INFO("Device discovery stopped");
}

QList<DeviceInfo> DeviceDiscovery::getDevices() const {
    return m_devices.values();
}

bool DeviceDiscovery::hasDevice(const QString& deviceId) const {
    return m_devices.contains(deviceId);
}

DeviceInfo DeviceDiscovery::getDevice(const QString& deviceId) const {
    return m_devices.value(deviceId);
}

void DeviceDiscovery::onDiscoveryMessage(const DeviceInfo& info) {
    if (info.deviceId.isEmpty()) {
        return;
    }
    
    addOrUpdateDevice(info);
}

void DeviceDiscovery::onCleanupOldDevices() {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList expiredDevices;
    
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (now - it.value().timestamp > DEVICE_TIMEOUT_MS) {
            expiredDevices.append(it.key());
        }
    }
    
    for (const QString& deviceId : expiredDevices) {
        m_arpPending.remove(deviceId);
        m_devices.remove(deviceId);
        emit deviceLost(deviceId);
        LOG_DEBUG("Device lost: " + deviceId);
    }
}

void DeviceDiscovery::broadcastDiscoveryRequest() {
    if (m_network) {
        m_network->broadcastDiscovery();
    }
}

void DeviceDiscovery::addOrUpdateDevice(const DeviceInfo& info) {
    bool isNew = !m_devices.contains(info.deviceId);

    // Preserve a previously resolved MAC; leave it empty for new devices so
    // the async resolver can fill it in without blocking this thread.
    DeviceInfo stored = info;
    if (!isNew) {
        stored.macAddress = m_devices[info.deviceId].macAddress;
    }

    // Track ARP resolution state so the UI can surface a hint (Pending/超时).
    if (!stored.macAddress.isEmpty()) {
        stored.arpStatus = DeviceInfo::ArpStatus::Resolved;
    } else if (!info.ipAddress.isEmpty()) {
        stored.arpStatus = DeviceInfo::ArpStatus::Pending;
    } else {
        stored.arpStatus = DeviceInfo::ArpStatus::Unknown;
    }

    m_devices[info.deviceId] = stored;

    if (isNew) {
        emit deviceFound(stored);
        LOG_INFO("Device found: " + info.deviceName + " (" + info.ipAddress + ")");
    } else {
        emit deviceUpdated(stored);
    }

    // Resolve the MAC asynchronously (off this thread) so an unreachable host
    // (SendARP blocks for seconds) never stalls discovery. Skips when we
    // already have a MAC or a resolution is already in flight for this device.
    if (stored.arpStatus == DeviceInfo::ArpStatus::Pending) {
        resolveMacAsync(info);
    }
}

void DeviceDiscovery::resolveMacAsync(const DeviceInfo& info) {
    const QString deviceId = info.deviceId;
    if (m_arpPending.contains(deviceId)) {
        return; // a resolution is already in flight for this device
    }
    m_arpPending.insert(deviceId);

    const QString ip = info.ipAddress;
    std::thread([this, ip, deviceId]() {
        QElapsedTimer timer;
        timer.start();
        const QString mac = ArpResolver::resolveMac(ip);
        const bool timedOut = timer.elapsed() > ARP_TIMEOUT_MS;

        // Marshal the outcome back onto the discovery thread so all access to
        // m_devices / m_arpPending stays single-threaded.
        QMetaObject::invokeMethod(this, [this, deviceId, mac, timedOut]() {
            m_arpPending.remove(deviceId);
            auto it = m_devices.find(deviceId);
            if (it == m_devices.end()) {
                return; // device was removed while resolving
            }
            if (!it->macAddress.isEmpty()) {
                return; // already filled by a newer update
            }
            if (mac.isEmpty() || timedOut) {
                it->arpStatus = DeviceInfo::ArpStatus::Timeout;
            } else {
                it->macAddress = mac;
                it->arpStatus = DeviceInfo::ArpStatus::Resolved;
            }
            emit deviceUpdated(*it);
        });
    }).detach();
}

} // namespace xrk
