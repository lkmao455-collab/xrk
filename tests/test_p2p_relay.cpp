#include <gtest/gtest.h>
#include "app/relay_server.h"
#include "app/p2p_manager.h"
#include "core/tcp_connection.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QHostAddress>
#include <QTimer>
#include <QEventLoop>

using namespace xrk;

namespace {
void waitMs(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
} // namespace

// ---- RelayServer: token auth + PUNCH/PEER_ADDR exchange ----

class RelayServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_server = new RelayServer();
        m_server->setSecret("secret123");
        ASSERT_TRUE(m_server->start(0));
        m_port = m_server->port();
    }

    void TearDown() override {
        m_server->stop();
        delete m_server;
    }

    RelayServer* m_server = nullptr;
    quint16 m_port = 0;
};

// Minimal relay client driving the server in tests (no Q_OBJECT needed).
class TestClient {
public:
    TestClient() : m_sock(new QTcpSocket()) {
        QObject::connect(m_sock, &QTcpSocket::readyRead, [this]() { onReady(); });
    }
    ~TestClient() { delete m_sock; }

    void connectTo(quint16 port, const QString& id, const QString& token) {
        m_id = id;
        m_port = port;
        m_sock->connectToHost(QHostAddress::LocalHost, port);
        if (m_sock->waitForConnected(2000)) {
            sendLine("REGISTER " + id + " " + token);
        }
    }
    void punch(const QString& target) { sendLine("PUNCH " + target); }
    void hello() {
        QUdpSocket udp;
        udp.writeDatagram(("HELLO " + m_id + "\n").toUtf8(),
                           QHostAddress::LocalHost, m_port);
    }
    QStringList messages() const { return m_messages; }
    void clear() { m_messages.clear(); }

private:
    void sendLine(const QString& line) {
        m_sock->write((line + "\n").toUtf8());
        m_sock->flush();
    }
    void onReady() {
        m_buffer.append(m_sock->readAll());
        int pos;
        while ((pos = m_buffer.indexOf('\n')) >= 0) {
            QString line = QString::fromUtf8(m_buffer.left(pos)).trimmed();
            m_buffer.remove(0, pos + 1);
            if (!line.isEmpty()) m_messages.append(line);
        }
    }
    QTcpSocket* m_sock = nullptr;
    QString m_id;
    quint16 m_port = 0;
    QByteArray m_buffer;
    QStringList m_messages;
};

TEST_F(RelayServerTest, TokenAuthRejectsBadToken) {
    TestClient a;
    a.connectTo(m_port, "host_a", "wrong");
    waitMs(300);
    EXPECT_TRUE(a.messages().contains("ERROR AUTH_FAILED"));
}

TEST_F(RelayServerTest, TokenAuthAcceptsGoodToken) {
    TestClient a;
    a.connectTo(m_port, "host_a", "secret123");
    waitMs(300);
    EXPECT_TRUE(a.messages().contains("REGISTERED"));
}

TEST_F(RelayServerTest, PunchExchangesPeerAddr) {
    TestClient a, b;
    a.connectTo(m_port, "host_a", "secret123");
    b.connectTo(m_port, "host_b", "secret123");
    waitMs(100);
    a.hello();
    b.hello();
    waitMs(100);
    a.clear();
    b.clear();
    a.punch("host_b");
    waitMs(300);

    // Locate the PEER_ADDR lines exchanged in both directions.
    // Format: "PEER_ADDR <peerId> <ip> <port>" (ip may be reported as the
    // IPv4-mapped form, e.g. ::ffff:127.0.0.1, on dual-stack hosts).
    auto findPeerAddr = [](const QStringList& msgs, const QString& peerId) -> QString {
        for (const QString& m : msgs) {
            QStringList p = m.split(' ');
            if (p.size() >= 4 && p[0] == "PEER_ADDR" && p[1] == peerId)
                return m;
        }
        return QString();
    };
    QString aLine = findPeerAddr(a.messages(), "host_b");
    QString bLine = findPeerAddr(b.messages(), "host_a");
    ASSERT_FALSE(aLine.isEmpty()) << "host_a did not receive PEER_ADDR for host_b";
    ASSERT_FALSE(bLine.isEmpty()) << "host_b did not receive PEER_ADDR for host_a";

    QStringList ap = aLine.split(' ');
    QStringList bp = bLine.split(' ');
    EXPECT_EQ(ap[3].toUShort(), static_cast<uint>(P2P_PORT));
    EXPECT_EQ(bp[3].toUShort(), static_cast<uint>(P2P_PORT));
    EXPECT_TRUE(QHostAddress(ap[2]).isLoopback());
    EXPECT_TRUE(QHostAddress(bp[2]).isLoopback());
}

// ---- P2PManager: direct connect + timeout fallback ----

class P2PManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_server = new QTcpServer();
        m_server->listen(QHostAddress::LocalHost);
        m_p2p = new P2PManager();
        QObject::connect(m_p2p, &P2PManager::directConnectionEstablished,
                         [this](QTcpSocket* s) { m_direct = s; m_gotDirect = true; });
        QObject::connect(m_p2p, &P2PManager::punchFailed,
                         [this]() { m_gotFailed = true; });
    }
    void TearDown() override {
        delete m_p2p;
        delete m_server;
    }

    QTcpServer* m_server = nullptr;
    P2PManager* m_p2p = nullptr;
    QTcpSocket* m_direct = nullptr;
    bool m_gotDirect = false;
    bool m_gotFailed = false;
};

TEST_F(P2PManagerTest, ConnectsToListeningPeer) {
    m_p2p->beginPunch(QHostAddress::LocalHost, m_server->serverPort());
    waitMs(1000);
    EXPECT_TRUE(m_gotDirect);
    EXPECT_FALSE(m_gotFailed);
    if (m_direct) m_direct->deleteLater();
}

TEST_F(P2PManagerTest, FailsToClosedPort) {
    m_p2p->beginPunch(QHostAddress::LocalHost, 1); // privileged port: nothing listening
    waitMs(6000);
    EXPECT_TRUE(m_gotFailed);
}
