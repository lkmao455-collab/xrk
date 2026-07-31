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
        // AUTH_RESP must be "OK" (2) + 32-byte key + 16-byte IV = 50 bytes.
        EXPECT_EQ(authPayload.size(), 2 + 32 + 16)
            << "AUTH_RESP is missing the session key";
        EXPECT_TRUE(authPayload.startsWith("OK"));
    }

    sock.disconnectFromHost();
    host.stop();
}

} // namespace xrk
