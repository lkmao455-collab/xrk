#include <gtest/gtest.h>
#include "app/device_manager.h"
#include "core/device_discovery.h"
#include "core/network_manager.h"
#include "core/types.h"

using namespace xrk;

class DeviceManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        network = std::make_unique<NetworkManager>();
        discovery = std::make_unique<DeviceDiscovery>(network.get());
        manager = std::make_unique<DeviceManager>(discovery.get());
    }
    
    void TearDown() override {
        manager.reset();
        discovery.reset();
        network.reset();
    }
    
    std::unique_ptr<NetworkManager> network;
    std::unique_ptr<DeviceDiscovery> discovery;
    std::unique_ptr<DeviceManager> manager;
};

TEST_F(DeviceManagerTest, AddDevice) {
    DeviceInfo info;
    info.deviceId = "device-123";
    info.deviceName = "Test Device";
    info.ipAddress = "192.168.1.100";
    info.port = 9999;
    info.version = "1.0.0";
    info.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    manager->addDevice(info);
    
    QList<DeviceInfo> devices = manager->getDevices();
    EXPECT_EQ(devices.size(), 1);
    EXPECT_EQ(devices[0].deviceId, info.deviceId);
}

TEST_F(DeviceManagerTest, RemoveDevice) {
    DeviceInfo info;
    info.deviceId = "device-123";
    info.deviceName = "Test Device";
    info.ipAddress = "192.168.1.100";
    info.port = 9999;
    info.version = "1.0.0";
    info.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    manager->addDevice(info);
    EXPECT_EQ(manager->getDevices().size(), 1);
    
    manager->removeDevice(info.deviceId);
    EXPECT_EQ(manager->getDevices().size(), 0);
}

TEST_F(DeviceManagerTest, UpdateDevice) {
    DeviceInfo info;
    info.deviceId = "device-123";
    info.deviceName = "Test Device";
    info.ipAddress = "192.168.1.100";
    info.port = 9999;
    info.version = "1.0.0";
    info.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    manager->addDevice(info);
    
    DeviceInfo updatedInfo = info;
    updatedInfo.deviceName = "Updated Device";
    manager->updateDevice(updatedInfo);
    
    QList<DeviceInfo> devices = manager->getDevices();
    EXPECT_EQ(devices.size(), 1);
    EXPECT_EQ(devices[0].deviceName, "Updated Device");
}

TEST_F(DeviceManagerTest, GetDevice) {
    DeviceInfo info;
    info.deviceId = "device-123";
    info.deviceName = "Test Device";
    info.ipAddress = "192.168.1.100";
    info.port = 9999;
    info.version = "1.0.0";
    info.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    manager->addDevice(info);
    
    DeviceInfo retrieved = manager->getDevice(info.deviceId);
    EXPECT_EQ(retrieved.deviceId, info.deviceId);
    EXPECT_EQ(retrieved.deviceName, info.deviceName);
}

TEST_F(DeviceManagerTest, GetDeviceNotFound) {
    DeviceInfo retrieved = manager->getDevice("nonexistent");
    EXPECT_TRUE(retrieved.deviceId.isEmpty());
}

TEST_F(DeviceManagerTest, MultipleDevices) {
    for (int i = 0; i < 5; ++i) {
        DeviceInfo info;
        info.deviceId = "device-" + QString::number(i);
        info.deviceName = "Device " + QString::number(i);
        info.ipAddress = "192.168.1." + QString::number(100 + i);
        info.port = 9999;
        info.version = "1.0.0";
        info.timestamp = QDateTime::currentMSecsSinceEpoch();
        
        manager->addDevice(info);
    }
    
    QList<DeviceInfo> devices = manager->getDevices();
    EXPECT_EQ(devices.size(), 5);
}

TEST_F(DeviceManagerTest, ClearDevices) {
    QList<QString> ids;
    for (int i = 0; i < 3; ++i) {
        DeviceInfo info;
        info.deviceId = "device-" + QString::number(i);
        info.deviceName = "Device " + QString::number(i);
        info.ipAddress = "192.168.1." + QString::number(100 + i);
        info.port = 9999;
        info.version = "1.0.0";
        info.timestamp = QDateTime::currentMSecsSinceEpoch();

        manager->addDevice(info);
        ids.append(info.deviceId);
    }

    for (const QString& id : ids) {
        manager->removeDevice(id);
    }
    EXPECT_EQ(manager->getDevices().size(), 0);
}
