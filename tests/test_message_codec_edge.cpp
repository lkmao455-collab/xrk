#include <gtest/gtest.h>
#include "core/message_codec.h"
#include "core/types.h"

using namespace xrk;

class MessageCodecEdgeTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(MessageCodecEdgeTest, DecodeTooShort) {
    MessageType type;
    QByteArray payload;
    QString sessionId;
    EXPECT_FALSE(MessageCodec::decode(QByteArray(10, 'A'), type, payload, sessionId));
    EXPECT_FALSE(MessageCodec::decode(QByteArray(), type, payload, sessionId));
}

TEST_F(MessageCodecEdgeTest, DecodeInvalidMagic) {
    QByteArray data = MessageCodec::createHeader(MessageType::HEARTBEAT, 0);
    EXPECT_EQ(data.size(), static_cast<int>(MessageCodec::MIN_MESSAGE_SIZE));
    data[0] = data[0] ^ 0xFF;

    MessageType type;
    QByteArray payload;
    QString sessionId;
    EXPECT_FALSE(MessageCodec::decode(data, type, payload, sessionId));
}

TEST_F(MessageCodecEdgeTest, ParseHeaderWrongVersion) {
    QByteArray data = MessageCodec::createHeader(MessageType::HEARTBEAT, 0);
    data[4] = data[4] ^ 0xFF;

    MessageHeader header;
    uint32_t sessionIdLen = 0;
    EXPECT_FALSE(MessageCodec::parseHeader(data, header, sessionIdLen));
}

TEST_F(MessageCodecEdgeTest, ParseHeaderSessionIdTooLong) {
    QByteArray data = MessageCodec::createHeader(MessageType::HEARTBEAT, 0);
    data.replace(24, 4, QByteArray::fromHex("7fffffff"));

    MessageHeader header;
    uint32_t sessionIdLen = 0;
    EXPECT_FALSE(MessageCodec::parseHeader(data, header, sessionIdLen));
}

TEST_F(MessageCodecEdgeTest, DecodeDeclaredLengthExceedsData) {
    QByteArray data = MessageCodec::encode(MessageType::HEARTBEAT, "hi");
    data.replace(12, 4, QByteArray::fromHex("000f4240"));

    MessageType type;
    QByteArray payload;
    QString sessionId;
    EXPECT_FALSE(MessageCodec::decode(data, type, payload, sessionId));
}

TEST_F(MessageCodecEdgeTest, VerifyChecksumTooShort) {
    EXPECT_FALSE(MessageCodec::verifyChecksum(QByteArray()));
    EXPECT_FALSE(MessageCodec::verifyChecksum(QByteArray(2, 'x')));
}

TEST_F(MessageCodecEdgeTest, VerifyChecksumValidMessage) {
    QByteArray data = MessageCodec::encode(MessageType::HEARTBEAT, "checksum me");
    EXPECT_TRUE(MessageCodec::verifyChecksum(data));
}

TEST_F(MessageCodecEdgeTest, VerifyChecksumCorruptedPayload) {
    QByteArray data = MessageCodec::encode(MessageType::HEARTBEAT, "checksum me");
    int mid = data.size() / 2;
    data[mid] = data[mid] ^ 0xFF;
    EXPECT_FALSE(MessageCodec::verifyChecksum(data));
}

TEST_F(MessageCodecEdgeTest, ChecksumDeterministic) {
    QByteArray a = MessageCodec::calculateChecksum("hello");
    QByteArray b = MessageCodec::calculateChecksum("hello");
    QByteArray c = MessageCodec::calculateChecksum("hello!");
    EXPECT_EQ(a, b);
    EXPECT_TRUE(a != c);
}

TEST_F(MessageCodecEdgeTest, CreateHeaderRoundTrip) {
    QByteArray data = MessageCodec::createHeader(MessageType::HEARTBEAT, 42, "sess-1");
    MessageHeader header;
    uint32_t sessionIdLen = 0;
    ASSERT_TRUE(MessageCodec::parseHeader(data, header, sessionIdLen));
    EXPECT_EQ(header.type, MessageType::HEARTBEAT);
    EXPECT_EQ(header.length, 42u);
    EXPECT_TRUE(header.sessionId == "sess-1");
    EXPECT_EQ(sessionIdLen, 6u);
}

TEST_F(MessageCodecEdgeTest, UnicodeSessionIdRoundTrip) {
    QByteArray data = MessageCodec::encode(MessageType::HEARTBEAT, "payload", "中文会话");
    EXPECT_TRUE(MessageCodec::verifyChecksum(data));

    MessageType type;
    QByteArray payload;
    QString sessionId;
    ASSERT_TRUE(MessageCodec::decode(data, type, payload, sessionId));
    EXPECT_EQ(type, MessageType::HEARTBEAT);
    EXPECT_TRUE(payload == QByteArray("payload"));
    EXPECT_TRUE(sessionId == "中文会话");
}

TEST_F(MessageCodecEdgeTest, EmptyPayloadRoundTrip) {
    QByteArray data = MessageCodec::encode(MessageType::HEARTBEAT, QByteArray());
    EXPECT_TRUE(MessageCodec::verifyChecksum(data));

    MessageType type;
    QByteArray payload;
    QString sessionId;
    ASSERT_TRUE(MessageCodec::decode(data, type, payload, sessionId));
    EXPECT_TRUE(payload.isEmpty());
}
