#include "device_registry.h"
#include "core/logger.h"
#include <QFile>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QDir>

namespace xrk {

QJsonObject RegisteredDevice::toJson() const {
    QJsonObject obj;
    obj["deviceId"] = deviceId;
    obj["deviceName"] = deviceName;
    obj["ipAddress"] = ipAddress;
    obj["port"] = port;
    obj["version"] = version;
    obj["group"] = group;
    obj["mac"] = mac;
    obj["notes"] = notes;
    obj["tags"] = QJsonArray::fromStringList(tags);
    obj["lastSeen"] = lastSeen.toSecsSinceEpoch();
    obj["lastConnected"] = lastConnected.toSecsSinceEpoch();
    obj["online"] = online;
    obj["lastHeartbeat"] = lastHeartbeat;
    return obj;
}

RegisteredDevice RegisteredDevice::fromJson(const QJsonObject& obj) {
    RegisteredDevice device;
    device.deviceId = obj["deviceId"].toString();
    device.deviceName = obj["deviceName"].toString();
    device.ipAddress = obj["ipAddress"].toString();
    device.port = static_cast<uint16_t>(obj["port"].toInt(9999));
    device.version = obj["version"].toString();
    device.group = obj["group"].toString();
    device.mac = obj["mac"].toString();
    device.notes = obj["notes"].toString();
    device.tags.clear();
    QJsonArray tagsArray = obj["tags"].toArray();
    for (const QJsonValue& v : tagsArray) {
        device.tags.append(v.toString());
    }
    device.lastSeen = QDateTime::fromSecsSinceEpoch(obj["lastSeen"].toInteger());
    device.lastConnected = QDateTime::fromSecsSinceEpoch(obj["lastConnected"].toInteger());
    device.online = obj["online"].toBool();
    device.lastHeartbeat = obj["lastHeartbeat"].toInteger();
    return device;
}

DeviceRegistry::DeviceRegistry(const QString& storagePath, QObject* parent)
    : QObject(parent), m_storagePath(storagePath) {
    if (m_storagePath.isEmpty()) {
        m_storagePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + "/device_registry.json";
    }
    
    m_cleanupTimer = new QTimer(this);
    connect(m_cleanupTimer, &QTimer::timeout, this, &DeviceRegistry::onCleanupTimer);
    m_cleanupTimer->start(CLEANUP_INTERVAL_MS);
    
    load();
}

DeviceRegistry::~DeviceRegistry() {
    save();
}

void DeviceRegistry::setStoragePath(const QString& path) {
    m_storagePath = path;
}

QString DeviceRegistry::storagePath() const {
    return m_storagePath;
}

void DeviceRegistry::load() {
    QFile file(m_storagePath);
    if (!file.exists()) {
        LOG_INFO("DeviceRegistry: no storage file found, starting fresh");
        return;
    }
    
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR("DeviceRegistry: failed to open storage file: " + file.errorString());
        return;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        LOG_ERROR("DeviceRegistry: invalid JSON in storage file");
        return;
    }
    
    QJsonObject root = doc.object();
    QJsonArray devicesArray = root["devices"].toArray();
    
    QMutexLocker locker(&m_mutex);
    m_devices.clear();
    
    for (const QJsonValue& value : devicesArray) {
        QJsonObject obj = value.toObject();
        RegisteredDevice device = RegisteredDevice::fromJson(obj);
        if (!device.deviceId.isEmpty()) {
            m_devices[device.deviceId] = device;
        }
    }
    
    locker.unlock();
    LOG_INFO("DeviceRegistry: loaded " + QString::number(m_devices.size()) + " devices");
}

void DeviceRegistry::save() {
    QMutexLocker locker(&m_mutex);
    
    QJsonArray devicesArray;
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        devicesArray.append(it.value().toJson());
    }
    
    QJsonObject root;
    root["devices"] = devicesArray;
    root["version"] = 1;
    root["lastSaved"] = QDateTime::currentSecsSinceEpoch();
    
    QJsonDocument doc(root);
    
    // Ensure directory exists
    QDir dir = QFileInfo(m_storagePath).absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    QFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR("DeviceRegistry: failed to save storage file: " + file.errorString());
        return;
    }
    
    file.write(doc.toJson());
    file.close();
    
    LOG_DEBUG("DeviceRegistry: saved " + QString::number(m_devices.size()) + " devices");
}

void DeviceRegistry::registerDevice(const RegisteredDevice& device) {
    if (device.deviceId.isEmpty()) {
        LOG_WARNING("DeviceRegistry: cannot register device with empty ID");
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    bool isNew = !m_devices.contains(device.deviceId);
    
    RegisteredDevice existing = m_devices.value(device.deviceId);
    
    // Merge with existing device if present
    RegisteredDevice merged = device;
    if (!isNew) {
        merged.lastConnected = existing.lastConnected;
        merged.lastSeen = existing.lastSeen;
        merged.online = existing.online;
        merged.lastHeartbeat = existing.lastHeartbeat;
        
        // Preserve user-set fields if not provided
        if (merged.group.isEmpty() && !existing.group.isEmpty()) {
            merged.group = existing.group;
        }
        if (merged.notes.isEmpty() && !existing.notes.isEmpty()) {
            merged.notes = existing.notes;
        }
        if (merged.tags.isEmpty() && !existing.tags.isEmpty()) {
            merged.tags = existing.tags;
        }
    }
    
    merged.lastSeen = QDateTime::currentDateTime();
    merged.lastConnected = QDateTime::currentDateTime();
    merged.online = true;
    merged.lastHeartbeat = QDateTime::currentMSecsSinceEpoch();
    
    m_devices[device.deviceId] = merged;
    locker.unlock();
    
    save();
    
    if (isNew) {
        emit deviceRegistered(device.deviceId);
        LOG_INFO("DeviceRegistry: registered new device: " + device.deviceId);
    } else {
        emit deviceUpdated(device.deviceId);
        LOG_INFO("DeviceRegistry: updated device: " + device.deviceId);
    }
    
    emit deviceListChanged();
}

void DeviceRegistry::updateDevice(const RegisteredDevice& device) {
    if (device.deviceId.isEmpty()) return;
    
    QMutexLocker locker(&m_mutex);
    if (!m_devices.contains(device.deviceId)) {
        LOG_WARNING("DeviceRegistry: cannot update non-existent device: " + device.deviceId);
        return;
    }
    
    m_devices[device.deviceId] = device;
    m_devices[device.deviceId].lastSeen = QDateTime::currentDateTime();
    locker.unlock();
    
    save();
    emit deviceUpdated(device.deviceId);
    emit deviceListChanged();
}

void DeviceRegistry::removeDevice(const QString& deviceId) {
    QMutexLocker locker(&m_mutex);
    if (!m_devices.contains(deviceId)) return;
    
    m_devices.remove(deviceId);
    locker.unlock();
    
    save();
    emit deviceRemoved(deviceId);
    emit deviceListChanged();
    LOG_INFO("DeviceRegistry: removed device: " + deviceId);
}

bool DeviceRegistry::containsDevice(const QString& deviceId) const {
    QMutexLocker locker(&m_mutex);
    return m_devices.contains(deviceId);
}

RegisteredDevice DeviceRegistry::device(const QString& deviceId) const {
    QMutexLocker locker(&m_mutex);
    return m_devices.value(deviceId);
}

QList<RegisteredDevice> DeviceRegistry::allDevices() const {
    QMutexLocker locker(&m_mutex);
    return m_devices.values();
}

QList<RegisteredDevice> DeviceRegistry::devicesByGroup(const QString& group) const {
    QMutexLocker locker(&m_mutex);
    QList<RegisteredDevice> result;
    
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (it.value().group == group) {
            result.append(it.value());
        }
    }
    
    return result;
}

QList<RegisteredDevice> DeviceRegistry::onlineDevices() const {
    QMutexLocker locker(&m_mutex);
    QList<RegisteredDevice> result;
    
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (it.value().online) {
            result.append(it.value());
        }
    }
    
    return result;
}

QList<RegisteredDevice> DeviceRegistry::searchDevices(const QString& keyword) const {
    QMutexLocker locker(&m_mutex);
    QList<RegisteredDevice> result;
    QString lowerKeyword = keyword.toLower();
    
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        const RegisteredDevice& device = it.value();
        if (device.deviceName.toLower().contains(lowerKeyword) ||
            device.deviceId.toLower().contains(lowerKeyword) ||
            device.ipAddress.toLower().contains(lowerKeyword) ||
            device.group.toLower().contains(lowerKeyword) ||
            device.notes.toLower().contains(lowerKeyword) ||
            device.mac.toLower().contains(lowerKeyword)) {
            result.append(device);
        }
    }
    
    return result;
}

QStringList DeviceRegistry::groups() const {
    QMutexLocker locker(&m_mutex);
    QSet<QString> groupSet;
    
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        groupSet.insert(it.value().group);
    }
    
    return groupSet.values();
}

bool DeviceRegistry::renameGroup(const QString& oldName, const QString& newName) {
    QMutexLocker locker(&m_mutex);
    bool changed = false;
    
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (it.value().group == oldName) {
            it.value().group = newName;
            changed = true;
        }
    }
    
    locker.unlock();
    
    if (changed) {
        save();
        emit deviceListChanged();
    }
    
    return changed;
}

bool DeviceRegistry::removeGroup(const QString& name) {
    QMutexLocker locker(&m_mutex);
    bool changed = false;
    
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (it.value().group == name) {
            it.value().group.clear();
            changed = true;
        }
    }
    
    locker.unlock();
    
    if (changed) {
        save();
        emit deviceListChanged();
    }
    
    return changed;
}

void DeviceRegistry::setDeviceOnline(const QString& deviceId, bool online) {
    QMutexLocker locker(&m_mutex);
    if (!m_devices.contains(deviceId)) return;
    
    bool wasOnline = m_devices[deviceId].online;
    m_devices[deviceId].online = online;
    m_devices[deviceId].lastSeen = QDateTime::currentDateTime();
    
    if (online) {
        m_devices[deviceId].lastHeartbeat = QDateTime::currentMSecsSinceEpoch();
    }
    
    locker.unlock();
    
    if (wasOnline != online) {
        emit deviceOnlineStatusChanged(deviceId, online);
        emit deviceListChanged();
    }
}

void DeviceRegistry::updateHeartbeat(const QString& deviceId) {
    QMutexLocker locker(&m_mutex);
    if (!m_devices.contains(deviceId)) return;
    
    m_devices[deviceId].lastHeartbeat = QDateTime::currentMSecsSinceEpoch();
    m_devices[deviceId].lastSeen = QDateTime::currentDateTime();
    
    if (!m_devices[deviceId].online) {
        m_devices[deviceId].online = true;
        locker.unlock();
        emit deviceOnlineStatusChanged(deviceId, true);
        emit deviceListChanged();
    }
}

int DeviceRegistry::deviceCount() const {
    QMutexLocker locker(&m_mutex);
    return m_devices.size();
}

int DeviceRegistry::onlineDeviceCount() const {
    QMutexLocker locker(&m_mutex);
    int count = 0;
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (it.value().online) count++;
    }
    return count;
}

int DeviceRegistry::groupDeviceCount(const QString& group) const {
    QMutexLocker locker(&m_mutex);
    int count = 0;
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (it.value().group == group) count++;
    }
    return count;
}

void DeviceRegistry::onCleanupTimer() {
    cleanupStaleDevices();
}

void DeviceRegistry::cleanupStaleDevices() {
    QMutexLocker locker(&m_mutex);
    QList<QString> staleDevices;
    
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (it.value().online && (now - it.value().lastHeartbeat > DEVICE_TIMEOUT_MS)) {
            staleDevices.append(it.key());
        }
    }
    
    locker.unlock();
    
    for (const QString& deviceId : staleDevices) {
        setDeviceOnline(deviceId, false);
        LOG_INFO("DeviceRegistry: device went offline (timeout): " + deviceId);
    }
}

bool DeviceRegistry::isDeviceStale(const RegisteredDevice& device) const {
    if (!device.online) return false;
    
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    return (now - device.lastHeartbeat > DEVICE_TIMEOUT_MS);
}

} // namespace xrk