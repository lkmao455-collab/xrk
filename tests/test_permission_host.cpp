#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QTcpSocket>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QDateTime>
#include <QSignalSpy>
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

        // Trusted IPs are persisted in QSettings, so they leak between tests
        // *and* between runs of this binary. Approval behaviour depends on
        // whether the peer is trusted, so drop loopback on the way in and on
        // the way out — this keeps the approval tests order-independent and
        // stops ApprovalSkippedForTrustedIp from poisoning the next run.
        m_host->removeTrustedIp(kLoopback);
    }

    void TearDown() override {
        if (m_host) {
            m_host->removeTrustedIp(kLoopback);
            m_host->stop();
            m_host.reset();
        }
        DatabaseManager::instance().shutdown();
        m_dir.reset();
    }

    static constexpr const char* kLoopback = "127.0.0.1";

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

// AUDIT_LOG_RESP carries a JSON array of audit entries as UTF-8 text.
TEST(PermissionCodecTest, AuditLogResponseRoundTrip) {
    QJsonArray in;
    in.append(QJsonObject{{"type", "operation"}, {"operation", "user_manage_denied"}});
    in.append(QJsonObject{{"type", "connection"}, {"event", "connected"}});
    QByteArray enc = ProtocolManager::encodeAuditLogResponse(in);
    QJsonArray out = ProtocolManager::decodeAuditLogResponse(enc);
    ASSERT_EQ(out.size(), 2);
    EXPECT_EQ(out[0].toObject()["operation"].toString(), "user_manage_denied");
    EXPECT_EQ(out[1].toObject()["event"].toString(), "connected");
    // Empty payload decodes to an empty (not null) array.
    EXPECT_TRUE(ProtocolManager::decodeAuditLogResponse(QByteArray()).isEmpty());
}

TEST(PermissionCodecTest, TemporaryGrantRoundTrip) {
    TemporaryGrant in;
    in.deviceId = "192.168.1.20";
    in.level = static_cast<int>(PermLevel::Admin);
    in.capMask = 0x1234;
    in.expiresAt = 1770000000000LL;
    TemporaryGrant out = ProtocolManager::decodeTemporaryGrant(
        ProtocolManager::encodeTemporaryGrant(in));
    EXPECT_EQ(out.deviceId, in.deviceId);
    EXPECT_EQ(out.level, in.level);
    EXPECT_EQ(out.capMask, in.capMask);
    EXPECT_EQ(out.expiresAt, in.expiresAt);

    // A revoke carries expiresAt < 0 and must survive the round trip.
    TemporaryGrant revoke;
    revoke.deviceId = "10.0.0.5";
    revoke.expiresAt = -1;
    TemporaryGrant rOut = ProtocolManager::decodeTemporaryGrant(
        ProtocolManager::encodeTemporaryGrant(revoke));
    EXPECT_EQ(rOut.deviceId, revoke.deviceId);
    EXPECT_LT(rOut.expiresAt, 0);

    // Truncated payloads must not read past the buffer.
    TemporaryGrant bad = ProtocolManager::decodeTemporaryGrant(QByteArray(6, '\0'));
    EXPECT_TRUE(bad.deviceId.isEmpty());
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

// v1.8.0 RBAC: a denied privileged operation must produce exactly one audit
// entry (auditing is centralized in sendPermissionDenied, not duplicated in
// requireCap). This guards against regression of the double-log/漏写 bug.
// Uses a before/after delta so the shared on-disk audit history is ignored.
TEST_F(HostPermissionTest, DenialIsAuditedOnce) {
    auto reader = connectOperator();
    QTcpSocket* sock = m_sockets.back().get();

    auto countDenied = [this]() -> int {
        QJsonArray entries = m_host->auditLogger()->recentEntries(100000);
        int n = 0;
        for (const auto& v : entries) {
            QJsonObject obj = v.toObject();
            if (obj["type"].toString() == "operation" &&
                obj["operation"].toString().endsWith("_denied")) {
                ++n;
            }
        }
        return n;
    };

    const int before = countDenied();

    sock->write(ProtocolManager::encode(MessageType::USER_LIST_REQ, QByteArray()));
    sock->flush();
    QByteArray denied;
    ASSERT_TRUE(reader->readType(MessageType::PERMISSION_DENIED, denied));

    EXPECT_EQ(countDenied() - before, 1)
        << "expected exactly one new *_denied audit entry for the single denial";
}

// An Admin can retrieve the host's recent audit entries via AUDIT_LOG_REQ.
TEST_F(HostPermissionTest, AdminRequestsAuditLog) {
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

    sock->write(ProtocolManager::encode(MessageType::AUDIT_LOG_REQ, QByteArray()));
    sock->flush();
    QByteArray resp;
    ASSERT_TRUE(reader->readType(MessageType::AUDIT_LOG_RESP, resp));
    QJsonArray entries = ProtocolManager::decodeAuditLogResponse(resp);
    // The host logs the "connected"/"authenticated" events for this session,
    // so the response must contain at least those entries.
    EXPECT_GE(entries.size(), 1);
    bool hasConnection = false;
    for (const auto& v : entries) {
        if (v.toObject()["type"].toString() == "connection") hasConnection = true;
    }
    EXPECT_TRUE(hasConnection);
}

// v1.8.0 RBAC: an Operator must not be able to grant itself a temporary
// elevation — TEMP_GRANT_REQ is gated by UserManage like every other 21x/22x
// admin message.
TEST_F(HostPermissionTest, OperatorCannotSetTempGrant) {
    auto reader = connectOperator();
    QTcpSocket* sock = m_sockets.back().get();

    TemporaryGrant g;
    g.deviceId = "127.0.0.1";
    g.level = static_cast<int>(PermLevel::Admin);
    g.capMask = -1;
    g.expiresAt = QDateTime::currentMSecsSinceEpoch() + 600000;
    sock->write(ProtocolManager::encode(MessageType::TEMP_GRANT_REQ,
                                        ProtocolManager::encodeTemporaryGrant(g)));
    sock->flush();
    QByteArray denied;
    ASSERT_TRUE(reader->readType(MessageType::PERMISSION_DENIED, denied));
    EXPECT_EQ(ProtocolManager::decodePermissionDenied(denied).capability,
              static_cast<uint32_t>(Capability::UserManage));
}

// v1.8.0 RBAC: an Admin can install a temporary grant; the host echoes it back
// on TEMP_GRANT_RESP, and a grant whose expiry is already in the past is
// dropped rather than applied.
TEST_F(HostPermissionTest, AdminSetsAndClearsTempGrant) {
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

    TemporaryGrant g;
    g.deviceId = "192.168.77.7";
    g.level = static_cast<int>(PermLevel::Operator);
    g.capMask = -1;
    g.expiresAt = QDateTime::currentMSecsSinceEpoch() + 600000;
    sock->write(ProtocolManager::encode(MessageType::TEMP_GRANT_REQ,
                                        ProtocolManager::encodeTemporaryGrant(g)));
    sock->flush();
    QByteArray resp;
    ASSERT_TRUE(reader->readType(MessageType::TEMP_GRANT_RESP, resp));
    TemporaryGrant echoed = ProtocolManager::decodeTemporaryGrant(resp);
    EXPECT_EQ(echoed.deviceId, g.deviceId);
    EXPECT_EQ(echoed.level, g.level);
    EXPECT_EQ(echoed.expiresAt, g.expiresAt);

    // Revoking uses expiresAt < 0 and is acknowledged the same way.
    TemporaryGrant revoke;
    revoke.deviceId = g.deviceId;
    revoke.expiresAt = -1;
    sock->write(ProtocolManager::encode(MessageType::TEMP_GRANT_REQ,
                                        ProtocolManager::encodeTemporaryGrant(revoke)));
    sock->flush();
    QByteArray resp2;
    ASSERT_TRUE(reader->readType(MessageType::TEMP_GRANT_RESP, resp2));
    EXPECT_LT(ProtocolManager::decodeTemporaryGrant(resp2).expiresAt, 0);

    // The grant/clear pair must be recorded in the audit trail.
    QJsonArray entries = m_host->auditLogger()->recentEntries(200);
    bool sawSet = false, sawClear = false;
    for (const auto& v : entries) {
        QString op = v.toObject()["operation"].toString();
        if (op == "temp_grant_set") sawSet = true;
        if (op == "temp_grant_clear") sawClear = true;
    }
    EXPECT_TRUE(sawSet);
    EXPECT_TRUE(sawClear);
}

// ─────────────── Real-time approval workflow (v1.8.0 Phase 4) ───────────────

// With approval enabled the host must NOT start the session on connect: the
// controller gets CONSENT_REQUEST and no session key, and the session holds no
// capabilities until the host user approves.
TEST_F(HostPermissionTest, ApprovalParksSessionUntilGranted) {
    m_host->setRequireApproval(true);
    QSignalSpy consentSpy(m_host.get(), &Host::consentRequested);

    auto sock = std::make_unique<QTcpSocket>();
    sock->connectToHost(QHostAddress::LocalHost, m_port);
    ASSERT_TRUE(sock->waitForConnected(5000));
    auto reader = std::make_unique<MessageReader>(sock.get());

    QByteArray consentReq;
    ASSERT_TRUE(reader->readType(MessageType::CONSENT_REQUEST, consentReq));

    // The host UI is notified with the clientId of the parked session, which
    // holds nothing until it is approved.
    ASSERT_EQ(consentSpy.count(), 1);
    const QString clientId = consentSpy.at(0).at(0).toString();
    EXPECT_TRUE(m_host->isAwaitingApproval(clientId));
    EXPECT_EQ(m_host->clientPermLevel(clientId), PermLevel::None);
    EXPECT_EQ(m_host->clientCapabilities(clientId), 0u);

    // Approving releases CONSENT_RESPONSE(allowed) and then the session key.
    m_host->grantConsent(clientId);
    QByteArray consentResp;
    ASSERT_TRUE(reader->readType(MessageType::CONSENT_RESPONSE, consentResp));
    bool allowed = false;
    QString hostName;
    ProtocolManager::decodeConsent(consentResp, allowed, hostName);
    EXPECT_TRUE(allowed);

    QByteArray auth;
    ASSERT_TRUE(reader->readType(MessageType::AUTH_RESP, auth));
    EXPECT_EQ(ProtocolManager::decodeAuthResponse(auth).grantedLevel,
              static_cast<uint8_t>(m_host->defaultPermLevel()));
    EXPECT_FALSE(m_host->isAwaitingApproval(clientId));

    m_sockets.push_back(std::move(sock));
}

// Denying an approval request must tell the controller it was rejected before
// the socket goes away, and must drop the session.
TEST_F(HostPermissionTest, ApprovalDenialRejectsSession) {
    m_host->setRequireApproval(true);
    QSignalSpy consentSpy(m_host.get(), &Host::consentRequested);
    QSignalSpy failSpy(m_host.get(), &Host::clientAuthFailed);

    auto sock = std::make_unique<QTcpSocket>();
    sock->connectToHost(QHostAddress::LocalHost, m_port);
    ASSERT_TRUE(sock->waitForConnected(5000));
    auto reader = std::make_unique<MessageReader>(sock.get());

    QByteArray consentReq;
    ASSERT_TRUE(reader->readType(MessageType::CONSENT_REQUEST, consentReq));
    ASSERT_EQ(consentSpy.count(), 1);
    const QString clientId = consentSpy.at(0).at(0).toString();

    m_host->denyConsent(clientId);

    QByteArray consentResp;
    ASSERT_TRUE(reader->readType(MessageType::CONSENT_RESPONSE, consentResp));
    bool allowed = true;
    QString hostName;
    ProtocolManager::decodeConsent(consentResp, allowed, hostName);
    EXPECT_FALSE(allowed);
    EXPECT_EQ(failSpy.count(), 1);
    // The session is gone: an unknown clientId reports no level / no caps.
    EXPECT_EQ(m_host->clientPermLevel(clientId), PermLevel::None);
    EXPECT_EQ(m_host->clientCapabilities(clientId), 0u);
    EXPECT_FALSE(m_host->isAwaitingApproval(clientId));

    m_sockets.push_back(std::move(sock));
}

// A trusted IP bypasses the prompt entirely — the session starts immediately
// even with approval enabled (this is what the dialog's "remember" box sets up).
TEST_F(HostPermissionTest, ApprovalSkippedForTrustedIp) {
    m_host->setRequireApproval(true);
    m_host->addTrustedIp(QHostAddress(QHostAddress::LocalHost).toString());

    auto sock = std::make_unique<QTcpSocket>();
    sock->connectToHost(QHostAddress::LocalHost, m_port);
    ASSERT_TRUE(sock->waitForConnected(5000));
    auto reader = std::make_unique<MessageReader>(sock.get());

    QByteArray auth;
    ASSERT_TRUE(reader->readType(MessageType::AUTH_RESP, auth));
    EXPECT_EQ(ProtocolManager::decodeAuthResponse(auth).grantedLevel,
              static_cast<uint8_t>(m_host->defaultPermLevel()));

    m_sockets.push_back(std::move(sock));
}

// Approval is off by default: existing behaviour (auto-grant on connect) must
// be unchanged, and consentRequested() must not fire.
TEST_F(HostPermissionTest, ApprovalDisabledByDefault) {
    EXPECT_FALSE(m_host->requireApproval());
    QSignalSpy consentSpy(m_host.get(), &Host::consentRequested);
    QSignalSpy connectSpy(m_host.get(), &Host::clientConnected);

    auto reader = connectOperator();

    ASSERT_EQ(connectSpy.count(), 1);
    const QString clientId = connectSpy.at(0).at(0).toString();
    EXPECT_FALSE(m_host->isAwaitingApproval(clientId));
    EXPECT_EQ(m_host->clientPermLevel(clientId), m_host->defaultPermLevel());
    EXPECT_EQ(consentSpy.count(), 0);
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
