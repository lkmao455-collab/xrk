#include <gtest/gtest.h>
#include "app/device_registry.h"
#include <QTemporaryDir>
#include <QStandardPaths>

using namespace xrk;

class DeviceRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for test storage
        tempDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(tempDir->isValid());
        storagePath = tempDir->filePath("test_device_registry.json");
        
        registry = std::make_unique<DeviceRegistry>(storagePath);
    }
    
    void TearDown() override {
        registry.reset();
        tempDir.reset();
    }
    
    RegisteredDevice createTestDevice(const QString& id, const QString& name = "Test Device") {
        RegisteredDevice device;
        device.deviceId = id;
        device.deviceName = name;
        device.ipAddress = "192.168.1." + QString::number(100 + id.right(2).toInt());
        device.port = 9999;
        device.version = "1.0.0";
        device.group = "test-group";
        device.mac = "AA:BB:CC:DD:EE:" + id.right(2);
        device.notes = "Test notes";
        device.tags = QStringList() << "test" << "unit-test";
        device.lastSeen = QDateTime::currentDateTime();
        device.lastConnected = QDateTime::currentDateTime();
        device.online = true;
        device.lastHeartbeat = QDateTime::currentMSecsSinceEpoch();
        return device;
    }
    
    std::unique_ptr<QTemporaryDir> tempDir;
    QString storagePath;
    std::unique_ptr<DeviceRegistry> registry;
};

TEST_F(DeviceRegistryTest, RegisterDevice) {
    RegisteredDevice device = createTestDevice("dev-001", "Server-01");
    
    registry->registerDevice(device);
    
    EXPECT_TRUE(registry->containsDevice("dev-001"));
    EXPECT_EQ(registry->deviceCount(), 1);
    
    RegisteredDevice retrieved = registry->device("dev-001");
    EXPECT_EQ(retrieved.deviceId, "dev-001");
    EXPECT_EQ(retrieved.deviceName, "Server-01");
    EXPECT_EQ(retrieved.ipAddress, "192.168.1.101");
    EXPECT_TRUE(retrieved.online);
}

TEST_F(DeviceRegistryTest, UpdateDevice) {
    RegisteredDevice device = createTestDevice("dev-001", "Server-01");
    registry->registerDevice(device);
    
    // Update device
    RegisteredDevice updatedDevice = device;
    updatedDevice.deviceName = "Server-02";
    updatedDevice.group = "new-group";
    
    registry->updateDevice(updatedDevice);
    
    RegisteredDevice retrieved = registry->device("dev-001");
    EXPECT_EQ(retrieved.deviceName, "Server-02");
    EXPECT_EQ(retrieved.group, "new-group");
}

TEST_F(DeviceRegistryTest, RemoveDevice) {
    RegisteredDevice device = createTestDevice("dev-001");
    registry->registerDevice(device);
    
    EXPECT_TRUE(registry->containsDevice("dev-001"));
    
    registry->removeDevice("dev-001");
    
    EXPECT_FALSE(registry->containsDevice("dev-001"));
    EXPECT_EQ(registry->deviceCount(), 0);
}

TEST_F(DeviceRegistryTest, DeviceGroups) {
    // Register devices in different groups
    RegisteredDevice device1 = createTestDevice("dev-001", "Server-01");
    device1.group = "servers";
    registry->registerDevice(device1);
    
    RegisteredDevice device2 = createTestDevice("dev-002", "Workstation-01");
    device2.group = "workstations";
    registry->registerDevice(device2);
    
    RegisteredDevice device3 = createTestDevice("dev-003", "Server-02");
    device3.group = "servers";
    registry->registerDevice(device3);
    
    // Test group queries
    QList<RegisteredDevice> servers = registry->devicesByGroup("servers");
    EXPECT_EQ(servers.size(), 2);
    
    QList<RegisteredDevice> workstations = registry->devicesByGroup("workstations");
    EXPECT_EQ(workstations.size(), 1);
    
    // Test groups list
    QStringList groups = registry->groups();
    EXPECT_TRUE(groups.contains("servers"));
    EXPECT_TRUE(groups.contains("workstations"));
}

TEST_F(DeviceRegistryTest, OnlineDevices) {
    // Register devices
    RegisteredDevice device1 = createTestDevice("dev-001");
    registry->registerDevice(device1);
    
    RegisteredDevice device2 = createTestDevice("dev-002");
    registry->registerDevice(device2);
    registry->setDeviceOnline("dev-002", false);
    
    RegisteredDevice device3 = createTestDevice("dev-003");
    registry->registerDevice(device3);
    
    // Test online query
    QList<RegisteredDevice> online = registry->onlineDevices();
    EXPECT_EQ(online.size(), 2);
    
    // Test statistics
    EXPECT_EQ(registry->deviceCount(), 3);
    EXPECT_EQ(registry->onlineDeviceCount(), 2);
}

TEST_F(DeviceRegistryTest, SearchDevices) {
    // Register devices
    RegisteredDevice device1 = createTestDevice("dev-001", "Production Server");
    device1.ipAddress = "192.168.1.100";
    device1.mac = "AA:BB:CC:DD:EE:01";
    registry->registerDevice(device1);
    
    RegisteredDevice device2 = createTestDevice("dev-002", "Development Workstation");
    device2.ipAddress = "192.168.1.101";
    device2.mac = "AA:BB:CC:DD:EE:02";
    registry->registerDevice(device2);
    
    RegisteredDevice device3 = createTestDevice("dev-003", "Test Server");
    device3.ipAddress = "10.0.0.100";
    device3.mac = "FF:FF:FF:FF:FF:FF";
    registry->registerDevice(device3);
    
    // Search by name
    QList<RegisteredDevice> results = registry->searchDevices("Server");
    EXPECT_EQ(results.size(), 2);
    
    // Search by IP
    results = registry->searchDevices("192.168.1");
    EXPECT_EQ(results.size(), 2);
    
    // Search by MAC
    results = registry->searchDevices("AA:BB:CC");
    EXPECT_EQ(results.size(), 2);
    
    // Search by group
    results = registry->searchDevices("test-group");
    EXPECT_EQ(results.size(), 3);
}

TEST_F(DeviceRegistryTest, Persistence) {
    // Register devices
    RegisteredDevice device1 = createTestDevice("dev-001", "Server-01");
    registry->registerDevice(device1);
    
    RegisteredDevice device2 = createTestDevice("dev-002", "Server-02");
    registry->registerDevice(device2);
    
    // Save and create new registry
    registry->save();
    auto newRegistry = std::make_unique<DeviceRegistry>(storagePath);
    
    // Verify devices are loaded
    EXPECT_EQ(newRegistry->deviceCount(), 2);
    EXPECT_TRUE(newRegistry->containsDevice("dev-001"));
    EXPECT_TRUE(newRegistry->containsDevice("dev-002"));
    
    RegisteredDevice retrieved = newRegistry->device("dev-001");
    EXPECT_EQ(retrieved.deviceName, "Server-01");
}

TEST_F(DeviceRegistryTest, GroupManagement) {
    // Register devices
    RegisteredDevice device1 = createTestDevice("dev-001");
    device1.group = "old-group";
    registry->registerDevice(device1);
    
    RegisteredDevice device2 = createTestDevice("dev-002");
    device2.group = "old-group";
    registry->registerDevice(device2);
    
    RegisteredDevice device3 = createTestDevice("dev-003");
    device3.group = "other-group";
    registry->registerDevice(device3);
    
    // Rename group
    bool renamed = registry->renameGroup("old-group", "new-group");
    EXPECT_TRUE(renamed);
    
    QList<RegisteredDevice> newGroup = registry->devicesByGroup("new-group");
    EXPECT_EQ(newGroup.size(), 2);
    
    QList<RegisteredDevice> oldGroup = registry->devicesByGroup("old-group");
    EXPECT_EQ(oldGroup.size(), 0);
    
    // Remove group
    bool removed = registry->removeGroup("new-group");
    EXPECT_TRUE(removed);
    
    // Devices in removed group should have empty group
    EXPECT_TRUE(registry->device("dev-001").group.isEmpty());
    EXPECT_TRUE(registry->device("dev-002").group.isEmpty());
    // Device in other group should keep its group
    EXPECT_EQ(registry->device("dev-003").group, "other-group");
}

TEST_F(DeviceRegistryTest, OnlineStatusUpdates) {
    RegisteredDevice device = createTestDevice("dev-001");
    registry->registerDevice(device);
    
    // Device is online by default after registration
    EXPECT_TRUE(registry->device("dev-001").online);
    
    // Set offline
    registry->setDeviceOnline("dev-001", false);
    EXPECT_FALSE(registry->device("dev-001").online);
    
    // Set online
    registry->setDeviceOnline("dev-001", true);
    EXPECT_TRUE(registry->device("dev-001").online);
    
    // Update heartbeat
    registry->updateHeartbeat("dev-001");
    EXPECT_TRUE(registry->device("dev-001").online);
    
    // Set offline again
    registry->setDeviceOnline("dev-001", false);
    EXPECT_FALSE(registry->device("dev-001").online);
}

TEST_F(DeviceRegistryTest, EmptyDeviceId) {
    RegisteredDevice device;
    device.deviceId = "";  // Empty ID
    device.deviceName = "Invalid Device";
    
    registry->registerDevice(device);
    
    // Should not be registered
    EXPECT_EQ(registry->deviceCount(), 0);
}

TEST_F(DeviceRegistryTest, NonExistentDevice) {
    EXPECT_FALSE(registry->containsDevice("nonexistent"));
    
    RegisteredDevice device = registry->device("nonexistent");
    EXPECT_TRUE(device.deviceId.isEmpty());
    
    // Update non-existent device should not crash
    RegisteredDevice invalidDevice = createTestDevice("nonexistent");
    registry->updateDevice(invalidDevice);
    
    // Remove non-existent device should not crash
    registry->removeDevice("nonexistent");
}

TEST_F(DeviceRegistryTest, Statistics) {
    // Register devices
    for (int i = 0; i < 5; ++i) {
        RegisteredDevice device = createTestDevice("dev-00" + QString::number(i));
        device.group = (i % 2 == 0) ? "even-group" : "odd-group";
        device.online = (i % 3 == 0);
        registry->registerDevice(device);
    }
    
    EXPECT_EQ(registry->deviceCount(), 5);
    EXPECT_EQ(registry->groupDeviceCount("even-group"), 3);
    EXPECT_EQ(registry->groupDeviceCount("odd-group"), 2);
    
    // Count online devices
    int onlineCount = 0;
    QList<RegisteredDevice> allDevices = registry->allDevices();
    for (const RegisteredDevice& device : allDevices) {
        if (device.online) onlineCount++;
    }
    EXPECT_EQ(registry->onlineDeviceCount(), onlineCount);
}