#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QTcpSocket>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <vector>
#include <cstdio>
#include "host.h"
#include "database_manager.h"
#include "core/protocol_manager.h"
#include "core/message_codec.h"
#include "core/permission_model.h"
#include "core/types.h"

using namespace xrk;

namespace {

// Reads framed XRK messages from a socket while preserving a persistent buffer,
// so interleaved messages (e.g. screen frames that an Operator session receives)
// are skipped rather than consumed/dropped.
struct MessageReader {
    QTcpSocket* sock = nullptr;
    QByteArray buf;

    explicit MessageReader(QTcpSocket* s) : sock(s) {
        QObject::connect(sock, &QTcpSocket::readyRead, [&]() {
            buf.append(sock->readAll());
        });
    }

    // Blocks until a message of `want` arrives, skipping all others. Returns
    // false on timeout. The received payload is written to `out`.
    bool readType(MessageType want, QByteArray& out, int timeoutMs = 6000) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < timeoutMs) {
            while (static_cast<size_t>(buf.size()) >= MessageCodec::MIN_MESSAGE_SIZE) {
                MessageHeader header;
                uint32_t sidLen = 0;
                if (!MessageCodec::parseHeader(buf, header, sidLen)) break;
                size_t total = MessageCodec::HEADER_FIXED_SIZE + 4 + sidLen +
                               header.length + MessageCodec::CHECKSUM_SIZE;
                if (static_cast<size_t>(buf.size()) < total) break;
                QByteArray msg = buf.left(total);
                buf.remove(0, total);
                if (!MessageCodec::verifyChecksum(msg)) continue;
                MessageType type;
                QByteArray payload;
                QString sid;
                if (!ProtocolManager::decode(msg, type, payload, sid)) continue;
                if (type == want) {
                    out = payload;
                    return true;
                }
            }
            QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 50);
        }
        return false;
    }
};

class HostPermissionTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_dir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(m_dir->isValid());
        // A fresh database seeds the default `admin` account + all toggles on.
        ASSERT_TRUE(DatabaseManager::instance().initialize(m_dir->filePath("perm.db")));
        // Add a known Admin account so the privileged message paths can run.
        ASSERT_TRUE(DatabaseManager::instance().addUser("tester", "pw", PermLevel::Admin));

        m_host = std::make_unique<Host>();
        m_host->setAudioEnabled(false);
        const uint16_t port = 19960 + static_cast<uint16_t>(s_portCounter++ % 20);
        ASSERT_TRUE(m_host->start(port));
        m_port = port;
    }

    void TearDown() override {
        if (m_host) { m_host->stop(); m_host.reset(); }
        DatabaseManager::instance().shutdown();
        m_dir.reset();
    }

    // Connect a socket and wait for the auto-grant AUTH_RESP. With no password
    // set the Host grants its default level (Operator) immediately on connect.
    std::unique_ptr<MessageReader> connectOperator() {
        auto sock = std::make_unique<QTcpSocket>();
        sock->connectToHost(QHostAddress::LocalHost, m_port);
        EXPECT_TRUE(sock->waitForConnected(5000));
        auto reader = std::make_unique<MessageReader>(sock.get());
        QByteArray auth;
        EXPECT_TRUE(reader->readType(MessageType::AUTH_RESP, auth));
        m_sockets.push_back(std::move(sock));
        return reader;
    }

    std::unique_ptr<Host> m_host;
    uint16_t m_port = 0;
    std::unique_ptr<QTemporaryDir> m_dir;
    std::vector<std::unique_ptr<QTcpSocket>> m_sockets;
    static int s_portCounter;
};

int HostPermissionTest::s_portCounter = 0;

// ───────────────────────── Pure codec round-trips ─────────────────────────

TEST(PermissionCodecTest, PermissionToggleRequestRoundTrip) {
    QByteArray enc = ProtocolManager::encodePermissionToggleRequest(
        static_cast<uint32_t>(Capability::Terminal), true);
    uint32_t cap = 0; bool enabled = false;
    ASSERT_TRUE(ProtocolManager::decodePermissionToggleRequest(enc, cap, enabled));
    EXPECT_EQ(cap, static_cast<uint32_t>(Capability::Terminal));
    EXPECT_TRUE(enabled);
}

TEST(PermissionCodecTest, PermissionToggleResponseRoundTrip) {
    const uint32_t toggles = kAllCapabilities & ~static_cast<uint32_t>(Capability::Terminal);
    QByteArray enc = ProtocolManager::encodePermissionToggleResponse(toggles);
    EXPECT_EQ(ProtocolManager::decodePermissionToggleResponse(enc), toggles);
}

TEST(PermissionCodecTest, UserMutationRoundTrip) {
    UserMutation m;
    m.username = "alice";
    m.password = "secret";
    m.level = static_cast<uint8_t>(PermLevel::Operator);
    m.enabled = false;
    m.fields = UserMutation::FieldPassword | UserMutation::FieldEnabled;
    QByteArray enc = ProtocolManager::encodeUserMutation(m);
    UserMutation out = ProtocolManager::decodeUserMutation(enc);
    EXPECT_EQ(out.username, "alice");
    EXPECT_EQ(out.password, "secret");
    EXPECT_EQ(out.level, static_cast<uint8_t>(PermLevel::Operator));
    EXPECT_FALSE(out.enabled);
    EXPECT_EQ(out.fields, m.fields);
}

TEST(PermissionCodecTest, DevicePermissionResponseRoundTrip) {
    DevicePermission d;
    d.deviceId = "dev-x";
    d.level = static_cast<int>(PermLevel::Viewer);
    d.capMask = 0x0F;
    d.note = "lobby";
    QByteArray enc = ProtocolManager::encodeDevicePermissionResponse({d});
    QList<DevicePermission> out = ProtocolManager::decodeDevicePermissionResponse(enc);
    ASSERT_EQ(out.size(), 1);
    EXPECT_EQ(out[0].deviceId, "dev-x");
    EXPECT_EQ(out[0].level, static_cast<int>(PermLevel::Viewer));
    EXPECT_EQ(out[0].capMask, 0x0F);
    EXPECT_EQ(out[0].note, "lobby");
}

TEST(PermissionCodecTest, UserListResponseRoundTrip) {
    QList<UserRecord> users = {
        {"admin", static_cast<uint8_t>(PermLevel::Admin), true, 123},
        {"bob",   static_cast<uint8_t>(PermLevel::Operator), true, 456},
    };
    QByteArray enc = ProtocolManager::encodeUserListResponse(users);
    QList<UserRecord> out = ProtocolManager::decodeUserListResponse(enc);
    ASSERT_EQ(out.size(), 2);
    EXPECT_EQ(out[0].username, "admin");
    EXPECT_EQ(out[0].level, static_cast<uint8_t>(PermLevel::Admin));
    EXPECT_EQ(out[1].username, "bob");
    EXPECT_EQ(out[1].lastLogin, 456);
}

TEST(PermissionCodecTest, PermissionDeniedRoundTrip) {
    PermissionDenied d;
    d.capability = static_cast<uint32_t>(Capability::Terminal);
    d.reason = "no terminal";
    QByteArray enc = ProtocolManager::encodePermissionDenied(d);
    PermissionDenied out = ProtocolManager::decodePermissionDenied(enc);
    EXPECT_EQ(out.capability, static_cast<uint32_t>(Capability::Terminal));
    EXPECT_EQ(out.reason, "no terminal");
}

// ─────────────────── Host-level permission enforcement ───────────────────

// An Operator (the default no-password level) must NOT be able to issue
// user-management requests: the Host rejects them with
// PERMISSION_DENIED(capability = UserManage).
TEST_F(HostPermissionTest, OperatorCannotManageUsers) {
    auto reader = connectOperator();
    QTcpSocket* sock = m_sockets.back().get();

    sock->write(ProtocolManager::encode(MessageType::USER_LIST_REQ, QByteArray()));
    sock->flush();
    QByteArray denied;
    ASSERT_TRUE(reader->readType(MessageType::PERMISSION_DENIED, denied));
    PermissionDenied pd = ProtocolManager::decodePermissionDenied(denied);
    EXPECT_EQ(pd.capability, static_cast<uint32_t>(Capability::UserManage));
}

// Operator lacks Terminal (admin-only) but holds SysInfo, so a terminal request
// is denied while a sysinfo request is served normally.
TEST_F(HostPermissionTest, OperatorTerminalDeniedButSysinfoAllowed) {
    auto reader = connectOperator();
    QTcpSocket* sock = m_sockets.back().get();

    sock->write(ProtocolManager::encode(MessageType::TERMINAL_START, QByteArray()));
    sock->flush();
    QByteArray denied;
    ASSERT_TRUE(reader->readType(MessageType::PERMISSION_DENIED, denied));
    PermissionDenied pd = ProtocolManager::decodePermissionDenied(denied);
    EXPECT_EQ(pd.capability, static_cast<uint32_t>(Capability::Terminal));

    sock->write(ProtocolManager::encode(MessageType::SYSINFO_REQ, QByteArray()));
    sock->flush();
    QByteArray sysinfo;
    EXPECT_TRUE(reader->readType(MessageType::SYSINFO_RESP, sysinfo));
}

// An Admin session (authenticated as a user account at Admin level) can list
// users, toggle a host capability, add/remove users, and set/clear device perms.
TEST_F(HostPermissionTest, AdminManagesUsersAndToggles) {
    auto reader = connectOperator();
    QTcpSocket* sock = m_sockets.back().get();

    // Upgrade this connection to an Admin session via a v2 AUTH_REQ.
    AuthRequest req;
    req.legacy = false;
    req.username = "tester";
    req.password = "pw";
    sock->write(ProtocolManager::encode(MessageType::AUTH_REQ,
                                       ProtocolManager::encodeAuthRequest(req)));
    sock->flush();
    QByteArray adminAuth;
    ASSERT_TRUE(reader->readType(MessageType::AUTH_RESP, adminAuth));
    AuthResponse ar = ProtocolManager::decodeAuthResponse(adminAuth);
    ASSERT_EQ(ar.grantedLevel, static_cast<uint8_t>(PermLevel::Admin));
    ASSERT_TRUE(PermissionModel::hasCapability(ar.grantedCaps, Capability::UserManage));

    // USER_LIST_REQ -> USER_LIST_RESP lists the seeded admin and our tester.
    sock->write(ProtocolManager::encode(MessageType::USER_LIST_REQ, QByteArray()));
    sock->flush();
    QByteArray listPayload;
    ASSERT_TRUE(reader->readType(MessageType::USER_LIST_RESP, listPayload));
    QStringList names;
    for (const auto& u : ProtocolManager::decodeUserListResponse(listPayload))
        names << u.username;
    EXPECT_TRUE(names.contains("admin"));
    EXPECT_TRUE(names.contains("tester"));

    // PERMISSION_TOGGLE_REQ(Terminal, off) -> PERMISSION_TOGGLE_RESP with only
    // the Terminal bit cleared and everything else preserved.
    sock->write(ProtocolManager::encode(
        MessageType::PERMISSION_TOGGLE_REQ,
        ProtocolManager::encodePermissionToggleRequest(
            static_cast<uint32_t>(Capability::Terminal), false)));
    sock->flush();
    QByteArray togglePayload;
    ASSERT_TRUE(reader->readType(MessageType::PERMISSION_TOGGLE_RESP, togglePayload));
    const uint32_t toggles = ProtocolManager::decodePermissionToggleResponse(togglePayload);
    EXPECT_EQ(toggles, kAllCapabilities & ~static_cast<uint32_t>(Capability::Terminal));
    EXPECT_TRUE(PermissionModel::hasCapability(toggles, Capability::ViewScreen));

    // USER_ADD alice -> she appears in the next USER_LIST_RESP.
    UserMutation add;
    add.username = "alice";
    add.password = "alicepw";
    add.level = static_cast<uint8_t>(PermLevel::Viewer);
    add.enabled = true;
    sock->write(ProtocolManager::encode(MessageType::USER_ADD,
                                       ProtocolManager::encodeUserMutation(add)));
    sock->flush();
    QByteArray listAfterAdd;
    ASSERT_TRUE(reader->readType(MessageType::USER_LIST_RESP, listAfterAdd));
    QStringList afterAdd;
    for (const auto& u : ProtocolManager::decodeUserListResponse(listAfterAdd))
        afterAdd << u.username;
    EXPECT_TRUE(afterAdd.contains("alice"));

    // USER_REMOVE alice -> she is gone from the next USER_LIST_RESP.
    sock->write(ProtocolManager::encode(MessageType::USER_REMOVE, QByteArray("alice")));
    sock->flush();
    QByteArray listAfterRemove;
    ASSERT_TRUE(reader->readType(MessageType::USER_LIST_RESP, listAfterRemove));
    QStringList afterRemove;
    for (const auto& u : ProtocolManager::decodeUserListResponse(listAfterRemove))
        afterRemove << u.username;
    EXPECT_FALSE(afterRemove.contains("alice"));
}

TEST_F(HostPermissionTest, AdminSetsAndClearsDevicePermission) {
    auto reader = connectOperator();
    QTcpSocket* sock = m_sockets.back().get();

    AuthRequest req;
    req.legacy = false;
    req.username = "tester";
    req.password = "pw";
    sock->write(ProtocolManager::encode(MessageType::AUTH_REQ,
                                       ProtocolManager::encodeAuthRequest(req)));
    sock->flush();
    QByteArray adminAuth;
    ASSERT_TRUE(reader->readType(MessageType::AUTH_RESP, adminAuth));
    ASSERT_EQ(ProtocolManager::decodeAuthResponse(adminAuth).grantedLevel,
              static_cast<uint8_t>(PermLevel::Admin));

    // Set a device permission (reuses the response codec as the request).
    DevicePermission dp;
    dp.deviceId = "dev-x";
    dp.level = static_cast<int>(PermLevel::Viewer);
    dp.capMask = -1;
    sock->write(ProtocolManager::encode(
        MessageType::DEVICE_PERM_SET_REQ,
        ProtocolManager::encodeDevicePermissionResponse({dp})));
    sock->flush();
    QByteArray devPayload;
    ASSERT_TRUE(reader->readType(MessageType::DEVICE_PERM_RESP, devPayload));
    bool found = false;
    for (const auto& d : ProtocolManager::decodeDevicePermissionResponse(devPayload))
        if (d.deviceId == "dev-x") found = true;
    EXPECT_TRUE(found);

    // Clear it (level < 0 signals removal) -> it disappears from the list.
    DevicePermission clear;
    clear.deviceId = "dev-x";
    clear.level = -1;
    sock->write(ProtocolManager::encode(
        MessageType::DEVICE_PERM_SET_REQ,
        ProtocolManager::encodeDevicePermissionResponse({clear})));
    sock->flush();
    QByteArray devPayload2;
    ASSERT_TRUE(reader->readType(MessageType::DEVICE_PERM_RESP, devPayload2));
    bool stillThere = false;
    for (const auto& d : ProtocolManager::decodeDevicePermissionResponse(devPayload2))
        if (d.deviceId == "dev-x") stillThere = true;
    EXPECT_FALSE(stillThere);
}

} // namespace
