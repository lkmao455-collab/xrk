#include <gtest/gtest.h>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QCoreApplication>
#include "core/tcp_connection.h"
#include "core/types.h"

using namespace xrk;

class TcpConnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        server = new QTcpServer();
        ASSERT_TRUE(server->listen(QHostAddress::LocalHost, 0));
        port = server->serverPort();
    }
    void TearDown() override {
        delete server;
    }

    QTcpSocket* acceptClient() {
        if (!server->waitForNewConnection(2000)) return nullptr;
        return server->nextPendingConnection();
    }

    TcpConnection* connectClient() {
        auto* sock = new QTcpSocket();
        sock->connectToHost(QHostAddress::LocalHost, port);
        if (!sock->waitForConnected(2000)) { delete sock; return nullptr; }
        auto* hostSock = acceptClient();
        if (!hostSock) { delete sock; return nullptr; }
        return new TcpConnection(hostSock);
    }

    QTcpServer* server = nullptr;
    quint16 port = 0;
};

TEST_F(TcpConnectionTest, ConstructWithSocket) {
    auto* sock = new QTcpSocket();
    sock->connectToHost(QHostAddress::LocalHost, port);
    ASSERT_TRUE(sock->waitForConnected(2000));
    auto* hostSock = acceptClient();
    ASSERT_NE(hostSock, nullptr);

    TcpConnection conn(hostSock);
    EXPECT_TRUE(conn.isConnected());
    EXPECT_EQ(conn.state(), ConnectionState::Connected);
}

TEST_F(TcpConnectionTest, DeviceId) {
    auto* conn = connectClient();
    ASSERT_NE(conn, nullptr);
    EXPECT_TRUE(conn->deviceId().isEmpty());

    conn->setDeviceId("dev-123");
    EXPECT_EQ(conn->deviceId(), "dev-123");
    delete conn;
}

TEST_F(TcpConnectionTest, SendAndReceive) {
    auto* clientSock = new QTcpSocket();
    clientSock->connectToHost(QHostAddress::LocalHost, port);
    ASSERT_TRUE(clientSock->waitForConnected(2000));
    auto* hostSock = acceptClient();
    ASSERT_NE(hostSock, nullptr);

    TcpConnection conn(hostSock);

    QByteArray testData = "Hello, TcpConnection!";
    conn.send(testData);
    ASSERT_TRUE(clientSock->waitForReadyRead(2000));

    QByteArray received = clientSock->readAll();
    EXPECT_FALSE(received.isEmpty());
    EXPECT_TRUE(received.contains(testData));

    delete clientSock;
}

TEST_F(TcpConnectionTest, IsConnected) {
    auto* conn = connectClient();
    ASSERT_NE(conn, nullptr);
    EXPECT_TRUE(conn->isConnected());
    delete conn;
}

TEST_F(TcpConnectionTest, BytesWritten) {
    auto* clientSock = new QTcpSocket();
    clientSock->connectToHost(QHostAddress::LocalHost, port);
    ASSERT_TRUE(clientSock->waitForConnected(2000));
    auto* hostSock = acceptClient();
    ASSERT_NE(hostSock, nullptr);

    TcpConnection conn(hostSock);

    QByteArray data(1024, 'A');
    conn.send(data);
    ASSERT_TRUE(clientSock->waitForReadyRead(2000));

    QByteArray received = clientSock->readAll();
    EXPECT_EQ(received.size(), 1024);

    delete clientSock;
}

TEST_F(TcpConnectionTest, BytesAvailable) {
    auto* conn = connectClient();
    ASSERT_NE(conn, nullptr);

    EXPECT_GE(conn->bytesAvailable(), 0);
    delete conn;
}

TEST_F(TcpConnectionTest, StateTracking) {
    auto* clientSock = new QTcpSocket();
    clientSock->connectToHost(QHostAddress::LocalHost, port);
    ASSERT_TRUE(clientSock->waitForConnected(2000));
    auto* hostSock = acceptClient();
    ASSERT_NE(hostSock, nullptr);

    TcpConnection conn(hostSock);
    EXPECT_EQ(conn.state(), ConnectionState::Connected);

    delete clientSock;
}

TEST_F(TcpConnectionTest, HistoryEmpty) {
    auto* conn = connectClient();
    ASSERT_NE(conn, nullptr);
    // History may or may not have entries depending on timing
    conn->clearHistory();
    EXPECT_TRUE(conn->history().isEmpty());
    delete conn;
}

TEST_F(TcpConnectionTest, ClearHistory) {
    auto* conn = connectClient();
    ASSERT_NE(conn, nullptr);
    conn->clearHistory();
    EXPECT_TRUE(conn->history().isEmpty());
    delete conn;
}

TEST_F(TcpConnectionTest, ReconnectDisabled) {
    auto* conn = connectClient();
    ASSERT_NE(conn, nullptr);
    EXPECT_FALSE(conn->isReconnectEnabled());
    conn->setReconnectEnabled(true);
    EXPECT_TRUE(conn->isReconnectEnabled());
    conn->setReconnectEnabled(false);
    EXPECT_FALSE(conn->isReconnectEnabled());
    delete conn;
}

TEST_F(TcpConnectionTest, ReconnectConfig) {
    auto* conn = connectClient();
    ASSERT_NE(conn, nullptr);
    // Just verify no crash when setting config
    conn->setReconnectInterval(5000);
    conn->setMaxReconnectAttempts(3);
    conn->setReconnectConfig(1000, 30000, 2.0, 20);
    delete conn;
}

TEST_F(TcpConnectionTest, DisconnectFromHost) {
    auto* conn = connectClient();
    ASSERT_NE(conn, nullptr);

    QSignalSpy spy(conn, &TcpConnection::disconnected);
    conn->disconnectFromHost();
    // Give time for disconnect
    QCoreApplication::processEvents();
    QThread::msleep(100);
    QCoreApplication::processEvents();
    delete conn;
}

TEST_F(TcpConnectionTest, InjectData) {
    auto* conn = connectClient();
    ASSERT_NE(conn, nullptr);

    QSignalSpy spy(conn, &TcpConnection::readyRead);
    QByteArray fakeMessage = QByteArray(24, 'H'); // minimal header-sized data
    conn->injectData(fakeMessage);
    // injectData should trigger processing
    QCoreApplication::processEvents();
    // At minimum, no crash
    delete conn;
}

TEST_F(TcpConnectionTest, SendEmptyData) {
    auto* conn = connectClient();
    ASSERT_NE(conn, nullptr);
    // Should not crash
    conn->send(QByteArray());
    delete conn;
}
