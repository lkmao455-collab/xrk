#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSignalSpy>
#include "app/connection_history_manager.h"
#include "core/types.h"

using namespace xrk;

class ConnectionHistoryManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Each test gets a fresh manager with cleared state
        mgr = new ConnectionHistoryManager(nullptr);
        mgr->clearHistory();
    }
    void TearDown() override {
        delete mgr;
    }

    ConnectionRecord makeRecord(const QString& addr, quint16 port, bool success,
                                const QString& device = "TestDevice") {
        ConnectionRecord r;
        r.timestamp = QDateTime::currentDateTime();
        r.hostAddress = addr;
        r.hostPort = port;
        r.deviceName = device;
        r.deviceId = "dev-" + addr;
        r.success = success;
        r.errorMessage = success ? "" : "Connection refused";
        r.duration = success ? 150 : 0;
        r.bytesSent = success ? 1024 : 0;
        r.bytesReceived = success ? 2048 : 0;
        r.reconnectAttempts = success ? 0 : 3;
        return r;
    }

    ConnectionHistoryManager* mgr = nullptr;
};

TEST_F(ConnectionHistoryManagerTest, RecordAndRetrieve) {
    ConnectionRecord r = makeRecord("192.168.1.100", 9999, true);
    mgr->recordConnection(r);

    auto recent = mgr->getRecentConnections();
    ASSERT_EQ(recent.size(), 1);
    EXPECT_EQ(recent[0].hostAddress, "192.168.1.100");
    EXPECT_EQ(recent[0].hostPort, 9999);
    EXPECT_TRUE(recent[0].success);
}

TEST_F(ConnectionHistoryManagerTest, MultipleRecords) {
    mgr->recordConnection(makeRecord("192.168.1.1", 9999, true));
    mgr->recordConnection(makeRecord("192.168.1.2", 9999, false));
    mgr->recordConnection(makeRecord("192.168.1.3", 9999, true));

    auto recent = mgr->getRecentConnections();
    EXPECT_EQ(recent.size(), 3);
    // Most recent first
    EXPECT_EQ(recent[0].hostAddress, "192.168.1.3");
    EXPECT_EQ(recent[1].hostAddress, "192.168.1.2");
    EXPECT_EQ(recent[2].hostAddress, "192.168.1.1");
}

TEST_F(ConnectionHistoryManagerTest, GetRecentConnectionsLimit) {
    for (int i = 0; i < 10; ++i) {
        mgr->recordConnection(makeRecord("10.0.0." + QString::number(i), 9999, true));
    }
    auto recent = mgr->getRecentConnections(3);
    EXPECT_EQ(recent.size(), 3);
}

TEST_F(ConnectionHistoryManagerTest, GetConnectionsForHost) {
    mgr->recordConnection(makeRecord("192.168.1.100", 9999, true));
    mgr->recordConnection(makeRecord("192.168.1.100", 9999, false));
    mgr->recordConnection(makeRecord("192.168.1.200", 9999, true));

    auto results = mgr->getConnectionsForHost("192.168.1.100", 9999);
    EXPECT_EQ(results.size(), 2);

    auto other = mgr->getConnectionsForHost("192.168.1.200", 9999);
    EXPECT_EQ(other.size(), 1);
}

TEST_F(ConnectionHistoryManagerTest, GetSuccessfulConnections) {
    mgr->recordConnection(makeRecord("10.0.0.1", 9999, true));
    mgr->recordConnection(makeRecord("10.0.0.2", 9999, false));
    mgr->recordConnection(makeRecord("10.0.0.3", 9999, true));
    mgr->recordConnection(makeRecord("10.0.0.4", 9999, false));

    auto successes = mgr->getSuccessfulConnections();
    EXPECT_EQ(successes.size(), 2);
    for (const auto& r : successes) {
        EXPECT_TRUE(r.success);
    }
}

TEST_F(ConnectionHistoryManagerTest, GetFailedConnections) {
    mgr->recordConnection(makeRecord("10.0.0.1", 9999, true));
    mgr->recordConnection(makeRecord("10.0.0.2", 9999, false));
    mgr->recordConnection(makeRecord("10.0.0.3", 9999, false));

    auto failures = mgr->getFailedConnections();
    EXPECT_EQ(failures.size(), 2);
    for (const auto& r : failures) {
        EXPECT_FALSE(r.success);
    }
}

TEST_F(ConnectionHistoryManagerTest, ClearHistory) {
    mgr->recordConnection(makeRecord("10.0.0.1", 9999, true));
    mgr->recordConnection(makeRecord("10.0.0.2", 9999, true));

    QSignalSpy spy(mgr, &ConnectionHistoryManager::historyCleared);
    mgr->clearHistory();

    EXPECT_EQ(mgr->getRecentConnections().size(), 0);
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(ConnectionHistoryManagerTest, Statistics) {
    mgr->recordConnection(makeRecord("10.0.0.1", 9999, true));
    ConnectionRecord r2 = makeRecord("10.0.0.2", 9999, false);
    r2.reconnectAttempts = 5;
    mgr->recordConnection(r2);
    ConnectionRecord r3 = makeRecord("10.0.0.3", 9999, true);
    r3.duration = 200;
    r3.bytesSent = 5000;
    r3.bytesReceived = 10000;
    mgr->recordConnection(r3);

    auto stats = mgr->getStatistics();
    EXPECT_EQ(stats.totalConnections, 3);
    EXPECT_EQ(stats.successfulConnections, 2);
    EXPECT_EQ(stats.failedConnections, 1);
    EXPECT_EQ(stats.totalDuration, 350);
    EXPECT_EQ(stats.totalBytesSent, 6024);
    EXPECT_EQ(stats.totalBytesReceived, 12048);
    EXPECT_EQ(stats.maxReconnectAttempts, 5);
}

TEST_F(ConnectionHistoryManagerTest, StatisticsEmpty) {
    auto stats = mgr->getStatistics();
    EXPECT_EQ(stats.totalConnections, 0);
    EXPECT_EQ(stats.successfulConnections, 0);
    EXPECT_EQ(stats.failedConnections, 0);
}

TEST_F(ConnectionHistoryManagerTest, ExportToJson) {
    mgr->recordConnection(makeRecord("10.0.0.1", 9999, true, "MyDevice"));
    QByteArray json = mgr->exportToJson();
    EXPECT_FALSE(json.isEmpty());
    EXPECT_TRUE(json.contains("10.0.0.1"));
    EXPECT_TRUE(json.contains("MyDevice"));
}

TEST_F(ConnectionHistoryManagerTest, ImportFromJson) {
    mgr->recordConnection(makeRecord("10.0.0.1", 9999, true, "Device1"));
    QByteArray json = mgr->exportToJson();

    ConnectionHistoryManager mgr2(nullptr);
    ASSERT_TRUE(mgr2.importFromJson(json));
    auto recent = mgr2.getRecentConnections();
    ASSERT_EQ(recent.size(), 1);
    EXPECT_EQ(recent[0].hostAddress, "10.0.0.1");
    EXPECT_EQ(recent[0].deviceName, "Device1");
}

TEST_F(ConnectionHistoryManagerTest, ImportInvalidJson) {
    EXPECT_FALSE(mgr->importFromJson("not valid json {"));
    EXPECT_FALSE(mgr->importFromJson(""));
}

TEST_F(ConnectionHistoryManagerTest, ImportJsonArray) {
    EXPECT_FALSE(mgr->importFromJson("{\"not\": \"array\"}"));
}

TEST_F(ConnectionHistoryManagerTest, SignalEmitted) {
    QSignalSpy spy(mgr, &ConnectionHistoryManager::connectionRecorded);
    mgr->recordConnection(makeRecord("10.0.0.1", 9999, true));
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(ConnectionHistoryManagerTest, GetRecentConnectionsEmpty) {
    auto recent = mgr->getRecentConnections();
    EXPECT_TRUE(recent.isEmpty());
}

TEST_F(ConnectionHistoryManagerTest, DefaultPort) {
    ConnectionRecord r;
    r.timestamp = QDateTime::currentDateTime();
    r.hostAddress = "10.0.0.1";
    r.hostPort = DEFAULT_PORT;
    r.success = true;
    mgr->recordConnection(r);

    auto results = mgr->getConnectionsForHost("10.0.0.1", DEFAULT_PORT);
    EXPECT_EQ(results.size(), 1);
}
