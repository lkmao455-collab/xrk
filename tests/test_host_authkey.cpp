#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QThread>
#include <QTcpSocket>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>
#include "host.h"
#include "core/message_codec.h"
#include "core/protocol_manager.h"
#include "core/types.h"

namespace xrk {

// Regression test for the no-password black-screen bug.
//
// The Host ALWAYS encrypts screen frames, so the controller must always have
// the session AES key. The key is delivered in AUTH_RESP. With a password the
// controller sends AUTH_REQ and gets the key back. But with NO password the
// controller does not send AUTH_REQ, so the Host must proactively send the key
// on connection. Previously the key was only sent in reply to AUTH_REQ, so the
// controller never initialized decryption and screen frames failed to decode
// (black desktop) while file transfer (unencrypted) kept working.
TEST(HostAuth, SendsKeyOnAutoAuth) {
    Host host;
    host.setAudioEnabled(false);
    const uint16_t port = 19997;
    ASSERT_TRUE(host.start(port));

    // Phase 5 consent: the Host with no password auto-authenticates the client
    // but does NOT send the session key until the host user grants consent.
    // Auto-grant it as soon as the host asks.
    QObject::connect(&host, &Host::consentRequested, &host, [&](const QString& clientId, const QString&) {
        host.grantConsent(clientId);
    });

    QTcpSocket sock;
    sock.connectToHost(QHostAddress::LocalHost, port);
    ASSERT_TRUE(sock.waitForConnected(5000));

    QByteArray buf;
    bool gotAuth = false;
    QByteArray authPayload;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&sock, &QTcpSocket::readyRead, [&]() {
        buf.append(sock.readAll());
        // Parse messages the same way the Host does.
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
            if (type == MessageType::AUTH_RESP) {
                gotAuth = true;
                authPayload = payload;
                loop.quit();
                return;
            }
        }
    });

    timeout.start(5000);
    loop.exec();

    EXPECT_TRUE(gotAuth) << "Host never sent AUTH_RESP on auto-auth (bytes read: "
                          << buf.size() << ")";
    if (!gotAuth && !buf.isEmpty()) {
        fprintf(stderr, "HostAuth debug: first 64 bytes = %s\n",
                buf.left(64).toHex(' ').toStdString().c_str());
    }
    if (gotAuth) {
        // v1.8.0: AUTH_RESP is "OK"(2) + 32-byte key + 16-byte IV +
        // [u8 grantedLevel][u32 grantedCaps BE] = 55 bytes.
        EXPECT_EQ(authPayload.size(), 2 + 32 + 16 + 1 + 4)
            << "AUTH_RESP is missing the session key or the permission tail";
        EXPECT_TRUE(authPayload.startsWith("OK"));

        AuthResponse resp = ProtocolManager::decodeAuthResponse(authPayload);
        EXPECT_TRUE(resp.ok);
        EXPECT_EQ(resp.sessionKey.size(), 32);
        EXPECT_EQ(resp.iv.size(), 16);
        // A no-password session is auto-granted at the host's default preset
        // level (Operator), which must at least be able to see the screen.
        EXPECT_EQ(resp.grantedLevel, static_cast<uint8_t>(PermLevel::Operator));
        EXPECT_TRUE(PermissionModel::hasCapability(resp.grantedCaps, Capability::ViewScreen));
        EXPECT_TRUE(PermissionModel::hasCapability(resp.grantedCaps, Capability::ControlInput));
        // Operator is not an admin: no user management, no terminal.
        EXPECT_FALSE(PermissionModel::hasCapability(resp.grantedCaps, Capability::UserManage));
        EXPECT_FALSE(PermissionModel::hasCapability(resp.grantedCaps, Capability::Terminal));
    }

    sock.disconnectFromHost();
    host.stop();
}

} // namespace xrk
