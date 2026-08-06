#include <gtest/gtest.h>
#include "core/protocol_manager.h"
#include "core/message_codec.h"
#include "core/types.h"

using namespace xrk;

class ProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
    
    void TearDown() override {
    }
};

TEST_F(ProtocolTest, EncodeDeviceInfo) {
    DeviceInfo info;
    info.deviceId = "test-123";
    info.deviceName = "Test Device";
    info.ipAddress = "192.168.1.100";
    info.port = 9999;
    info.version = "1.0.0";
    info.timestamp = 1234567890;
    
    QByteArray encoded = ProtocolManager::encodeDeviceInfo(info);
    EXPECT_FALSE(encoded.isEmpty());
    
    DeviceInfo decoded = ProtocolManager::decodeDeviceInfo(encoded);
    EXPECT_EQ(decoded.deviceId, info.deviceId);
    EXPECT_EQ(decoded.deviceName, info.deviceName);
    EXPECT_EQ(decoded.ipAddress, info.ipAddress);
    EXPECT_EQ(decoded.port, info.port);
    EXPECT_EQ(decoded.version, info.version);
    EXPECT_EQ(decoded.timestamp, info.timestamp);
}

TEST_F(ProtocolTest, EncodeMouseEvent) {
    MouseEvent event;
    event.x = 100;
    event.y = 200;
    event.action = MouseAction::CLICK;
    event.button = MouseButton::LEFT;
    event.delta = 0;
    
    QByteArray encoded = ProtocolManager::encodeMouseEvent(event);
    EXPECT_FALSE(encoded.isEmpty());
    
    MouseEvent decoded = ProtocolManager::decodeMouseEvent(encoded);
    EXPECT_EQ(decoded.x, event.x);
    EXPECT_EQ(decoded.y, event.y);
    EXPECT_EQ(decoded.action, event.action);
    EXPECT_EQ(decoded.button, event.button);
    EXPECT_EQ(decoded.delta, event.delta);
}

TEST_F(ProtocolTest, EncodeKeyEvent) {
    KeyEvent event;
    event.keyCode = 65;
    event.pressed = true;
    event.modifiers = 0x0001;
    event.text = QStringLiteral("A");

    QByteArray encoded = ProtocolManager::encodeKeyEvent(event);
    EXPECT_FALSE(encoded.isEmpty());

    KeyEvent decoded = ProtocolManager::decodeKeyEvent(encoded);
    EXPECT_EQ(decoded.keyCode, event.keyCode);
    EXPECT_EQ(decoded.pressed, event.pressed);
    EXPECT_EQ(decoded.modifiers, event.modifiers);
    EXPECT_EQ(decoded.text, event.text);
}

TEST_F(ProtocolTest, EncodeKeyEventUnicode) {
    // CJK / surrogate characters must survive the wire intact so the host can
    // inject them via KEYEVENTF_UNICODE regardless of its keyboard layout.
    KeyEvent event;
    event.keyCode = 0;
    event.pressed = true;
    event.text = QStringLiteral("你好");

    QByteArray encoded = ProtocolManager::encodeKeyEvent(event);
    KeyEvent decoded = ProtocolManager::decodeKeyEvent(encoded);
    EXPECT_EQ(decoded.text, event.text);
}

TEST_F(ProtocolTest, EncodeScreenFrame) {
    ScreenFrame frame;
    frame.width = 1920;
    frame.height = 1080;
    frame.format = FrameFormat::JPEG;
    frame.timestamp = 1234567890;
    frame.data = QByteArray(1024, 'x');
    
    QByteArray encoded = ProtocolManager::encodeScreenFrame(frame);
    EXPECT_FALSE(encoded.isEmpty());
    
    ScreenFrame decoded = ProtocolManager::decodeScreenFrame(encoded);
    EXPECT_EQ(decoded.width, frame.width);
    EXPECT_EQ(decoded.height, frame.height);
    EXPECT_EQ(decoded.format, frame.format);
    EXPECT_EQ(decoded.timestamp, frame.timestamp);
    EXPECT_EQ(decoded.data, frame.data);
}

TEST_F(ProtocolTest, EncodeFileRequest) {
    FileRequest request;
    request.fileId = "file-123";
    request.fileName = "test.txt";
    request.fileSize = 1024;
    request.offset = 0;
    request.isUpload = true;
    
    QByteArray encoded = ProtocolManager::encodeFileRequest(request);
    EXPECT_FALSE(encoded.isEmpty());
    
    FileRequest decoded = ProtocolManager::decodeFileRequest(encoded);
    EXPECT_EQ(decoded.fileId, request.fileId);
    EXPECT_EQ(decoded.fileName, request.fileName);
    EXPECT_EQ(decoded.fileSize, request.fileSize);
    EXPECT_EQ(decoded.offset, request.offset);
    EXPECT_EQ(decoded.isUpload, request.isUpload);
}

TEST_F(ProtocolTest, EncodeFileData) {
    FileData fileData;
    fileData.fileId = "file-123";
    fileData.offset = 1024;
    fileData.data = QByteArray(512, 'y');
    
    QByteArray encoded = ProtocolManager::encodeFileData(fileData);
    EXPECT_FALSE(encoded.isEmpty());
    
    FileData decoded = ProtocolManager::decodeFileData(encoded);
    EXPECT_EQ(decoded.fileId, fileData.fileId);
    EXPECT_EQ(decoded.offset, fileData.offset);
    EXPECT_EQ(decoded.data, fileData.data);
}

TEST_F(ProtocolTest, Checksum) {
    QByteArray data = "test data for checksum";
    uint32_t checksum = ProtocolManager::calculateChecksum(data);
    EXPECT_NE(checksum, 0u);
}

TEST_F(ProtocolTest, Validate) {
    QByteArray data = "test data";
    QByteArray withChecksum = data + MessageCodec::calculateChecksum(data);
    
    EXPECT_TRUE(ProtocolManager::validate(withChecksum));
}

TEST_F(ProtocolTest, MessageCodecEncodeDecode) {
    QByteArray payload = "hello world";
    MessageType type = MessageType::HEARTBEAT;
    QString sessionId = "session-123";
    
    QByteArray encoded = MessageCodec::encode(type, payload, sessionId);
    EXPECT_FALSE(encoded.isEmpty());
    
    MessageType decodedType;
    QByteArray decodedPayload;
    QString decodedSessionId;
    
    EXPECT_TRUE(MessageCodec::decode(encoded, decodedType, decodedPayload, decodedSessionId));
    EXPECT_EQ(decodedType, type);
    EXPECT_EQ(decodedPayload, payload);
    EXPECT_EQ(decodedSessionId, sessionId);
}

TEST_F(ProtocolTest, MessageCodecAllTypes) {
    MessageType types[] = {
        MessageType::DEVICE_DISCOVER_REQ,
        MessageType::DEVICE_DISCOVER_RESP,
        MessageType::CONNECT_REQ,
        MessageType::CONNECT_RESP,
        MessageType::DISCONNECT_REQ,
        MessageType::AUTH_REQ,
        MessageType::AUTH_RESP,
        MessageType::SCREEN_FRAME,
        MessageType::SCREEN_FRAME_ACK,
        MessageType::MOUSE_EVENT,
        MessageType::KEY_EVENT,
        MessageType::FILE_REQ,
        MessageType::FILE_DATA,
        MessageType::FILE_ACK,
        MessageType::HEARTBEAT,
        MessageType::HEARTBEAT_RESP
    };
    
    for (MessageType type : types) {
        QByteArray payload = "test payload";
        QString sessionId = "test-session";
        
        QByteArray encoded = MessageCodec::encode(type, payload, sessionId);
        EXPECT_FALSE(encoded.isEmpty());
        
        MessageType decodedType;
        QByteArray decodedPayload;
        QString decodedSessionId;
        
        EXPECT_TRUE(MessageCodec::decode(encoded, decodedType, decodedPayload, decodedSessionId));
        EXPECT_EQ(decodedType, type);
        EXPECT_EQ(decodedPayload, payload);
        EXPECT_EQ(decodedSessionId, sessionId);
    }
}

TEST_F(ProtocolTest, MessageCodecEmptyPayload) {
    QByteArray payload;
    MessageType type = MessageType::HEARTBEAT;
    QString sessionId = "session-123";
    
    QByteArray encoded = MessageCodec::encode(type, payload, sessionId);
    EXPECT_FALSE(encoded.isEmpty());
    
    MessageType decodedType;
    QByteArray decodedPayload;
    QString decodedSessionId;
    
    EXPECT_TRUE(MessageCodec::decode(encoded, decodedType, decodedPayload, decodedSessionId));
    EXPECT_EQ(decodedType, type);
    EXPECT_EQ(decodedPayload, payload);
    EXPECT_EQ(decodedSessionId, sessionId);
}

TEST_F(ProtocolTest, MessageCodecLargePayload) {
    QByteArray payload(65536, 'z');
    MessageType type = MessageType::SCREEN_FRAME;
    QString sessionId = "session-456";
    
    QByteArray encoded = MessageCodec::encode(type, payload, sessionId);
    EXPECT_FALSE(encoded.isEmpty());
    
    MessageType decodedType;
    QByteArray decodedPayload;
    QString decodedSessionId;
    
    EXPECT_TRUE(MessageCodec::decode(encoded, decodedType, decodedPayload, decodedSessionId));
    EXPECT_EQ(decodedType, type);
    EXPECT_EQ(decodedPayload, payload);
    EXPECT_EQ(decodedSessionId, sessionId);
}

TEST_F(ProtocolTest, EncodeQualityRequest) {
    struct Case { QualityLevel level; bool game; };
    Case cases[] = {
        {QualityLevel::AUTO, false},
        {QualityLevel::LOW, false},
        {QualityLevel::MEDIUM, false},
        {QualityLevel::HIGH, false},
        {QualityLevel::ULTRA, true},
    };
    for (const Case& c : cases) {
        QualityRequest req;
        req.level = c.level;
        req.gameMode = c.game;

        QByteArray encoded = ProtocolManager::encodeQualityRequest(req);
        EXPECT_FALSE(encoded.isEmpty());

        QualityRequest decoded = ProtocolManager::decodeQualityRequest(encoded);
        EXPECT_EQ(decoded.level, c.level);
        EXPECT_EQ(decoded.gameMode, c.game);
    }
}

TEST_F(ProtocolTest, QualityRequestRoundTripThroughMessage) {
    QualityRequest req;
    req.level = QualityLevel::HIGH;
    req.gameMode = false;

    QByteArray payload = ProtocolManager::encodeQualityRequest(req);
    QByteArray message = ProtocolManager::encode(MessageType::SET_QUALITY, payload, "sess-1");

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    ASSERT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::SET_QUALITY);
    EXPECT_EQ(sessionId, "sess-1");

    QualityRequest decoded = ProtocolManager::decodeQualityRequest(outPayload);
    EXPECT_EQ(decoded.level, QualityLevel::HIGH);
    EXPECT_FALSE(decoded.gameMode);
}

TEST_F(ProtocolTest, EncodeSyncPair) {
    SyncPair pair;
    pair.hostDir = "C:\\Host\\Watch";
    pair.localDir = "D:/Local/Target";

    QByteArray encoded = ProtocolManager::encodeSyncPair(pair);
    EXPECT_FALSE(encoded.isEmpty());
    SyncPair decoded = ProtocolManager::decodeSyncPair(encoded);
    EXPECT_EQ(decoded.hostDir, pair.hostDir);
    EXPECT_EQ(decoded.localDir, pair.localDir);
}

TEST_F(ProtocolTest, EncodeSyncNotify) {
    SyncNotify note;
    note.hostDir = "C:\\Host\\Watch";
    note.hostFilePath = "C:\\Host\\Watch\\sub\\file.txt";
    note.localDir = "D:/Local/Target";
    note.size = 1234;
    note.mtime = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeSyncNotify(note);
    EXPECT_FALSE(encoded.isEmpty());
    SyncNotify decoded = ProtocolManager::decodeSyncNotify(encoded);
    EXPECT_EQ(decoded.hostDir, note.hostDir);
    EXPECT_EQ(decoded.hostFilePath, note.hostFilePath);
    EXPECT_EQ(decoded.localDir, note.localDir);
    EXPECT_EQ(decoded.size, note.size);
    EXPECT_EQ(decoded.mtime, note.mtime);
}
