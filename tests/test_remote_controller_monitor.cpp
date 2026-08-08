#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QEventLoop>
#include <QTimer>
#include "app/remote_controller.h"
#include "core/types.h"
#include "core/protocol_manager.h"
#include "core/network_manager.h"

using namespace xrk;

class RemoteControllerMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_network = new NetworkManager();
        m_controller = new RemoteController(m_network, nullptr);
    }

    void TearDown() override {
        if (m_controller) {
            m_controller->stopRemote();
            delete m_controller;
            m_controller = nullptr;
        }
        if (m_network) {
            m_network->shutdown();
            delete m_network;
            m_network = nullptr;
        }
    }

    void waitMs(int ms) {
        QEventLoop loop;
        QTimer::singleShot(ms, &loop, &QEventLoop::quit);
        loop.exec();
    }

    NetworkManager* m_network = nullptr;
    RemoteController* m_controller = nullptr;
};

// ========== Constructor Tests ==========

TEST_F(RemoteControllerMonitorTest, ConstructorCreatesValidObject) {
    EXPECT_TRUE(m_controller != nullptr);
}

TEST_F(RemoteControllerMonitorTest, InitiallyNotActive) {
    EXPECT_FALSE(m_controller->isRemoteActive());
}

// ========== Monitor List Request Tests ==========

TEST_F(RemoteControllerMonitorTest, RequestMonitorListWhenNotConnected) {
    // Should not crash when not connected
    m_controller->requestMonitorList();
    EXPECT_TRUE(true);
}

// ========== Switch Monitor Tests ==========

TEST_F(RemoteControllerMonitorTest, SwitchMonitorWhenNotConnected) {
    // Should not crash when not connected
    m_controller->switchMonitor(0);
    EXPECT_TRUE(true);
}

TEST_F(RemoteControllerMonitorTest, SwitchMonitorWithInvalidIndex) {
    // Should not crash with any index
    m_controller->switchMonitor(-1);
    m_controller->switchMonitor(999);
    EXPECT_TRUE(true);
}

// ========== Monitor Refresh Tests ==========

TEST_F(RemoteControllerMonitorTest, RequestMonitorRefreshWhenNotConnected) {
    // Should not crash when not connected
    m_controller->requestMonitorRefresh();
    EXPECT_TRUE(true);
}

// ========== Auto Switch Control Tests ==========

TEST_F(RemoteControllerMonitorTest, StartAutoSwitchWhenNotConnected) {
    // Should not crash when not connected
    m_controller->startAutoSwitch();
    EXPECT_TRUE(true);
}

TEST_F(RemoteControllerMonitorTest, StopAutoSwitchWhenNotConnected) {
    // Should not crash when not connected
    m_controller->stopAutoSwitch();
    EXPECT_TRUE(true);
}

TEST_F(RemoteControllerMonitorTest, PauseAutoSwitchWhenNotConnected) {
    // Should not crash when not connected
    m_controller->pauseAutoSwitch();
    EXPECT_TRUE(true);
}

TEST_F(RemoteControllerMonitorTest, ResumeAutoSwitchWhenNotConnected) {
    // Should not crash when not connected
    m_controller->resumeAutoSwitch();
    EXPECT_TRUE(true);
}

TEST_F(RemoteControllerMonitorTest, SetAutoSwitchIntervalWhenNotConnected) {
    // Should not crash when not connected
    m_controller->setAutoSwitchInterval(3000);
    m_controller->setAutoSwitchInterval(500);
    m_controller->setAutoSwitchInterval(60000);
    EXPECT_TRUE(true);
}

// ========== Thumbnail Request Tests ==========

TEST_F(RemoteControllerMonitorTest, RequestThumbnailFrameWhenNotConnected) {
    // Should not crash when not connected
    m_controller->requestThumbnailFrame(0, 1, 180, 100);
    EXPECT_TRUE(true);
}

TEST_F(RemoteControllerMonitorTest, RequestThumbnailFrameWithVariousParams) {
    // Should not crash with any parameters
    m_controller->requestThumbnailFrame(-1, 0, 0, 0);
    m_controller->requestThumbnailFrame(0, 99, 1920, 1080);
    EXPECT_TRUE(true);
}

// ========== Signal Connection Tests ==========

TEST_F(RemoteControllerMonitorTest, MonitorListReceivedSignalCanBeConnected) {
    QSignalSpy spy(m_controller, &RemoteController::monitorListReceived);
    EXPECT_TRUE(spy.isValid());
}

TEST_F(RemoteControllerMonitorTest, MonitorSwitchCompletedSignalCanBeConnected) {
    QSignalSpy spy(m_controller, &RemoteController::monitorSwitchCompleted);
    EXPECT_TRUE(spy.isValid());
}

TEST_F(RemoteControllerMonitorTest, AutoSwitchStatusReceivedSignalCanBeConnected) {
    QSignalSpy spy(m_controller, &RemoteController::autoSwitchStatusReceived);
    EXPECT_TRUE(spy.isValid());
}

TEST_F(RemoteControllerMonitorTest, ThumbnailFrameReceivedSignalCanBeConnected) {
    QSignalSpy spy(m_controller, &RemoteController::thumbnailFrameReceived);
    EXPECT_TRUE(spy.isValid());
}

// ========== Protocol Encoding Tests ==========

TEST_F(RemoteControllerMonitorTest, MonitorSwitchProtocolEncoding) {
    int testIndex = 2;
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<uint32_t>(testIndex);
    
    EXPECT_EQ(payload.size(), 4);
    
    QDataStream decodeStream(payload);
    decodeStream.setByteOrder(QDataStream::BigEndian);
    uint32_t decodedIndex;
    decodeStream >> decodedIndex;
    EXPECT_EQ(decodedIndex, static_cast<uint32_t>(testIndex));
}

TEST_F(RemoteControllerMonitorTest, AutoSwitchConfigProtocolEncoding) {
    int intervalMs = 5000;
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<int32_t>(intervalMs);
    
    EXPECT_EQ(payload.size(), 4);
    
    QDataStream decodeStream(payload);
    decodeStream.setByteOrder(QDataStream::BigEndian);
    int32_t decodedInterval;
    decodeStream >> decodedInterval;
    EXPECT_EQ(decodedInterval, intervalMs);
}

TEST_F(RemoteControllerMonitorTest, ThumbnailRequestProtocolEncoding) {
    int excludeIndex = 0;
    int targetIndex = 1;
    int width = 180;
    int height = 100;
    
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<int32_t>(excludeIndex);
    stream << static_cast<int32_t>(targetIndex);
    stream << static_cast<int32_t>(width);
    stream << static_cast<int32_t>(height);
    
    EXPECT_EQ(payload.size(), 16);
    
    QDataStream decodeStream(payload);
    decodeStream.setByteOrder(QDataStream::BigEndian);
    int32_t decExclude, decTarget, decWidth, decHeight;
    decodeStream >> decExclude >> decTarget >> decWidth >> decHeight;
    
    EXPECT_EQ(decExclude, excludeIndex);
    EXPECT_EQ(decTarget, targetIndex);
    EXPECT_EQ(decWidth, width);
    EXPECT_EQ(decHeight, height);
}

// ========== Message Type Tests ==========

TEST_F(RemoteControllerMonitorTest, MessageTypesExist) {
    EXPECT_EQ(static_cast<int>(MessageType::MONITOR_LIST), 60);
    EXPECT_EQ(static_cast<int>(MessageType::MONITOR_SWITCH), 61);
    EXPECT_EQ(static_cast<int>(MessageType::MONITOR_SWITCH_ACK), 62);
    EXPECT_EQ(static_cast<int>(MessageType::MONITOR_REFRESH), 63);
    EXPECT_EQ(static_cast<int>(MessageType::MONITOR_AUTO_SWITCH_START), 64);
    EXPECT_EQ(static_cast<int>(MessageType::MONITOR_AUTO_SWITCH_STOP), 65);
    EXPECT_EQ(static_cast<int>(MessageType::MONITOR_AUTO_SWITCH_PAUSE), 66);
    EXPECT_EQ(static_cast<int>(MessageType::MONITOR_AUTO_SWITCH_RESUME), 67);
    EXPECT_EQ(static_cast<int>(MessageType::MONITOR_AUTO_SWITCH_CONFIG), 68);
    EXPECT_EQ(static_cast<int>(MessageType::MONITOR_AUTO_SWITCH_STATUS), 69);
    EXPECT_EQ(static_cast<int>(MessageType::MONITOR_THUMBNAIL_REQUEST), 70);
    EXPECT_EQ(static_cast<int>(MessageType::MONITOR_THUMBNAIL_FRAME), 71);
}

// ========== ProtocolManager Encode/Decode Tests ==========

TEST_F(RemoteControllerMonitorTest, MessageEncodeDecodeRoundtrip) {
    QByteArray testPayload = "test payload data";
    QByteArray encoded = ProtocolManager::encode(MessageType::MONITOR_SWITCH, testPayload);
    
    EXPECT_FALSE(encoded.isEmpty());
    
    MessageType type;
    QByteArray decodedPayload;
    QString decodedSessionId;
    
    bool ok = ProtocolManager::decode(encoded, type, decodedPayload, decodedSessionId);
    EXPECT_TRUE(ok);
    EXPECT_EQ(type, MessageType::MONITOR_SWITCH);
    EXPECT_EQ(decodedPayload, testPayload);
}

// ========== Edge Case Tests ==========

TEST_F(RemoteControllerMonitorTest, MultipleRapidCalls) {
    for (int i = 0; i < 10; ++i) {
        m_controller->requestMonitorList();
        m_controller->switchMonitor(i);
        m_controller->requestMonitorRefresh();
        m_controller->startAutoSwitch();
        m_controller->stopAutoSwitch();
        m_controller->pauseAutoSwitch();
        m_controller->resumeAutoSwitch();
        m_controller->setAutoSwitchInterval(1000 + i * 100);
        m_controller->requestThumbnailFrame(0, i, 100, 100);
    }
    EXPECT_TRUE(true);
}

// ========== DeviceInfo ArpStatus Tests ==========

TEST_F(RemoteControllerMonitorTest, DeviceInfoArpStatusValues) {
    EXPECT_EQ(static_cast<int>(DeviceInfo::ArpStatus::Unknown), 0);
    EXPECT_EQ(static_cast<int>(DeviceInfo::ArpStatus::Pending), 1);
    EXPECT_EQ(static_cast<int>(DeviceInfo::ArpStatus::Resolved), 2);
    EXPECT_EQ(static_cast<int>(DeviceInfo::ArpStatus::Timeout), 3);
}

// ========== DeviceInfo Default Values ==========

TEST_F(RemoteControllerMonitorTest, DeviceInfoDefaultValues) {
    DeviceInfo info;
    EXPECT_TRUE(info.deviceId.isEmpty());
    EXPECT_TRUE(info.deviceName.isEmpty());
    EXPECT_TRUE(info.ipAddress.isEmpty());
    EXPECT_EQ(info.port, DEFAULT_PORT);
    EXPECT_TRUE(info.accessCode.isEmpty());
    EXPECT_TRUE(info.macAddress.isEmpty());
    EXPECT_EQ(info.arpStatus, DeviceInfo::ArpStatus::Unknown);
    EXPECT_EQ(info.timestamp, 0);
}

// ========== MonitorInfo Tests ==========

TEST_F(RemoteControllerMonitorTest, MonitorInfoDefaultValues) {
    MonitorInfo info;
    EXPECT_EQ(info.index, 0);
    EXPECT_TRUE(info.name.isEmpty());
    EXPECT_EQ(info.width, 0);
    EXPECT_EQ(info.height, 0);
    EXPECT_FALSE(info.isPrimary);
    EXPECT_EQ(info.x, 0);
    EXPECT_EQ(info.y, 0);
}

TEST_F(RemoteControllerMonitorTest, MonitorInfoFieldAssignment) {
    MonitorInfo info;
    info.index = 5;
    info.name = "TestMonitor";
    info.width = 1920;
    info.height = 1080;
    info.isPrimary = true;
    info.x = 100;
    info.y = 200;
    
    EXPECT_EQ(info.index, 5);
    EXPECT_EQ(info.name, "TestMonitor");
    EXPECT_EQ(info.width, 1920);
    EXPECT_EQ(info.height, 1080);
    EXPECT_TRUE(info.isPrimary);
    EXPECT_EQ(info.x, 100);
    EXPECT_EQ(info.y, 200);
}
