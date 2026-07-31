#include <gtest/gtest.h>
#include "core/network_manager.h"
#include "core/device_discovery.h"
#include "core/protocol_manager.h"
#include "core/types.h"

using namespace xrk;

class DeviceDiscoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        qRegisterMetaType<DeviceInfo>();
        network = std::make_unique<NetworkManager>();
        network->initialize(9999);
    }
    
    void TearDown() override {
        network->shutdown();
    }
    
    std::unique_ptr<NetworkManager> network;
};

TEST_F(DeviceDiscoveryTest, StartStop) {
    DeviceDiscovery discovery(network.get());
    
    discovery.startDiscovery();
    EXPECT_TRUE(true);
    
    discovery.stopDiscovery();
    EXPECT_TRUE(true);
}

TEST_F(DeviceDiscoveryTest, GetDevicesEmpty) {
    DeviceDiscovery discovery(network.get());
    
    auto devices = discovery.getDevices();
    EXPECT_TRUE(devices.isEmpty());
}

TEST_F(DeviceDiscoveryTest, HasDeviceFalse) {
    DeviceDiscovery discovery(network.get());
    
    EXPECT_FALSE(discovery.hasDevice("nonexistent"));
}

TEST_F(DeviceDiscoveryTest, AddDevice) {
    DeviceDiscovery discovery(network.get());
    
    DeviceInfo info;
    info.deviceId = "test-device-123";
    info.deviceName = "Test Device";
    info.ipAddress = "192.168.1.100";
    info.port = 9999;
    info.version = "1.0.0";
    info.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    QMetaObject::invokeMethod(&discovery, "onDiscoveryMessage", Q_ARG(DeviceInfo, info));
    
    EXPECT_TRUE(discovery.hasDevice("test-device-123"));
    EXPECT_EQ(discovery.getDevice("test-device-123").deviceName, "Test Device");
}

TEST_F(DeviceDiscoveryTest, GetDevices) {
    DeviceDiscovery discovery(network.get());
    
    DeviceInfo info1;
    info1.deviceId = "device-1";
    info1.deviceName = "Device 1";
    info1.timestamp = QDateTime::currentMSecsSinceEpoch();
    QMetaObject::invokeMethod(&discovery, "onDiscoveryMessage", Q_ARG(DeviceInfo, info1));
    
    DeviceInfo info2;
    info2.deviceId = "device-2";
    info2.deviceName = "Device 2";
    info2.timestamp = QDateTime::currentMSecsSinceEpoch();
    QMetaObject::invokeMethod(&discovery, "onDiscoveryMessage", Q_ARG(DeviceInfo, info2));
    
    auto devices = discovery.getDevices();
    EXPECT_EQ(devices.size(), 2);
}
