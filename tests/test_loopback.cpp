// Loopback connection self-test: proves a RemoteController (the client software
// that runs on the *connecting* PC) can connect over TCP to a listening host on
// 127.0.0.1, complete the full AUTH handshake (including encryption-key
// exchange), and do a real request/response round-trip (system info).
//
// The host side here is a *protocol-correct minimal stub* built from the very
// same ProtocolManager/MessageCodec the production Host uses to frame, checksum
// and (de)serialize messages. We avoid instantiating the full Host media
// pipeline (capture/encode/audio threads) because its real-time threading model
// is only safe to tear down inside the GUI app; everything below the framing
// layer is identical to production. No GUI required — runs under QCoreApplication.
#include <gtest/gtest.h>

#include "app/remote_controller.h"
#include "app/session_manager.h"
#include "core/network_manager.h"
#include "core/protocol_manager.h"
#include "core/message_codec.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QRandomGenerator>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <functional>

using namespace xrk;

namespace {
// Pump the event loop until `predicate()` is true or `timeoutMs` elapses.
bool waitFor(std::function<bool()> predicate, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (!predicate() && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    return predicate();
}

QByteArray randomBytes(int n) {
    QByteArray b;
    b.resize(n);
    for (int i = 0; i < n; ++i) b[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    return b;
}
} // namespace

// Minimal, protocol-correct host: listens on TCP, frames incoming bytes with
// MessageCodec exactly like the production Host, and answers AUTH_REQ /
// SYSINFO_REQ with ProtocolManager-encoded responses.
class StubHost : public QObject {
    Q_OBJECT
public:
    explicit StubHost(const QString& password, QObject* parent = nullptr)
        : QObject(parent), m_password(password) {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &StubHost::onNewConnection);
    }
    bool start(quint16 port) { return m_server->listen(QHostAddress::Any, port); }
    ~StubHost() override { m_server->close(); }

private slots:
    void onNewConnection() {
        QTcpSocket* sock = m_server->nextPendingConnection();
        if (!sock) return;
        m_buffers.insert(sock, QByteArray());
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() { onReady(sock); });
        connect(sock, &QTcpSocket::disconnected, this, [sock]() {
            sock->deleteLater();
        });
    }

    void onReady(QTcpSocket* sock) {
        m_buffers[sock].append(sock->readAll());
        QByteArray& buf = m_buffers[sock];

        while (true) {
            MessageHeader header;
            uint32_t sidLen = 0;
            if (!MessageCodec::parseHeader(buf, header, sidLen)) break;
            size_t total = MessageCodec::HEADER_FIXED_SIZE + 4 + sidLen + header.length
                           + MessageCodec::CHECKSUM_SIZE;
            if (static_cast<size_t>(buf.size()) < total) break;

            QByteArray frame = buf.left(static_cast<int>(total));
            buf = buf.mid(static_cast<int>(total));

            MessageType type{};
            QByteArray payload;
            QString sid;
            if (MessageCodec::decode(frame, type, payload, sid)) handle(type, payload, sock);
        }
    }

    void handle(MessageType type, const QByteArray& payload, QTcpSocket* sock) {
        if (type == MessageType::AUTH_REQ) {
            QByteArray resp("FAILED");
            if (QString::fromUtf8(payload) == m_password) {
                // "OK" + 32-byte AES key + 16-byte IV + 1 pad (51 bytes total so
                // the client actually initializes its encryption context).
                resp = QByteArray("OK") + randomBytes(32) + randomBytes(16) + QByteArray(1, 'X');
            }
            sock->write(ProtocolManager::encode(MessageType::AUTH_RESP, resp));
        } else if (type == MessageType::SYSINFO_REQ) {
            SysInfo info;
            info.osName = "LoopbackOS";
            info.osVersion = "1.0";
            info.cpuName = "TestCPU";
            info.cpuUsage = 12.5;
            info.memoryTotal = 16ULL * 1024 * 1024 * 1024;
            info.memoryUsage = 40.0;
            sock->write(ProtocolManager::encode(
                MessageType::SYSINFO_RESP, ProtocolManager::encodeSysInfo(info)));
        }
    }

private:
    QString m_password;
    QTcpServer* m_server;
    QHash<QTcpSocket*, QByteArray> m_buffers;
};

class LoopbackConnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_host = new StubHost(kPassword, nullptr);
        ASSERT_TRUE(m_host->start(kPort)) << "Stub host failed to listen on port " << kPort;

        m_network = new NetworkManager();
        ASSERT_TRUE(m_network->initialize(0)) << "NetworkManager init failed";
        m_session = new SessionManager();
        m_controller = new RemoteController(m_network, m_session);
    }

    void TearDown() override {
        delete m_controller;
        delete m_session;
        m_network->shutdown();
        delete m_network;
        delete m_host;
    }

    StubHost* m_host = nullptr;
    NetworkManager* m_network = nullptr;
    SessionManager* m_session = nullptr;
    RemoteController* m_controller = nullptr;

    static constexpr uint16_t kPort = 19999;
    static constexpr char kPassword[] = "loopback123";
};

TEST_F(LoopbackConnectionTest, ConnectAndAuthenticate) {
    bool authOk = false;
    bool transportOk = false;
    QObject::connect(m_controller, &RemoteController::authSuccess, [&]() { authOk = true; });
    QObject::connect(m_controller, &RemoteController::transportEstablished,
                     [&](TransportType) { transportOk = true; });

    ASSERT_TRUE(m_controller->startRemote("127.0.0.1", kPort, kPassword));
    EXPECT_TRUE(waitFor([&]() { return authOk; }, 5000))
        << "Client never received authSuccess within 5s";
    EXPECT_TRUE(transportOk) << "transportEstablished was not emitted";
    EXPECT_TRUE(m_controller->isRemoteActive()) << "remote session not active after auth";
}

TEST_F(LoopbackConnectionTest, WrongPasswordRejected) {
    bool authFailed = false;
    bool authOk = false;
    QObject::connect(m_controller, &RemoteController::authFailed,
                     [&](const QString&) { authFailed = true; });
    QObject::connect(m_controller, &RemoteController::authSuccess, [&]() { authOk = true; });

    ASSERT_TRUE(m_controller->startRemote("127.0.0.1", kPort, "definitely-wrong"));
    EXPECT_TRUE(waitFor([&]() { return authFailed; }, 5000))
        << "Wrong password was not rejected";
    EXPECT_FALSE(authOk) << "authSuccess fired despite wrong password";
}

TEST_F(LoopbackConnectionTest, SystemInfoRoundTrip) {
    bool authOk = false;
    QObject::connect(m_controller, &RemoteController::authSuccess, [&]() { authOk = true; });
    ASSERT_TRUE(m_controller->startRemote("127.0.0.1", kPort, kPassword));
    ASSERT_TRUE(waitFor([&]() { return authOk; }, 5000)) << "auth did not succeed";

    bool gotSysInfo = false;
    QString osName;
    QObject::connect(m_controller, &RemoteController::sysInfoReceived,
                     [&](const SysInfo& info) {
                         gotSysInfo = true;
                         osName = info.osName;
                     });

    m_controller->requestSystemInfo();
    EXPECT_TRUE(waitFor([&]() { return gotSysInfo; }, 5000))
        << "Host did not answer SYSINFO_REQ — bidirectional channel broken";
    EXPECT_EQ(osName, "LoopbackOS");
}

#include "test_loopback.moc"
