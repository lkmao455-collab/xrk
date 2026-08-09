#include <gtest/gtest.h>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QThread>
#include "app/relay_server.h"
#include "app/device_registry.h"

using namespace xrk;

class RelayServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        server = new RelayServer();
        // Use port 0 for OS-assigned port
        ASSERT_TRUE(server->start(0));
        port = server->port();
        ASSERT_GT(port, 0);
    }
    void TearDown() override {
        delete server;
    }

    QTcpSocket* connectClient() {
        auto* sock = new QTcpSocket();
        sock->connectToHost(QHostAddress::LocalHost, port);
        if (!sock->waitForConnected(2000)) { delete sock; return nullptr; }
        return sock;
    }

    QString readResponse(QTcpSocket* sock, int timeoutMs = 2000) {
        if (!sock->waitForReadyRead(timeoutMs)) return QString();
        return QString::fromUtf8(sock->readLine()).trimmed();
    }

    void registerPeer(QTcpSocket* sock, const QString& deviceId, const QString& token = QString()) {
        QString cmd = "REGISTER " + deviceId;
        if (!token.isEmpty()) cmd += " " + token;
        sock->write((cmd + "\n").toUtf8());
        sock->flush();
    }

    RelayServer* server = nullptr;
    quint16 port = 0;
};

TEST_F(RelayServerTest, StartAndStop) {
    EXPECT_TRUE(server->isRunning());
    EXPECT_GT(server->port(), 0);
    server->stop();
    EXPECT_FALSE(server->isRunning());
}

TEST_F(RelayServerTest, DoubleStart) {
    // Should return true without issues
    EXPECT_TRUE(server->start(port));
}

TEST_F(RelayServerTest, RegisterSuccess) {
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    registerPeer(sock, "peer-1");
    QString resp = readResponse(sock);
    EXPECT_EQ(resp, "REGISTERED");

    delete sock;
}

TEST_F(RelayServerTest, RegisterWithToken) {
    server->setSecret("mysecret123");
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    registerPeer(sock, "peer-1", "mysecret123");
    QString resp = readResponse(sock);
    EXPECT_EQ(resp, "REGISTERED");

    delete sock;
}

TEST_F(RelayServerTest, RegisterAuthFailed) {
    server->setSecret("mysecret123");
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    registerPeer(sock, "peer-1", "wrongsecret");
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.startsWith("ERROR"));

    delete sock;
}

TEST_F(RelayServerTest, RegisterEmptyId) {
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("REGISTER \n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.startsWith("ERROR"));

    delete sock;
}

TEST_F(RelayServerTest, RegisterDuplicateKicksOld) {
    auto* sock1 = connectClient();
    auto* sock2 = connectClient();
    ASSERT_NE(sock1, nullptr);
    ASSERT_NE(sock2, nullptr);

    registerPeer(sock1, "peer-dup");
    readResponse(sock1);

    registerPeer(sock2, "peer-dup");
    QString resp2 = readResponse(sock2);
    EXPECT_EQ(resp2, "REGISTERED");

    // sock1 should receive KICKED
    QString resp1 = readResponse(sock1, 500);
    EXPECT_EQ(resp1, "KICKED");

    delete sock1;
    delete sock2;
}

TEST_F(RelayServerTest, ConnectToPeer) {
    auto* sock1 = connectClient();
    auto* sock2 = connectClient();
    ASSERT_NE(sock1, nullptr);
    ASSERT_NE(sock2, nullptr);

    registerPeer(sock1, "peer-a");
    readResponse(sock1);
    registerPeer(sock2, "peer-b");
    readResponse(sock2);

    // peer-a requests bridge to peer-b
    sock1->write("CONNECT peer-b\n");
    sock1->flush();

    QString resp1 = readResponse(sock1);
    EXPECT_TRUE(resp1.startsWith("BRIDGED"));

    // peer-b receives BRIDGE notification
    QString resp2 = readResponse(sock2);
    EXPECT_TRUE(resp2.startsWith("BRIDGE"));

    delete sock1;
    delete sock2;
}

TEST_F(RelayServerTest, ConnectNotRegistered) {
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("CONNECT peer-x\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("NOT_REGISTERED"));

    delete sock;
}

TEST_F(RelayServerTest, ConnectPeerNotFound) {
    auto* sock1 = connectClient();
    auto* sock2 = connectClient();
    ASSERT_NE(sock1, nullptr);
    ASSERT_NE(sock2, nullptr);

    registerPeer(sock1, "peer-1");
    readResponse(sock1);

    sock1->write("CONNECT peer-nonexistent\n");
    sock1->flush();
    QString resp = readResponse(sock1);
    EXPECT_TRUE(resp.contains("PEER_NOT_FOUND"));

    delete sock1;
    delete sock2;
}

TEST_F(RelayServerTest, ListPeers) {
    auto* sock1 = connectClient();
    auto* sock2 = connectClient();
    ASSERT_NE(sock1, nullptr);
    ASSERT_NE(sock2, nullptr);

    registerPeer(sock1, "peer-x");
    readResponse(sock1);
    registerPeer(sock2, "peer-y");
    readResponse(sock2);

    sock1->write("LIST\n");
    sock1->flush();
    QString resp = readResponse(sock1);
    EXPECT_TRUE(resp.startsWith("PEERS"));
    EXPECT_TRUE(resp.contains("peer-x"));
    EXPECT_TRUE(resp.contains("peer-y"));

    delete sock1;
    delete sock2;
}

TEST_F(RelayServerTest, ListEmpty) {
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("LIST\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.startsWith("PEERS"));

    delete sock;
}

TEST_F(RelayServerTest, PUNCHExchange) {
    auto* sock1 = connectClient();
    auto* sock2 = connectClient();
    ASSERT_NE(sock1, nullptr);
    ASSERT_NE(sock2, nullptr);

    registerPeer(sock1, "peer-a");
    readResponse(sock1);
    registerPeer(sock2, "peer-b");
    readResponse(sock2);

    // peer-a requests PUNCH to peer-b
    sock1->write("PUNCH peer-b\n");
    sock1->flush();

    // Both should receive PEER_ADDR
    QString resp1 = readResponse(sock1);
    EXPECT_TRUE(resp1.contains("PEER_ADDR"));

    QString resp2 = readResponse(sock2);
    EXPECT_TRUE(resp2.contains("PEER_ADDR"));

    delete sock1;
    delete sock2;
}

TEST_F(RelayServerTest, PUNCHNotRegistered) {
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("PUNCH peer-x\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("NOT_REGISTERED"));

    delete sock;
}

TEST_F(RelayServerTest, PUNCHSamePeer) {
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    registerPeer(sock, "peer-self");
    readResponse(sock);

    sock->write("PUNCH peer-self\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("SAME_PEER"));

    delete sock;
}

TEST_F(RelayServerTest, PUNCHPeerNotFound) {
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    registerPeer(sock, "peer-1");
    readResponse(sock);

    sock->write("PUNCH peer-ghost\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("PEER_NOT_FOUND"));

    delete sock;
}

TEST_F(RelayServerTest, UnknownCommand) {
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("FOOBAR\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("UNKNOWN"));

    delete sock;
}

TEST_F(RelayServerTest, SignalPeerConnected) {
    QSignalSpy spy(server, &RelayServer::peerConnected);

    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);
    registerPeer(sock, "sig-peer");
    readResponse(sock);

    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), "sig-peer");

    delete sock;
}

TEST_F(RelayServerTest, SignalPeerDisconnected) {
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);
    registerPeer(sock, "disc-peer");
    readResponse(sock);

    QSignalSpy spy(server, &RelayServer::peerDisconnected);
    sock->close();
    // Give time for disconnect processing
    QThread::msleep(200);
    QCoreApplication::processEvents();

    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), "disc-peer");
}

TEST_F(RelayServerTest, ConnectedPeers) {
    auto* sock1 = connectClient();
    auto* sock2 = connectClient();
    ASSERT_NE(sock1, nullptr);
    ASSERT_NE(sock2, nullptr);

    registerPeer(sock1, "cp-1");
    readResponse(sock1);
    registerPeer(sock2, "cp-2");
    readResponse(sock2);

    QStringList peers = server->connectedPeers();
    EXPECT_TRUE(peers.contains("cp-1"));
    EXPECT_TRUE(peers.contains("cp-2"));

    delete sock1;
    delete sock2;
}

TEST_F(RelayServerTest, BridgeForwardData) {
    auto* sock1 = connectClient();
    auto* sock2 = connectClient();
    ASSERT_NE(sock1, nullptr);
    ASSERT_NE(sock2, nullptr);

    registerPeer(sock1, "bf-a");
    readResponse(sock1);
    registerPeer(sock2, "bf-b");
    readResponse(sock2);

    // Bridge
    sock1->write("CONNECT bf-b\n");
    sock1->flush();
    readResponse(sock1); // BRIDGED
    readResponse(sock2); // BRIDGE

    // Send raw data through bridge
    QByteArray testData = "Hello bridge!";
    sock1->write(testData);
    sock1->flush();

    // sock2 should receive the data
    ASSERT_TRUE(sock2->waitForReadyRead(2000));
    QByteArray received = sock2->readAll();
    EXPECT_TRUE(received.contains(testData));

    delete sock1;
    delete sock2;
}

TEST_F(RelayServerTest, DeviceRegistryNotAvailable) {
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    // Without device registry, device commands should fail
    sock->write("DEVICE_REGISTER dev-1\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("DEVICE_REGISTRY_NOT_AVAILABLE"));

    delete sock;
}

TEST_F(RelayServerTest, DeviceRegisterWithRegistry) {
    DeviceRegistry registry;
    server->setDeviceRegistry(&registry);

    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("DEVICE_REGISTER dev-1 MyPC 192.168.1.1 9999 1.0.0\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("DEVICE_REGISTERED"));

    // Verify device is in registry
    EXPECT_TRUE(registry.containsDevice("dev-1"));

    delete sock;
}

TEST_F(RelayServerTest, DeviceListWithRegistry) {
    DeviceRegistry registry;
    server->setDeviceRegistry(&registry);

    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    // Register a device first
    sock->write("DEVICE_REGISTER dev-1 MyPC 192.168.1.1 9999 1.0.0\n");
    sock->flush();
    readResponse(sock);

    // List devices
    sock->write("DEVICE_LIST\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("DEVICE_LIST"));

    delete sock;
}

TEST_F(RelayServerTest, DeviceRemoveWithRegistry) {
    DeviceRegistry registry;
    server->setDeviceRegistry(&registry);

    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    // Register then remove
    sock->write("DEVICE_REGISTER dev-rm\n");
    sock->flush();
    readResponse(sock);

    sock->write("DEVICE_REMOVE dev-rm\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("DEVICE_REMOVED"));

    EXPECT_FALSE(registry.containsDevice("dev-rm"));

    delete sock;
}

TEST_F(RelayServerTest, DeviceQueryWithRegistry) {
    DeviceRegistry registry;
    server->setDeviceRegistry(&registry);

    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("DEVICE_REGISTER dev-q QueryDevice\n");
    sock->flush();
    readResponse(sock);

    sock->write("DEVICE_QUERY dev-q\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("DEVICE_INFO"));

    delete sock;
}

TEST_F(RelayServerTest, DeviceQueryNotFound) {
    DeviceRegistry registry;
    server->setDeviceRegistry(&registry);

    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("DEVICE_QUERY dev-ghost\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("DEVICE_NOT_FOUND"));

    delete sock;
}

TEST_F(RelayServerTest, DeviceRemoveNotFound) {
    DeviceRegistry registry;
    server->setDeviceRegistry(&registry);

    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("DEVICE_REMOVE dev-ghost\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("DEVICE_NOT_FOUND"));

    delete sock;
}

TEST_F(RelayServerTest, DeviceListOnlineFilter) {
    DeviceRegistry registry;
    server->setDeviceRegistry(&registry);

    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("DEVICE_REGISTER dev-on\n");
    sock->flush();
    readResponse(sock);

    sock->write("DEVICE_LIST online\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("DEVICE_LIST"));

    delete sock;
}

TEST_F(RelayServerTest, DeviceListSearchFilter) {
    DeviceRegistry registry;
    server->setDeviceRegistry(&registry);

    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("DEVICE_REGISTER dev-s MyLaptop\n");
    sock->flush();
    readResponse(sock);

    sock->write("DEVICE_LIST search Laptop\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("DEVICE_LIST"));

    delete sock;
}

TEST_F(RelayServerTest, DeviceUpdateWithRegistry) {
    DeviceRegistry registry;
    server->setDeviceRegistry(&registry);

    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    // Register device
    sock->write("DEVICE_REGISTER dev-upd\n");
    sock->flush();
    readResponse(sock);

    // Update device
    QJsonObject updateObj;
    updateObj["deviceName"] = "UpdatedName";
    QByteArray jsonB64 = QJsonDocument(updateObj).toJson(QJsonDocument::Compact).toBase64();
    sock->write(("DEVICE_UPDATE dev-upd " + jsonB64 + "\n").toUtf8());
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("DEVICE_UPDATED"));

    delete sock;
}

TEST_F(RelayServerTest, DeviceUpdateInvalidJson) {
    DeviceRegistry registry;
    server->setDeviceRegistry(&registry);

    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("DEVICE_UPDATE dev-x notbase64==\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("ERROR"));

    delete sock;
}

TEST_F(RelayServerTest, DeviceRegisterNoRegistry) {
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("DEVICE_REGISTER dev-1\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("DEVICE_REGISTRY_NOT_AVAILABLE"));

    delete sock;
}

TEST_F(RelayServerTest, DeviceListNoRegistry) {
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("DEVICE_LIST\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("DEVICE_REGISTRY_NOT_AVAILABLE"));

    delete sock;
}

TEST_F(RelayServerTest, DeviceUpdateNoRegistry) {
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("DEVICE_UPDATE dev-x dGVzdA==\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("DEVICE_REGISTRY_NOT_AVAILABLE"));

    delete sock;
}

TEST_F(RelayServerTest, DeviceRemoveNoRegistry) {
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("DEVICE_REMOVE dev-x\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("DEVICE_REGISTRY_NOT_AVAILABLE"));

    delete sock;
}

TEST_F(RelayServerTest, DeviceQueryNoRegistry) {
    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("DEVICE_QUERY dev-x\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("DEVICE_REGISTRY_NOT_AVAILABLE"));

    delete sock;
}

TEST_F(RelayServerTest, HeartbeatWithRegistry) {
    DeviceRegistry registry;
    server->setDeviceRegistry(&registry);

    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    // Register device
    sock->write("DEVICE_REGISTER hb-dev\n");
    sock->flush();
    readResponse(sock);

    sock->write("HEARTBEAT hb-dev\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_EQ(resp, "HEARTBEAT_OK");

    delete sock;
}

TEST_F(RelayServerTest, HeartbeatDeviceNotFound) {
    DeviceRegistry registry;
    server->setDeviceRegistry(&registry);

    auto* sock = connectClient();
    ASSERT_NE(sock, nullptr);

    sock->write("HEARTBEAT ghost-dev\n");
    sock->flush();
    QString resp = readResponse(sock);
    EXPECT_TRUE(resp.contains("DEVICE_NOT_FOUND"));

    delete sock;
}
