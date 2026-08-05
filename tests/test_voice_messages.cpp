#include <gtest/gtest.h>
#include "core/protocol_manager.h"
#include "core/types.h"
#include "core/message_codec.h"

using namespace xrk;

class VoiceMessageTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(VoiceMessageTest, EncodeDecodeRoundTrip) {
    VoiceMessage msg;
    msg.messageId = "msg-123";
    msg.senderId = "sender-456";
    msg.senderName = "TestUser";
    msg.voiceData = QByteArray(1024, 'x');
    msg.voiceFileName = "voice_1234567890.pcm";
    msg.duration = 5;
    msg.timestamp = 1700000000000LL;
    msg.isRead = false;

    QByteArray encoded = ProtocolManager::encodeVoiceMessage(msg);
    EXPECT_FALSE(encoded.isEmpty());

    VoiceMessage decoded = ProtocolManager::decodeVoiceMessage(encoded);
    EXPECT_EQ(decoded.messageId, msg.messageId);
    EXPECT_EQ(decoded.senderId, msg.senderId);
    EXPECT_EQ(decoded.senderName, msg.senderName);
    EXPECT_EQ(decoded.voiceData, msg.voiceData);
    EXPECT_EQ(decoded.voiceFileName, msg.voiceFileName);
    EXPECT_EQ(decoded.duration, msg.duration);
    EXPECT_EQ(decoded.timestamp, msg.timestamp);
    EXPECT_EQ(decoded.isRead, msg.isRead);
}

TEST_F(VoiceMessageTest, EncodeDecodeEmptyVoiceData) {
    VoiceMessage msg;
    msg.messageId = "msg-empty";
    msg.senderId = "sender-1";
    msg.senderName = "User";
    msg.voiceData = QByteArray();
    msg.voiceFileName = "voice_empty.pcm";
    msg.duration = 0;
    msg.timestamp = 0;
    msg.isRead = true;

    QByteArray encoded = ProtocolManager::encodeVoiceMessage(msg);
    EXPECT_FALSE(encoded.isEmpty());

    VoiceMessage decoded = ProtocolManager::decodeVoiceMessage(encoded);
    EXPECT_EQ(decoded.messageId, msg.messageId);
    EXPECT_EQ(decoded.senderId, msg.senderId);
    EXPECT_EQ(decoded.voiceData, msg.voiceData);
    EXPECT_EQ(decoded.duration, 0);
    EXPECT_TRUE(decoded.isRead);
}

TEST_F(VoiceMessageTest, VoiceMessageProtocolMessage) {
    VoiceMessage msg;
    msg.messageId = "msg-protocol";
    msg.senderId = "sender-789";
    msg.senderName = "ProtocolUser";
    msg.voiceData = QByteArray(2048, 'y');
    msg.voiceFileName = "voice_proto.pcm";
    msg.duration = 10;
    msg.timestamp = 1700000000000LL;
    msg.isRead = false;

    QByteArray payload = ProtocolManager::encodeVoiceMessage(msg);
    QByteArray message = ProtocolManager::encode(MessageType::VOICE_MSG, payload, "session-voice");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::VOICE_MSG);
    EXPECT_EQ(sessionId, "session-voice");

    VoiceMessage decoded = ProtocolManager::decodeVoiceMessage(outPayload);
    EXPECT_EQ(decoded.messageId, msg.messageId);
    EXPECT_EQ(decoded.senderId, msg.senderId);
    EXPECT_EQ(decoded.voiceData, msg.voiceData);
    EXPECT_EQ(decoded.duration, msg.duration);
}

TEST_F(VoiceMessageTest, VoiceAckProtocolMessage) {
    QByteArray payload("OK");
    QByteArray message = ProtocolManager::encode(MessageType::VOICE_ACK, payload, "session-ack");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::VOICE_ACK);
    EXPECT_EQ(sessionId, "session-ack");
    EXPECT_EQ(outPayload, payload);
}

TEST_F(VoiceMessageTest, VoiceMessageTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::VOICE_MSG), 143u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::VOICE_ACK), 144u);
}

class VideoMessageTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(VideoMessageTest, EncodeDecodeRoundTrip) {
    VideoMessage msg;
    msg.messageId = "msg-123";
    msg.senderId = "sender-456";
    msg.senderName = "TestUser";
    msg.videoData = QByteArray(1024, 'x');
    msg.videoFileName = "video_1234567890.mp4";
    msg.duration = 5;
    msg.width = 1280;
    msg.height = 720;
    msg.timestamp = 1700000000000LL;
    msg.isRead = false;

    QByteArray encoded = ProtocolManager::encodeVideoMessage(msg);
    EXPECT_FALSE(encoded.isEmpty());

    VideoMessage decoded = ProtocolManager::decodeVideoMessage(encoded);
    EXPECT_EQ(decoded.messageId, msg.messageId);
    EXPECT_EQ(decoded.senderId, msg.senderId);
    EXPECT_EQ(decoded.senderName, msg.senderName);
    EXPECT_EQ(decoded.videoData, msg.videoData);
    EXPECT_EQ(decoded.videoFileName, msg.videoFileName);
    EXPECT_EQ(decoded.duration, msg.duration);
    EXPECT_DOUBLE_EQ(decoded.width, msg.width);
    EXPECT_DOUBLE_EQ(decoded.height, msg.height);
    EXPECT_EQ(decoded.timestamp, msg.timestamp);
    EXPECT_EQ(decoded.isRead, msg.isRead);
}

TEST_F(VideoMessageTest, EncodeDecodeEmptyVideoData) {
    VideoMessage msg;
    msg.messageId = "msg-empty";
    msg.senderId = "sender-1";
    msg.senderName = "User";
    msg.videoData = QByteArray();
    msg.videoFileName = "video_empty.mp4";
    msg.duration = 0;
    msg.width = 0;
    msg.height = 0;
    msg.timestamp = 0;
    msg.isRead = true;

    QByteArray encoded = ProtocolManager::encodeVideoMessage(msg);
    EXPECT_FALSE(encoded.isEmpty());

    VideoMessage decoded = ProtocolManager::decodeVideoMessage(encoded);
    EXPECT_EQ(decoded.messageId, msg.messageId);
    EXPECT_EQ(decoded.senderId, msg.senderId);
    EXPECT_EQ(decoded.videoData, msg.videoData);
    EXPECT_EQ(decoded.duration, 0);
    EXPECT_DOUBLE_EQ(decoded.width, 0.0);
    EXPECT_DOUBLE_EQ(decoded.height, 0.0);
    EXPECT_TRUE(decoded.isRead);
}

TEST_F(VideoMessageTest, VideoMessageProtocolMessage) {
    VideoMessage msg;
    msg.messageId = "msg-protocol";
    msg.senderId = "sender-789";
    msg.senderName = "ProtocolUser";
    msg.videoData = QByteArray(2048, 'y');
    msg.videoFileName = "video_proto.mp4";
    msg.duration = 10;
    msg.width = 1920;
    msg.height = 1080;
    msg.timestamp = 1700000000000LL;
    msg.isRead = false;

    QByteArray payload = ProtocolManager::encodeVideoMessage(msg);
    QByteArray message = ProtocolManager::encode(MessageType::VIDEO_MSG, payload, "session-video");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::VIDEO_MSG);
    EXPECT_EQ(sessionId, "session-video");

    VideoMessage decoded = ProtocolManager::decodeVideoMessage(outPayload);
    EXPECT_EQ(decoded.messageId, msg.messageId);
    EXPECT_EQ(decoded.senderId, msg.senderId);
    EXPECT_EQ(decoded.videoData, msg.videoData);
    EXPECT_EQ(decoded.duration, msg.duration);
    EXPECT_DOUBLE_EQ(decoded.width, msg.width);
    EXPECT_DOUBLE_EQ(decoded.height, msg.height);
}

TEST_F(VideoMessageTest, VideoAckProtocolMessage) {
    QByteArray payload("OK");
    QByteArray message = ProtocolManager::encode(MessageType::VIDEO_ACK, payload, "session-ack");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::VIDEO_ACK);
    EXPECT_EQ(sessionId, "session-ack");
    EXPECT_EQ(outPayload, payload);
}

TEST_F(VideoMessageTest, VideoMessageTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::VIDEO_MSG), 145u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::VIDEO_ACK), 146u);
}

class LocationMessageTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(LocationMessageTest, EncodeDecodeRoundTrip) {
    LocationMessage msg;
    msg.messageId = "msg-123";
    msg.senderId = "sender-456";
    msg.senderName = "TestUser";
    msg.latitude = 39.9042;
    msg.longitude = 116.4074;
    msg.locationName = "Beijing";
    msg.timestamp = 1700000000000LL;
    msg.isRead = false;

    QByteArray encoded = ProtocolManager::encodeLocationMessage(msg);
    EXPECT_FALSE(encoded.isEmpty());

    LocationMessage decoded = ProtocolManager::decodeLocationMessage(encoded);
    EXPECT_EQ(decoded.messageId, msg.messageId);
    EXPECT_EQ(decoded.senderId, msg.senderId);
    EXPECT_EQ(decoded.senderName, msg.senderName);
    EXPECT_DOUBLE_EQ(decoded.latitude, msg.latitude);
    EXPECT_DOUBLE_EQ(decoded.longitude, msg.longitude);
    EXPECT_EQ(decoded.locationName, msg.locationName);
    EXPECT_EQ(decoded.timestamp, msg.timestamp);
    EXPECT_EQ(decoded.isRead, msg.isRead);
}

TEST_F(LocationMessageTest, EncodeDecodeEmptyLocation) {
    LocationMessage msg;
    msg.messageId = "msg-empty";
    msg.senderId = "sender-1";
    msg.senderName = "User";
    msg.latitude = 0;
    msg.longitude = 0;
    msg.locationName = "";
    msg.timestamp = 0;
    msg.isRead = true;

    QByteArray encoded = ProtocolManager::encodeLocationMessage(msg);
    EXPECT_FALSE(encoded.isEmpty());

    LocationMessage decoded = ProtocolManager::decodeLocationMessage(encoded);
    EXPECT_EQ(decoded.messageId, msg.messageId);
    EXPECT_EQ(decoded.senderId, msg.senderId);
    EXPECT_DOUBLE_EQ(decoded.latitude, 0.0);
    EXPECT_DOUBLE_EQ(decoded.longitude, 0.0);
    EXPECT_TRUE(decoded.isRead);
}

TEST_F(LocationMessageTest, LocationMessageProtocolMessage) {
    LocationMessage msg;
    msg.messageId = "msg-protocol";
    msg.senderId = "sender-789";
    msg.senderName = "ProtocolUser";
    msg.latitude = 31.2304;
    msg.longitude = 121.4737;
    msg.locationName = "Shanghai";
    msg.timestamp = 1700000000000LL;
    msg.isRead = false;

    QByteArray payload = ProtocolManager::encodeLocationMessage(msg);
    QByteArray message = ProtocolManager::encode(MessageType::LOCATION_MSG, payload, "session-location");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::LOCATION_MSG);
    EXPECT_EQ(sessionId, "session-location");

    LocationMessage decoded = ProtocolManager::decodeLocationMessage(outPayload);
    EXPECT_EQ(decoded.messageId, msg.messageId);
    EXPECT_EQ(decoded.senderId, msg.senderId);
    EXPECT_DOUBLE_EQ(decoded.latitude, msg.latitude);
    EXPECT_DOUBLE_EQ(decoded.longitude, msg.longitude);
    EXPECT_EQ(decoded.locationName, msg.locationName);
}

TEST_F(LocationMessageTest, LocationAckProtocolMessage) {
    QByteArray payload("OK");
    QByteArray message = ProtocolManager::encode(MessageType::LOCATION_ACK, payload, "session-ack");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::LOCATION_ACK);
    EXPECT_EQ(sessionId, "session-ack");
    EXPECT_EQ(outPayload, payload);
}

TEST_F(LocationMessageTest, LocationMessageTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::LOCATION_MSG), 147u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::LOCATION_ACK), 148u);
}

class CardMessageTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(CardMessageTest, EncodeDecodeRoundTrip) {
    CardMessage msg;
    msg.messageId = "msg-123";
    msg.senderId = "sender-456";
    msg.senderName = "TestUser";
    msg.vCardData = "BEGIN:VCARD\nVERSION:3.0\nFN:John Doe\nEND:VCARD";
    msg.timestamp = 1700000000000LL;
    msg.isRead = false;

    QByteArray encoded = ProtocolManager::encodeCardMessage(msg);
    EXPECT_FALSE(encoded.isEmpty());

    CardMessage decoded = ProtocolManager::decodeCardMessage(encoded);
    EXPECT_EQ(decoded.messageId, msg.messageId);
    EXPECT_EQ(decoded.senderId, msg.senderId);
    EXPECT_EQ(decoded.senderName, msg.senderName);
    EXPECT_EQ(decoded.vCardData, msg.vCardData);
    EXPECT_EQ(decoded.timestamp, msg.timestamp);
    EXPECT_EQ(decoded.isRead, msg.isRead);
}

TEST_F(CardMessageTest, EncodeDecodeEmptyCard) {
    CardMessage msg;
    msg.messageId = "msg-empty";
    msg.senderId = "sender-1";
    msg.senderName = "User";
    msg.vCardData = "";
    msg.timestamp = 0;
    msg.isRead = true;

    QByteArray encoded = ProtocolManager::encodeCardMessage(msg);
    EXPECT_FALSE(encoded.isEmpty());

    CardMessage decoded = ProtocolManager::decodeCardMessage(encoded);
    EXPECT_EQ(decoded.messageId, msg.messageId);
    EXPECT_EQ(decoded.senderId, msg.senderId);
    EXPECT_EQ(decoded.vCardData, msg.vCardData);
    EXPECT_TRUE(decoded.isRead);
}

TEST_F(CardMessageTest, CardMessageProtocolMessage) {
    CardMessage msg;
    msg.messageId = "msg-protocol";
    msg.senderId = "sender-789";
    msg.senderName = "ProtocolUser";
    msg.vCardData = "BEGIN:VCARD\nVERSION:3.0\nFN:Jane Smith\nEND:VCARD";
    msg.timestamp = 1700000000000LL;
    msg.isRead = false;

    QByteArray payload = ProtocolManager::encodeCardMessage(msg);
    QByteArray message = ProtocolManager::encode(MessageType::CARD_MSG, payload, "session-card");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::CARD_MSG);
    EXPECT_EQ(sessionId, "session-card");

    CardMessage decoded = ProtocolManager::decodeCardMessage(outPayload);
    EXPECT_EQ(decoded.messageId, msg.messageId);
    EXPECT_EQ(decoded.senderId, msg.senderId);
    EXPECT_EQ(decoded.vCardData, msg.vCardData);
}

TEST_F(CardMessageTest, CardAckProtocolMessage) {
    QByteArray payload("OK");
    QByteArray message = ProtocolManager::encode(MessageType::CARD_ACK, payload, "session-ack");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::CARD_ACK);
    EXPECT_EQ(sessionId, "session-ack");
    EXPECT_EQ(outPayload, payload);
}

TEST_F(CardMessageTest, CardMessageTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::CARD_MSG), 149u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::CARD_ACK), 150u);
}

class MergeForwardMessageTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(MergeForwardMessageTest, EncodeDecodeRoundTrip) {
    MergeForwardMessage msg;
    msg.messageId = "msg-123";
    msg.senderId = "sender-456";
    msg.senderName = "TestUser";
    msg.timestamp = 1700000000000LL;
    msg.isRead = false;

    ForwardedMessage fwd1;
    fwd1.messageId = "fwd-1";
    fwd1.senderId = "sender-1";
    fwd1.senderName = "User1";
    fwd1.content = "Hello";
    fwd1.timestamp = 1700000000001LL;
    fwd1.msgType = 0;
    msg.messages.append(fwd1);

    ForwardedMessage fwd2;
    fwd2.messageId = "fwd-2";
    fwd2.senderId = "sender-2";
    fwd2.senderName = "User2";
    fwd2.content = "World";
    fwd2.timestamp = 1700000000002LL;
    fwd2.msgType = 1;
    msg.messages.append(fwd2);

    QByteArray encoded = ProtocolManager::encodeMergeForwardMessage(msg);
    EXPECT_FALSE(encoded.isEmpty());

    MergeForwardMessage decoded = ProtocolManager::decodeMergeForwardMessage(encoded);
    EXPECT_EQ(decoded.messageId, msg.messageId);
    EXPECT_EQ(decoded.senderId, msg.senderId);
    EXPECT_EQ(decoded.senderName, msg.senderName);
    EXPECT_EQ(decoded.messages.size(), 2);
    EXPECT_EQ(decoded.messages[0].messageId, "fwd-1");
    EXPECT_EQ(decoded.messages[0].content, "Hello");
    EXPECT_EQ(decoded.messages[0].msgType, 0);
    EXPECT_EQ(decoded.messages[1].messageId, "fwd-2");
    EXPECT_EQ(decoded.messages[1].content, "World");
    EXPECT_EQ(decoded.messages[1].msgType, 1);
    EXPECT_EQ(decoded.timestamp, msg.timestamp);
    EXPECT_EQ(decoded.isRead, msg.isRead);
}

TEST_F(MergeForwardMessageTest, EncodeDecodeEmptyMessages) {
    MergeForwardMessage msg;
    msg.messageId = "msg-empty";
    msg.senderId = "sender-1";
    msg.senderName = "User";
    msg.timestamp = 0;
    msg.isRead = true;

    QByteArray encoded = ProtocolManager::encodeMergeForwardMessage(msg);
    EXPECT_FALSE(encoded.isEmpty());

    MergeForwardMessage decoded = ProtocolManager::decodeMergeForwardMessage(encoded);
    EXPECT_EQ(decoded.messageId, msg.messageId);
    EXPECT_EQ(decoded.senderId, msg.senderId);
    EXPECT_TRUE(decoded.messages.isEmpty());
    EXPECT_TRUE(decoded.isRead);
}

TEST_F(MergeForwardMessageTest, MergeForwardProtocolMessage) {
    MergeForwardMessage msg;
    msg.messageId = "msg-protocol";
    msg.senderId = "sender-789";
    msg.senderName = "ProtocolUser";
    msg.timestamp = 1700000000000LL;
    msg.isRead = false;

    ForwardedMessage fwd;
    fwd.messageId = "fwd-1";
    fwd.senderId = "sender-1";
    fwd.senderName = "ForwardedUser";
    fwd.content = "Forwarded content";
    fwd.timestamp = 1700000000001LL;
    fwd.msgType = 2;
    msg.messages.append(fwd);

    QByteArray payload = ProtocolManager::encodeMergeForwardMessage(msg);
    QByteArray message = ProtocolManager::encode(MessageType::MERGE_FORWARD, payload, "session-merge");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::MERGE_FORWARD);
    EXPECT_EQ(sessionId, "session-merge");

    MergeForwardMessage decoded = ProtocolManager::decodeMergeForwardMessage(outPayload);
    EXPECT_EQ(decoded.messageId, msg.messageId);
    EXPECT_EQ(decoded.senderId, msg.senderId);
    EXPECT_EQ(decoded.messages.size(), 1);
    EXPECT_EQ(decoded.messages[0].messageId, "fwd-1");
    EXPECT_EQ(decoded.messages[0].msgType, 2);
}

TEST_F(MergeForwardMessageTest, MergeForwardAckProtocolMessage) {
    QByteArray payload("OK");
    QByteArray message = ProtocolManager::encode(MessageType::MERGE_FORWARD_ACK, payload, "session-ack");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::MERGE_FORWARD_ACK);
    EXPECT_EQ(sessionId, "session-ack");
    EXPECT_EQ(outPayload, payload);
}

TEST_F(MergeForwardMessageTest, MergeForwardMessageTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::MERGE_FORWARD), 151u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::MERGE_FORWARD_ACK), 152u);
}

class CallInviteTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(CallInviteTest, EncodeDecodeRoundTrip) {
    CallInvite invite;
    invite.callId = "call-123";
    invite.callerId = "caller-456";
    invite.callerName = "TestUser";
    invite.callType = "voice";
    invite.sdp = "v=0\r\no=- 123456 2 IN IP4 127.0.0.1\r\ns=-\r\nc=IN IP4 127.0.0.1\r\nt=0 0\r\nm=audio 5000 RTP/AVP 0\r\na=rtpmap:0 PCMU/8000\r\n";
    invite.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeCallInvite(invite);
    EXPECT_FALSE(encoded.isEmpty());

    CallInvite decoded = ProtocolManager::decodeCallInvite(encoded);
    EXPECT_EQ(decoded.callId, invite.callId);
    EXPECT_EQ(decoded.callerId, invite.callerId);
    EXPECT_EQ(decoded.callerName, invite.callerName);
    EXPECT_EQ(decoded.callType, invite.callType);
    EXPECT_EQ(decoded.sdp, invite.sdp);
    EXPECT_EQ(decoded.timestamp, invite.timestamp);
}

TEST_F(CallInviteTest, EncodeDecodeVideoCall) {
    CallInvite invite;
    invite.callId = "call-video";
    invite.callerId = "caller-1";
    invite.callerName = "VideoUser";
    invite.callType = "video";
    invite.sdp = "video sdp offer";
    invite.timestamp = 0;

    QByteArray encoded = ProtocolManager::encodeCallInvite(invite);
    EXPECT_FALSE(encoded.isEmpty());

    CallInvite decoded = ProtocolManager::decodeCallInvite(encoded);
    EXPECT_EQ(decoded.callId, invite.callId);
    EXPECT_EQ(decoded.callType, "video");
}

TEST_F(CallInviteTest, CallInviteProtocolMessage) {
    CallInvite invite;
    invite.callId = "call-proto";
    invite.callerId = "caller-789";
    invite.callerName = "ProtoUser";
    invite.callType = "voice";
    invite.sdp = "sdp offer";
    invite.timestamp = 1700000000000LL;

    QByteArray payload = ProtocolManager::encodeCallInvite(invite);
    QByteArray message = ProtocolManager::encode(MessageType::CALL_INVITE, payload, "session-call");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::CALL_INVITE);
    EXPECT_EQ(sessionId, "session-call");

    CallInvite decoded = ProtocolManager::decodeCallInvite(outPayload);
    EXPECT_EQ(decoded.callId, invite.callId);
    EXPECT_EQ(decoded.callerId, invite.callerId);
    EXPECT_EQ(decoded.sdp, invite.sdp);
}

TEST_F(CallInviteTest, CallInviteTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::CALL_INVITE), 153u);
}

class CallAcceptTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(CallAcceptTest, EncodeDecodeRoundTrip) {
    CallAccept accept;
    accept.callId = "call-123";
    accept.calleeId = "callee-456";
    accept.sdp = "v=0\r\no=- 123456 2 IN IP4 127.0.0.1\r\ns=-\r\nc=IN IP4 127.0.0.1\r\nt=0 0\r\nm=audio 5000 RTP/AVP 0\r\na=rtpmap:0 PCMU/8000\r\n";
    accept.timestamp = 1700000000001LL;

    QByteArray encoded = ProtocolManager::encodeCallAccept(accept);
    EXPECT_FALSE(encoded.isEmpty());

    CallAccept decoded = ProtocolManager::decodeCallAccept(encoded);
    EXPECT_EQ(decoded.callId, accept.callId);
    EXPECT_EQ(decoded.calleeId, accept.calleeId);
    EXPECT_EQ(decoded.sdp, accept.sdp);
    EXPECT_EQ(decoded.timestamp, accept.timestamp);
}

TEST_F(CallAcceptTest, CallAcceptProtocolMessage) {
    CallAccept accept;
    accept.callId = "call-proto";
    accept.calleeId = "callee-1";
    accept.sdp = "sdp answer";
    accept.timestamp = 1700000000000LL;

    QByteArray payload = ProtocolManager::encodeCallAccept(accept);
    QByteArray message = ProtocolManager::encode(MessageType::CALL_ACCEPT, payload, "session-accept");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::CALL_ACCEPT);
    EXPECT_EQ(sessionId, "session-accept");

    CallAccept decoded = ProtocolManager::decodeCallAccept(outPayload);
    EXPECT_EQ(decoded.callId, accept.callId);
    EXPECT_EQ(decoded.calleeId, accept.calleeId);
    EXPECT_EQ(decoded.sdp, accept.sdp);
}

TEST_F(CallAcceptTest, CallAcceptTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::CALL_ACCEPT), 154u);
}

class CallRejectTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(CallRejectTest, EncodeDecodeRoundTrip) {
    CallReject reject;
    reject.callId = "call-123";
    reject.calleeId = "callee-456";
    reject.reason = "Busy";
    reject.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeCallReject(reject);
    EXPECT_FALSE(encoded.isEmpty());

    CallReject decoded = ProtocolManager::decodeCallReject(encoded);
    EXPECT_EQ(decoded.callId, reject.callId);
    EXPECT_EQ(decoded.calleeId, reject.calleeId);
    EXPECT_EQ(decoded.reason, reject.reason);
    EXPECT_EQ(decoded.timestamp, reject.timestamp);
}

TEST_F(CallRejectTest, CallRejectProtocolMessage) {
    CallReject reject;
    reject.callId = "call-proto";
    reject.calleeId = "callee-1";
    reject.reason = "Rejected";
    reject.timestamp = 1700000000000LL;

    QByteArray payload = ProtocolManager::encodeCallReject(reject);
    QByteArray message = ProtocolManager::encode(MessageType::CALL_REJECT, payload, "session-reject");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::CALL_REJECT);
    EXPECT_EQ(sessionId, "session-reject");

    CallReject decoded = ProtocolManager::decodeCallReject(outPayload);
    EXPECT_EQ(decoded.callId, reject.callId);
    EXPECT_EQ(decoded.reason, reject.reason);
}

TEST_F(CallRejectTest, CallRejectTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::CALL_REJECT), 155u);
}

class CallEndTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(CallEndTest, EncodeDecodeRoundTrip) {
    CallEnd end;
    end.callId = "call-123";
    end.peerId = "peer-456";
    end.timestamp = 1700000000002LL;

    QByteArray encoded = ProtocolManager::encodeCallEnd(end);
    EXPECT_FALSE(encoded.isEmpty());

    CallEnd decoded = ProtocolManager::decodeCallEnd(encoded);
    EXPECT_EQ(decoded.callId, end.callId);
    EXPECT_EQ(decoded.peerId, end.peerId);
    EXPECT_EQ(decoded.timestamp, end.timestamp);
}

TEST_F(CallEndTest, CallEndProtocolMessage) {
    CallEnd end;
    end.callId = "call-proto";
    end.peerId = "peer-1";
    end.timestamp = 1700000000000LL;

    QByteArray payload = ProtocolManager::encodeCallEnd(end);
    QByteArray message = ProtocolManager::encode(MessageType::CALL_END, payload, "session-end");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::CALL_END);
    EXPECT_EQ(sessionId, "session-end");

    CallEnd decoded = ProtocolManager::decodeCallEnd(outPayload);
    EXPECT_EQ(decoded.callId, end.callId);
    EXPECT_EQ(decoded.peerId, end.peerId);
}

TEST_F(CallEndTest, CallEndTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::CALL_END), 156u);
}

class IceCandidateTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(IceCandidateTest, EncodeDecodeRoundTrip) {
    IceCandidate candidate;
    candidate.callId = "call-123";
    candidate.candidate = "candidate:1 1 UDP 2122260223 192.168.1.100 5000 typ host";
    candidate.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeIceCandidate(candidate);
    EXPECT_FALSE(encoded.isEmpty());

    IceCandidate decoded = ProtocolManager::decodeIceCandidate(encoded);
    EXPECT_EQ(decoded.callId, candidate.callId);
    EXPECT_EQ(decoded.candidate, candidate.candidate);
    EXPECT_EQ(decoded.timestamp, candidate.timestamp);
}

TEST_F(IceCandidateTest, IceCandidateProtocolMessage) {
    IceCandidate candidate;
    candidate.callId = "call-proto";
    candidate.candidate = "candidate:2 1 UDP 2122260222 192.168.1.101 5001 typ srflx raddr 192.168.1.100 rport 5000";
    candidate.timestamp = 1700000000000LL;

    QByteArray payload = ProtocolManager::encodeIceCandidate(candidate);
    QByteArray message = ProtocolManager::encode(MessageType::ICE_CANDIDATE, payload, "session-ice");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::ICE_CANDIDATE);
    EXPECT_EQ(sessionId, "session-ice");

    IceCandidate decoded = ProtocolManager::decodeIceCandidate(outPayload);
    EXPECT_EQ(decoded.callId, candidate.callId);
    EXPECT_EQ(decoded.candidate, candidate.candidate);
}

TEST_F(IceCandidateTest, IceCandidateTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::ICE_CANDIDATE), 157u);
}

class VideoCallStartTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(VideoCallStartTest, EncodeDecodeRoundTrip) {
    VideoCallStart start;
    start.callId = "call-123";
    start.callerId = "caller-456";
    start.callerName = "TestUser";
    start.width = 1920;
    start.height = 1080;
    start.fps = 30;
    start.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeVideoCallStart(start);
    EXPECT_FALSE(encoded.isEmpty());

    VideoCallStart decoded = ProtocolManager::decodeVideoCallStart(encoded);
    EXPECT_EQ(decoded.callId, start.callId);
    EXPECT_EQ(decoded.callerId, start.callerId);
    EXPECT_EQ(decoded.callerName, start.callerName);
    EXPECT_EQ(decoded.width, start.width);
    EXPECT_EQ(decoded.height, start.height);
    EXPECT_EQ(decoded.fps, start.fps);
    EXPECT_EQ(decoded.timestamp, start.timestamp);
}

TEST_F(VideoCallStartTest, VideoCallStartProtocolMessage) {
    VideoCallStart start;
    start.callId = "call-proto";
    start.callerId = "caller-789";
    start.callerName = "ProtoUser";
    start.width = 1280;
    start.height = 720;
    start.fps = 60;
    start.timestamp = 1700000000000LL;

    QByteArray payload = ProtocolManager::encodeVideoCallStart(start);
    QByteArray message = ProtocolManager::encode(MessageType::VIDEO_CALL_START, payload, "session-videostart");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::VIDEO_CALL_START);
    EXPECT_EQ(sessionId, "session-videostart");

    VideoCallStart decoded = ProtocolManager::decodeVideoCallStart(outPayload);
    EXPECT_EQ(decoded.callId, start.callId);
    EXPECT_EQ(decoded.width, start.width);
    EXPECT_EQ(decoded.height, start.height);
    EXPECT_EQ(decoded.fps, start.fps);
}

TEST_F(VideoCallStartTest, VideoCallStartTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::VIDEO_CALL_START), 250u);
}

class VideoCallStopTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(VideoCallStopTest, EncodeDecodeRoundTrip) {
    VideoCallStop stop;
    stop.callId = "call-123";
    stop.peerId = "peer-456";
    stop.timestamp = 1700000000002LL;

    QByteArray encoded = ProtocolManager::encodeVideoCallStop(stop);
    EXPECT_FALSE(encoded.isEmpty());

    VideoCallStop decoded = ProtocolManager::decodeVideoCallStop(encoded);
    EXPECT_EQ(decoded.callId, stop.callId);
    EXPECT_EQ(decoded.peerId, stop.peerId);
    EXPECT_EQ(decoded.timestamp, stop.timestamp);
}

TEST_F(VideoCallStopTest, VideoCallStopProtocolMessage) {
    VideoCallStop stop;
    stop.callId = "call-proto";
    stop.peerId = "peer-1";
    stop.timestamp = 1700000000000LL;

    QByteArray payload = ProtocolManager::encodeVideoCallStop(stop);
    QByteArray message = ProtocolManager::encode(MessageType::VIDEO_CALL_STOP, payload, "session-videostop");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::VIDEO_CALL_STOP);
    EXPECT_EQ(sessionId, "session-videostop");

    VideoCallStop decoded = ProtocolManager::decodeVideoCallStop(outPayload);
    EXPECT_EQ(decoded.callId, stop.callId);
    EXPECT_EQ(decoded.peerId, stop.peerId);
}

TEST_F(VideoCallStopTest, VideoCallStopTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::VIDEO_CALL_STOP), 251u);
}

class VideoCallFrameTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(VideoCallFrameTest, EncodeDecodeRoundTrip) {
    VideoCallFrame frame;
    frame.callId = "call-123";
    frame.frameData = QByteArray(1024, 'x');
    frame.timestamp = 1700000000000LL;
    frame.sequenceNumber = 1;
    frame.isKeyFrame = true;
    frame.captureTime = 1700000000001LL;

    QByteArray encoded = ProtocolManager::encodeVideoCallFrame(frame);
    EXPECT_FALSE(encoded.isEmpty());

    VideoCallFrame decoded = ProtocolManager::decodeVideoCallFrame(encoded);
    EXPECT_EQ(decoded.callId, frame.callId);
    EXPECT_EQ(decoded.frameData, frame.frameData);
    EXPECT_EQ(decoded.timestamp, frame.timestamp);
    EXPECT_EQ(decoded.sequenceNumber, frame.sequenceNumber);
    EXPECT_EQ(decoded.isKeyFrame, frame.isKeyFrame);
    EXPECT_EQ(decoded.captureTime, frame.captureTime);
}

TEST_F(VideoCallFrameTest, EncodeDecodeNonKeyFrame) {
    VideoCallFrame frame;
    frame.callId = "call-123";
    frame.frameData = QByteArray(512, 'y');
    frame.timestamp = 1700000000001LL;
    frame.sequenceNumber = 2;
    frame.isKeyFrame = false;
    frame.captureTime = 1700000000002LL;

    QByteArray encoded = ProtocolManager::encodeVideoCallFrame(frame);
    EXPECT_FALSE(encoded.isEmpty());

    VideoCallFrame decoded = ProtocolManager::decodeVideoCallFrame(encoded);
    EXPECT_EQ(decoded.callId, frame.callId);
    EXPECT_EQ(decoded.sequenceNumber, 2);
    EXPECT_FALSE(decoded.isKeyFrame);
}

TEST_F(VideoCallFrameTest, VideoCallFrameProtocolMessage) {
    VideoCallFrame frame;
    frame.callId = "call-proto";
    frame.frameData = QByteArray(2048, 'z');
    frame.timestamp = 1700000000000LL;
    frame.sequenceNumber = 1;
    frame.isKeyFrame = true;
    frame.captureTime = 1700000000001LL;

    QByteArray payload = ProtocolManager::encodeVideoCallFrame(frame);
    QByteArray message = ProtocolManager::encode(MessageType::VIDEO_CALL_FRAME, payload, "session-videoframe");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::VIDEO_CALL_FRAME);
    EXPECT_EQ(sessionId, "session-videoframe");

    VideoCallFrame decoded = ProtocolManager::decodeVideoCallFrame(outPayload);
    EXPECT_EQ(decoded.callId, frame.callId);
    EXPECT_EQ(decoded.frameData, frame.frameData);
    EXPECT_EQ(decoded.sequenceNumber, frame.sequenceNumber);
    EXPECT_TRUE(decoded.isKeyFrame);
}

TEST_F(VideoCallFrameTest, VideoCallFrameTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::VIDEO_CALL_FRAME), 252u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::VIDEO_CALL_ACK), 253u);
}

class ScreenShareStartTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ScreenShareStartTest, EncodeDecodeRoundTrip) {
    ScreenShareStart start;
    start.sessionId = "session-123";
    start.callerId = "caller-456";
    start.callerName = "TestUser";
    start.width = 1920;
    start.height = 1080;
    start.fps = 30;
    start.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeScreenShareStart(start);
    EXPECT_FALSE(encoded.isEmpty());

    ScreenShareStart decoded = ProtocolManager::decodeScreenShareStart(encoded);
    EXPECT_EQ(decoded.sessionId, start.sessionId);
    EXPECT_EQ(decoded.callerId, start.callerId);
    EXPECT_EQ(decoded.callerName, start.callerName);
    EXPECT_EQ(decoded.width, start.width);
    EXPECT_EQ(decoded.height, start.height);
    EXPECT_EQ(decoded.fps, start.fps);
    EXPECT_EQ(decoded.timestamp, start.timestamp);
}

TEST_F(ScreenShareStartTest, ScreenShareStartProtocolMessage) {
    ScreenShareStart start;
    start.sessionId = "session-proto";
    start.callerId = "caller-789";
    start.callerName = "ProtoUser";
    start.width = 1280;
    start.height = 720;
    start.fps = 60;
    start.timestamp = 1700000000000LL;

    QByteArray payload = ProtocolManager::encodeScreenShareStart(start);
    QByteArray message = ProtocolManager::encode(MessageType::SCREEN_SHARE_START, payload, "session-screenshare");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::SCREEN_SHARE_START);
    EXPECT_EQ(sessionId, "session-screenshare");

    ScreenShareStart decoded = ProtocolManager::decodeScreenShareStart(outPayload);
    EXPECT_EQ(decoded.sessionId, start.sessionId);
    EXPECT_EQ(decoded.width, start.width);
    EXPECT_EQ(decoded.height, start.height);
    EXPECT_EQ(decoded.fps, start.fps);
}

TEST_F(ScreenShareStartTest, ScreenShareStartTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::SCREEN_SHARE_START), 254u);
}

class ScreenShareStopTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ScreenShareStopTest, EncodeDecodeRoundTrip) {
    ScreenShareStop stop;
    stop.sessionId = "session-123";
    stop.peerId = "peer-456";
    stop.timestamp = 1700000000002LL;

    QByteArray encoded = ProtocolManager::encodeScreenShareStop(stop);
    EXPECT_FALSE(encoded.isEmpty());

    ScreenShareStop decoded = ProtocolManager::decodeScreenShareStop(encoded);
    EXPECT_EQ(decoded.sessionId, stop.sessionId);
    EXPECT_EQ(decoded.peerId, stop.peerId);
    EXPECT_EQ(decoded.timestamp, stop.timestamp);
}

TEST_F(ScreenShareStopTest, ScreenShareStopProtocolMessage) {
    ScreenShareStop stop;
    stop.sessionId = "session-proto";
    stop.peerId = "peer-1";
    stop.timestamp = 1700000000000LL;

    QByteArray payload = ProtocolManager::encodeScreenShareStop(stop);
    QByteArray message = ProtocolManager::encode(MessageType::SCREEN_SHARE_STOP, payload, "session-screenstop");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::SCREEN_SHARE_STOP);
    EXPECT_EQ(sessionId, "session-screenstop");

    ScreenShareStop decoded = ProtocolManager::decodeScreenShareStop(outPayload);
    EXPECT_EQ(decoded.sessionId, stop.sessionId);
    EXPECT_EQ(decoded.peerId, stop.peerId);
}

TEST_F(ScreenShareStopTest, ScreenShareStopTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::SCREEN_SHARE_STOP), 255u);
}

class ScreenShareFrameTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ScreenShareFrameTest, EncodeDecodeRoundTrip) {
    ScreenShareFrame frame;
    frame.sessionId = "session-123";
    frame.frameData = QByteArray(1024, 'x');
    frame.timestamp = 1700000000000LL;
    frame.sequenceNumber = 1;
    frame.isKeyFrame = true;
    frame.captureTime = 1700000000001LL;

    QByteArray encoded = ProtocolManager::encodeScreenShareFrame(frame);
    EXPECT_FALSE(encoded.isEmpty());

    ScreenShareFrame decoded = ProtocolManager::decodeScreenShareFrame(encoded);
    EXPECT_EQ(decoded.sessionId, frame.sessionId);
    EXPECT_EQ(decoded.frameData, frame.frameData);
    EXPECT_EQ(decoded.timestamp, frame.timestamp);
    EXPECT_EQ(decoded.sequenceNumber, frame.sequenceNumber);
    EXPECT_EQ(decoded.isKeyFrame, frame.isKeyFrame);
    EXPECT_EQ(decoded.captureTime, frame.captureTime);
}

TEST_F(ScreenShareFrameTest, EncodeDecodeNonKeyFrame) {
    ScreenShareFrame frame;
    frame.sessionId = "session-123";
    frame.frameData = QByteArray(512, 'y');
    frame.timestamp = 1700000000001LL;
    frame.sequenceNumber = 2;
    frame.isKeyFrame = false;
    frame.captureTime = 1700000000002LL;

    QByteArray encoded = ProtocolManager::encodeScreenShareFrame(frame);
    EXPECT_FALSE(encoded.isEmpty());

    ScreenShareFrame decoded = ProtocolManager::decodeScreenShareFrame(encoded);
    EXPECT_EQ(decoded.sessionId, frame.sessionId);
    EXPECT_EQ(decoded.sequenceNumber, 2);
    EXPECT_FALSE(decoded.isKeyFrame);
}

TEST_F(ScreenShareFrameTest, ScreenShareFrameProtocolMessage) {
    ScreenShareFrame frame;
    frame.sessionId = "session-proto";
    frame.frameData = QByteArray(2048, 'z');
    frame.timestamp = 1700000000000LL;
    frame.sequenceNumber = 1;
    frame.isKeyFrame = true;
    frame.captureTime = 1700000000001LL;

    QByteArray payload = ProtocolManager::encodeScreenShareFrame(frame);
    QByteArray message = ProtocolManager::encode(MessageType::SCREEN_SHARE_FRAME, payload, "session-screenframe");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::SCREEN_SHARE_FRAME);
    EXPECT_EQ(sessionId, "session-screenframe");

    ScreenShareFrame decoded = ProtocolManager::decodeScreenShareFrame(outPayload);
    EXPECT_EQ(decoded.sessionId, frame.sessionId);
    EXPECT_EQ(decoded.frameData, frame.frameData);
    EXPECT_EQ(decoded.sequenceNumber, frame.sequenceNumber);
    EXPECT_TRUE(decoded.isKeyFrame);
}

TEST_F(ScreenShareFrameTest, ScreenShareFrameTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::SCREEN_SHARE_FRAME), 256u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::SCREEN_SHARE_ACK), 257u);
}

class GroupAnnouncementTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(GroupAnnouncementTest, EncodeDecodeRoundTrip) {
    GroupAnnouncement announcement;
    announcement.groupId = "group-123";
    announcement.groupName = "Test Group";
    announcement.announcement = "Welcome to the group!";
    announcement.announcerId = "announcer-456";
    announcement.announcerName = "Admin";
    announcement.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeGroupAnnouncement(announcement);
    EXPECT_FALSE(encoded.isEmpty());

    GroupAnnouncement decoded = ProtocolManager::decodeGroupAnnouncement(encoded);
    EXPECT_EQ(decoded.groupId, announcement.groupId);
    EXPECT_EQ(decoded.groupName, announcement.groupName);
    EXPECT_EQ(decoded.announcement, announcement.announcement);
    EXPECT_EQ(decoded.announcerId, announcement.announcerId);
    EXPECT_EQ(decoded.announcerName, announcement.announcerName);
    EXPECT_EQ(decoded.timestamp, announcement.timestamp);
}

TEST_F(GroupAnnouncementTest, GroupAnnouncementProtocolMessage) {
    GroupAnnouncement announcement;
    announcement.groupId = "group-proto";
    announcement.groupName = "Proto Group";
    announcement.announcement = "Test announcement";
    announcement.announcerId = "announcer-789";
    announcement.announcerName = "ProtoAdmin";
    announcement.timestamp = 1700000000000LL;

    QByteArray payload = ProtocolManager::encodeGroupAnnouncement(announcement);
    QByteArray message = ProtocolManager::encode(MessageType::GROUP_ANNOUNCEMENT, payload, "session-announcement");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::GROUP_ANNOUNCEMENT);
    EXPECT_EQ(sessionId, "session-announcement");

    GroupAnnouncement decoded = ProtocolManager::decodeGroupAnnouncement(outPayload);
    EXPECT_EQ(decoded.groupId, announcement.groupId);
    EXPECT_EQ(decoded.announcement, announcement.announcement);
    EXPECT_EQ(decoded.announcerId, announcement.announcerId);
}

TEST_F(GroupAnnouncementTest, GroupAnnouncementTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::GROUP_ANNOUNCEMENT), 258u);
}

class GroupMentionTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(GroupMentionTest, EncodeDecodeRoundTrip) {
    GroupMention mention;
    mention.groupId = "group-123";
    mention.groupName = "Test Group";
    mention.message = "Hello @User1 @User2";
    mention.mentionedMemberIds = {"user-1", "user-2"};
    mention.mentionedMemberNames = {"User1", "User2"};
    mention.senderId = "sender-456";
    mention.senderName = "Sender";
    mention.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeGroupMention(mention);
    EXPECT_FALSE(encoded.isEmpty());

    GroupMention decoded = ProtocolManager::decodeGroupMention(encoded);
    EXPECT_EQ(decoded.groupId, mention.groupId);
    EXPECT_EQ(decoded.groupName, mention.groupName);
    EXPECT_EQ(decoded.message, mention.message);
    EXPECT_EQ(decoded.mentionedMemberIds, mention.mentionedMemberIds);
    EXPECT_EQ(decoded.mentionedMemberNames, mention.mentionedMemberNames);
    EXPECT_EQ(decoded.senderId, mention.senderId);
    EXPECT_EQ(decoded.senderName, mention.senderName);
    EXPECT_EQ(decoded.timestamp, mention.timestamp);
}

TEST_F(GroupMentionTest, GroupMentionProtocolMessage) {
    GroupMention mention;
    mention.groupId = "group-proto";
    mention.groupName = "Proto Group";
    mention.message = "Test @User1";
    mention.mentionedMemberIds = {"user-1"};
    mention.mentionedMemberNames = {"User1"};
    mention.senderId = "sender-789";
    mention.senderName = "ProtoSender";
    mention.timestamp = 1700000000000LL;

    QByteArray payload = ProtocolManager::encodeGroupMention(mention);
    QByteArray message = ProtocolManager::encode(MessageType::GROUP_MENTION, payload, "session-mention");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::GROUP_MENTION);
    EXPECT_EQ(sessionId, "session-mention");

    GroupMention decoded = ProtocolManager::decodeGroupMention(outPayload);
    EXPECT_EQ(decoded.groupId, mention.groupId);
    EXPECT_EQ(decoded.mentionedMemberIds, mention.mentionedMemberIds);
    EXPECT_EQ(decoded.senderId, mention.senderId);
}

TEST_F(GroupMentionTest, GroupMentionTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::GROUP_MENTION), 259u);
}

class GroupVoteTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(GroupVoteTest, EncodeDecodeRoundTrip) {
    GroupVote vote;
    vote.groupId = "group-123";
    vote.groupName = "Test Group";
    vote.voteTitle = "Best language?";
    vote.options = {"C++", "Python", "Rust"};
    vote.durationSeconds = 3600;
    vote.creatorId = "creator-456";
    vote.creatorName = "Creator";
    vote.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeGroupVote(vote);
    EXPECT_FALSE(encoded.isEmpty());

    GroupVote decoded = ProtocolManager::decodeGroupVote(encoded);
    EXPECT_EQ(decoded.groupId, vote.groupId);
    EXPECT_EQ(decoded.groupName, vote.groupName);
    EXPECT_EQ(decoded.voteTitle, vote.voteTitle);
    EXPECT_EQ(decoded.options, vote.options);
    EXPECT_EQ(decoded.durationSeconds, vote.durationSeconds);
    EXPECT_EQ(decoded.creatorId, vote.creatorId);
    EXPECT_EQ(decoded.creatorName, vote.creatorName);
    EXPECT_EQ(decoded.timestamp, vote.timestamp);
}

TEST_F(GroupVoteTest, GroupVoteProtocolMessage) {
    GroupVote vote;
    vote.groupId = "group-proto";
    vote.groupName = "Proto Group";
    vote.voteTitle = "Vote title";
    vote.options = {"Option A", "Option B"};
    vote.durationSeconds = 1800;
    vote.creatorId = "creator-789";
    vote.creatorName = "ProtoCreator";
    vote.timestamp = 1700000000000LL;

    QByteArray payload = ProtocolManager::encodeGroupVote(vote);
    QByteArray message = ProtocolManager::encode(MessageType::GROUP_VOTE, payload, "session-vote");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::GROUP_VOTE);
    EXPECT_EQ(sessionId, "session-vote");

    GroupVote decoded = ProtocolManager::decodeGroupVote(outPayload);
    EXPECT_EQ(decoded.groupId, vote.groupId);
    EXPECT_EQ(decoded.options, vote.options);
    EXPECT_EQ(decoded.creatorId, vote.creatorId);
}

TEST_F(GroupVoteTest, GroupVoteTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::GROUP_VOTE), 260u);
}

class GroupFileTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(GroupFileTest, EncodeDecodeRoundTrip) {
    GroupFile file;
    file.groupId = "group-123";
    file.groupName = "Test Group";
    file.fileId = "file-456";
    file.fileName = "test.txt";
    file.fileSize = 1024;
    file.md5 = "d41d8cd98f00b204e9800998ecf8427e";
    file.uploaderId = "uploader-789";
    file.uploaderName = "Uploader";
    file.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeGroupFile(file);
    EXPECT_FALSE(encoded.isEmpty());

    GroupFile decoded = ProtocolManager::decodeGroupFile(encoded);
    EXPECT_EQ(decoded.groupId, file.groupId);
    EXPECT_EQ(decoded.groupName, file.groupName);
    EXPECT_EQ(decoded.fileId, file.fileId);
    EXPECT_EQ(decoded.fileName, file.fileName);
    EXPECT_EQ(decoded.fileSize, file.fileSize);
    EXPECT_EQ(decoded.md5, file.md5);
    EXPECT_EQ(decoded.uploaderId, file.uploaderId);
    EXPECT_EQ(decoded.uploaderName, file.uploaderName);
    EXPECT_EQ(decoded.timestamp, file.timestamp);
}

TEST_F(GroupFileTest, GroupFileProtocolMessage) {
    GroupFile file;
    file.groupId = "group-proto";
    file.groupName = "Proto Group";
    file.fileId = "file-proto";
    file.fileName = "proto.txt";
    file.fileSize = 2048;
    file.md5 = "abcdef";
    file.uploaderId = "uploader-1";
    file.uploaderName = "ProtoUploader";
    file.timestamp = 1700000000000LL;

    QByteArray payload = ProtocolManager::encodeGroupFile(file);
    QByteArray message = ProtocolManager::encode(MessageType::GROUP_FILE, payload, "session-groupfile");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::GROUP_FILE);
    EXPECT_EQ(sessionId, "session-groupfile");

    GroupFile decoded = ProtocolManager::decodeGroupFile(outPayload);
    EXPECT_EQ(decoded.groupId, file.groupId);
    EXPECT_EQ(decoded.fileId, file.fileId);
    EXPECT_EQ(decoded.md5, file.md5);
}

TEST_F(GroupFileTest, GroupFileTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::GROUP_FILE), 261u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::GROUP_FILE_ACK), 262u);
}

class GroupAlbumTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(GroupAlbumTest, EncodeDecodeRoundTrip) {
    GroupAlbum album;
    album.groupId = "group-123";
    album.groupName = "Test Group";
    album.albumId = "album-456";
    album.albumName = "Test Album";
    album.fileIds = {"file-1", "file-2"};
    album.fileNames = {"photo1.jpg", "photo2.jpg"};
    album.creatorId = "creator-456";
    album.creatorName = "Creator";
    album.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeGroupAlbum(album);
    EXPECT_FALSE(encoded.isEmpty());

    GroupAlbum decoded = ProtocolManager::decodeGroupAlbum(encoded);
    EXPECT_EQ(decoded.groupId, album.groupId);
    EXPECT_EQ(decoded.groupName, album.groupName);
    EXPECT_EQ(decoded.albumId, album.albumId);
    EXPECT_EQ(decoded.albumName, album.albumName);
    EXPECT_EQ(decoded.fileIds, album.fileIds);
    EXPECT_EQ(decoded.fileNames, album.fileNames);
    EXPECT_EQ(decoded.creatorId, album.creatorId);
    EXPECT_EQ(decoded.creatorName, album.creatorName);
    EXPECT_EQ(decoded.timestamp, album.timestamp);
}

TEST_F(GroupAlbumTest, GroupAlbumProtocolMessage) {
    GroupAlbum album;
    album.groupId = "group-proto";
    album.groupName = "Proto Group";
    album.albumId = "album-proto";
    album.albumName = "Proto Album";
    album.fileIds = {"file-1", "file-2", "file-3"};
    album.fileNames = {"img1.png", "img2.png", "img3.png"};
    album.creatorId = "creator-789";
    album.creatorName = "ProtoCreator";
    album.timestamp = 1700000000000LL;

    QByteArray payload = ProtocolManager::encodeGroupAlbum(album);
    QByteArray message = ProtocolManager::encode(MessageType::GROUP_ALBUM, payload, "session-groupalbum");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::GROUP_ALBUM);
    EXPECT_EQ(sessionId, "session-groupalbum");

    GroupAlbum decoded = ProtocolManager::decodeGroupAlbum(outPayload);
    EXPECT_EQ(decoded.groupId, album.groupId);
    EXPECT_EQ(decoded.fileIds, album.fileIds);
    EXPECT_EQ(decoded.creatorId, album.creatorId);
}

TEST_F(GroupAlbumTest, GroupAlbumTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::GROUP_ALBUM), 263u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::GROUP_ALBUM_ACK), 264u);
}

class GroupTodoTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(GroupTodoTest, EncodeDecodeRoundTrip) {
    GroupTodo todo;
    todo.groupId = "group-123";
    todo.groupName = "Test Group";
    todo.todoId = "todo-456";
    todo.title = "Complete task";
    todo.description = "Finish the implementation";
    todo.status = 1; // in_progress
    todo.priority = 2; // high
    todo.assigneeId = "assignee-456";
    todo.assigneeName = "Assignee";
    todo.creatorId = "creator-789";
    todo.creatorName = "Creator";
    todo.dueDate = 1700000000000LL;
    todo.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeGroupTodo(todo);
    EXPECT_FALSE(encoded.isEmpty());

    GroupTodo decoded = ProtocolManager::decodeGroupTodo(encoded);
    EXPECT_EQ(decoded.groupId, todo.groupId);
    EXPECT_EQ(decoded.groupName, todo.groupName);
    EXPECT_EQ(decoded.todoId, todo.todoId);
    EXPECT_EQ(decoded.title, todo.title);
    EXPECT_EQ(decoded.description, todo.description);
    EXPECT_EQ(decoded.status, todo.status);
    EXPECT_EQ(decoded.priority, todo.priority);
    EXPECT_EQ(decoded.assigneeId, todo.assigneeId);
    EXPECT_EQ(decoded.assigneeName, todo.assigneeName);
    EXPECT_EQ(decoded.creatorId, todo.creatorId);
    EXPECT_EQ(decoded.creatorName, todo.creatorName);
    EXPECT_EQ(decoded.dueDate, todo.dueDate);
    EXPECT_EQ(decoded.timestamp, todo.timestamp);
}

TEST_F(GroupTodoTest, GroupTodoProtocolMessage) {
    GroupTodo todo;
    todo.groupId = "group-proto";
    todo.groupName = "Proto Group";
    todo.todoId = "todo-proto";
    todo.title = "Test todo";
    todo.description = "Test description";
    todo.status = 2; // completed
    todo.priority = 0; // low
    todo.assigneeId = "assignee-1";
    todo.assigneeName = "ProtoAssignee";
    todo.creatorId = "creator-1";
    todo.creatorName = "ProtoCreator";
    todo.dueDate = 1700000000000LL;
    todo.timestamp = 1700000000000LL;

    QByteArray payload = ProtocolManager::encodeGroupTodo(todo);
    QByteArray message = ProtocolManager::encode(MessageType::GROUP_TODO, payload, "session-todo");
    EXPECT_FALSE(message.isEmpty());

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    EXPECT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::GROUP_TODO);
    EXPECT_EQ(sessionId, "session-todo");

    GroupTodo decoded = ProtocolManager::decodeGroupTodo(outPayload);
    EXPECT_EQ(decoded.groupId, todo.groupId);
    EXPECT_EQ(decoded.status, todo.status);
    EXPECT_EQ(decoded.priority, todo.priority);
}

TEST_F(GroupTodoTest, GroupTodoTypesExist) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::GROUP_TODO), 265u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::GROUP_TODO_ACK), 266u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::GROUP_TODO_UPDATE), 267u);
}