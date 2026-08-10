#include <gtest/gtest.h>
#include "core/protocol_manager.h"
#include "core/types.h"
#include "core/permission_model.h"

using namespace xrk;

class ProtocolExtendedTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ProtocolExtendedTest, MonitorInfoRoundTrip) {
    MonitorInfo info;
    info.index = 1;
    info.name = "DELL U2723QE";
    info.x = -1920;
    info.y = 0;
    info.width = 2560;
    info.height = 1440;
    info.isPrimary = true;

    QByteArray encoded = ProtocolManager::encodeMonitorInfo(info);
    EXPECT_FALSE(encoded.isEmpty());

    MonitorInfo decoded = ProtocolManager::decodeMonitorInfo(encoded);
    EXPECT_EQ(decoded.index, info.index);
    EXPECT_EQ(decoded.name, info.name);
    EXPECT_EQ(decoded.x, info.x);
    EXPECT_EQ(decoded.y, info.y);
    EXPECT_EQ(decoded.width, info.width);
    EXPECT_EQ(decoded.height, info.height);
    EXPECT_EQ(decoded.isPrimary, info.isPrimary);
}

TEST_F(ProtocolExtendedTest, MonitorInfoNegativeCoordinates) {
    MonitorInfo info;
    info.index = 0;
    info.name = "Left Monitor";
    info.x = -3840;
    info.y = -1080;
    info.width = 1920;
    info.height = 1080;
    info.isPrimary = false;

    QByteArray encoded = ProtocolManager::encodeMonitorInfo(info);
    MonitorInfo decoded = ProtocolManager::decodeMonitorInfo(encoded);
    EXPECT_EQ(decoded.x, -3840);
    EXPECT_EQ(decoded.y, -1080);
}

TEST_F(ProtocolExtendedTest, MonitorInfoEmptyName) {
    MonitorInfo info;
    info.index = 0;
    info.name = "";
    info.width = 1920;
    info.height = 1080;
    info.isPrimary = true;

    QByteArray encoded = ProtocolManager::encodeMonitorInfo(info);
    MonitorInfo decoded = ProtocolManager::decodeMonitorInfo(encoded);
    EXPECT_TRUE(decoded.name.isEmpty());
    EXPECT_EQ(decoded.width, 1920);
}

TEST_F(ProtocolExtendedTest, MonitorListRoundTrip) {
    QList<MonitorInfo> monitors;
    MonitorInfo m1;
    m1.index = 0; m1.name = "Primary"; m1.x = 0; m1.y = 0;
    m1.width = 1920; m1.height = 1080; m1.isPrimary = true;
    MonitorInfo m2;
    m2.index = 1; m2.name = "Secondary"; m2.x = 1920; m2.y = 0;
    m2.width = 2560; m2.height = 1440; m2.isPrimary = false;
    monitors.append(m1);
    monitors.append(m2);

    int currentIdx = 1;
    QByteArray encoded = ProtocolManager::encodeMonitorList(monitors, currentIdx);
    EXPECT_FALSE(encoded.isEmpty());

    int decodedIdx = 0;
    QList<MonitorInfo> decoded = ProtocolManager::decodeMonitorList(encoded, decodedIdx);
    EXPECT_EQ(decodedIdx, currentIdx);
    EXPECT_EQ(decoded.size(), 2);
    EXPECT_EQ(decoded[0].name, "Primary");
    EXPECT_EQ(decoded[1].name, "Secondary");
    EXPECT_EQ(decoded[0].isPrimary, true);
    EXPECT_EQ(decoded[1].isPrimary, false);
}

TEST_F(ProtocolExtendedTest, MonitorListEmpty) {
    QList<MonitorInfo> monitors;
    int currentIdx = 0;
    QByteArray encoded = ProtocolManager::encodeMonitorList(monitors, currentIdx);

    int decodedIdx = -1;
    QList<MonitorInfo> decoded = ProtocolManager::decodeMonitorList(encoded, decodedIdx);
    EXPECT_EQ(decodedIdx, 0);
    EXPECT_TRUE(decoded.isEmpty());
}

TEST_F(ProtocolExtendedTest, MonitorListSingle) {
    QList<MonitorInfo> monitors;
    MonitorInfo m;
    m.index = 0; m.name = "Only"; m.width = 3840; m.height = 2160; m.isPrimary = true;
    monitors.append(m);

    QByteArray encoded = ProtocolManager::encodeMonitorList(monitors, 0);
    int decodedIdx = -1;
    QList<MonitorInfo> decoded = ProtocolManager::decodeMonitorList(encoded, decodedIdx);
    EXPECT_EQ(decoded.size(), 1);
    EXPECT_EQ(decoded[0].width, 3840);
}

TEST_F(ProtocolExtendedTest, ClipboardDataRoundTrip) {
    ClipboardData data;
    data.mimeType = "text/plain";
    data.data = "Hello, clipboard!";
    data.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeClipboardData(data);
    EXPECT_FALSE(encoded.isEmpty());

    ClipboardData decoded = ProtocolManager::decodeClipboardData(encoded);
    EXPECT_EQ(decoded.mimeType, data.mimeType);
    EXPECT_EQ(decoded.data, data.data);
    EXPECT_EQ(decoded.timestamp, data.timestamp);
}

TEST_F(ProtocolExtendedTest, ClipboardDataBinary) {
    ClipboardData data;
    data.mimeType = "image/png";
    data.data = QByteArray(256, '\xff');
    data.timestamp = 12345;

    QByteArray encoded = ProtocolManager::encodeClipboardData(data);
    ClipboardData decoded = ProtocolManager::decodeClipboardData(encoded);
    EXPECT_EQ(decoded.mimeType, "image/png");
    EXPECT_EQ(decoded.data.size(), 256);
}

TEST_F(ProtocolExtendedTest, ClipboardDataEmpty) {
    ClipboardData data;
    data.mimeType = "";
    data.data = "";
    data.timestamp = 0;

    QByteArray encoded = ProtocolManager::encodeClipboardData(data);
    ClipboardData decoded = ProtocolManager::decodeClipboardData(encoded);
    EXPECT_TRUE(decoded.mimeType.isEmpty());
    EXPECT_TRUE(decoded.data.isEmpty());
    EXPECT_EQ(decoded.timestamp, 0);
}

TEST_F(ProtocolExtendedTest, ChatMessageRoundTrip) {
    ChatMessage msg;
    msg.sender = "Alice";
    msg.content = "Hello, World!";
    msg.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeChatMessage(msg);
    EXPECT_FALSE(encoded.isEmpty());

    ChatMessage decoded = ProtocolManager::decodeChatMessage(encoded);
    EXPECT_EQ(decoded.sender, msg.sender);
    EXPECT_EQ(decoded.content, msg.content);
    EXPECT_EQ(decoded.timestamp, msg.timestamp);
}

TEST_F(ProtocolExtendedTest, ChatMessageUnicode) {
    ChatMessage msg;
    msg.sender = "User";
    msg.content = QStringLiteral("你好世界");
    msg.timestamp = 999;

    QByteArray encoded = ProtocolManager::encodeChatMessage(msg);
    ChatMessage decoded = ProtocolManager::decodeChatMessage(encoded);
    EXPECT_EQ(decoded.content, msg.content);
}

TEST_F(ProtocolExtendedTest, ChatMessageEmpty) {
    ChatMessage msg;
    msg.sender = "";
    msg.content = "";
    msg.timestamp = 0;

    QByteArray encoded = ProtocolManager::encodeChatMessage(msg);
    ChatMessage decoded = ProtocolManager::decodeChatMessage(encoded);
    EXPECT_TRUE(decoded.sender.isEmpty());
    EXPECT_TRUE(decoded.content.isEmpty());
}

TEST_F(ProtocolExtendedTest, EncryptionKeyExchangeRoundTrip) {
    EncryptionKeyExchange ex;
    ex.publicKey = QByteArray(32, 'K');
    ex.nonce = QByteArray(16, 'N');

    QByteArray encoded = ProtocolManager::encodeEncryptionKeyExchange(ex);
    EXPECT_FALSE(encoded.isEmpty());

    EncryptionKeyExchange decoded = ProtocolManager::decodeEncryptionKeyExchange(encoded);
    EXPECT_EQ(decoded.publicKey, ex.publicKey);
    EXPECT_EQ(decoded.nonce, ex.nonce);
}

TEST_F(ProtocolExtendedTest, TerminalDataRoundTrip) {
    TerminalData data;
    data.data = "dir /s\r\nC:\\>";
    data.cols = 120;
    data.rows = 40;
    data.shellType = "powershell";

    QByteArray encoded = ProtocolManager::encodeTerminalData(data);
    EXPECT_FALSE(encoded.isEmpty());

    TerminalData decoded = ProtocolManager::decodeTerminalData(encoded);
    EXPECT_EQ(decoded.data, data.data);
    EXPECT_EQ(decoded.cols, data.cols);
    EXPECT_EQ(decoded.rows, data.rows);
    EXPECT_EQ(decoded.shellType, data.shellType);
}

TEST_F(ProtocolExtendedTest, TerminalDataDefaultShell) {
    TerminalData data;
    data.data = "echo hello";
    data.cols = 80;
    data.rows = 25;
    data.shellType = "cmd";

    QByteArray encoded = ProtocolManager::encodeTerminalData(data);
    TerminalData decoded = ProtocolManager::decodeTerminalData(encoded);
    EXPECT_EQ(decoded.shellType, "cmd");
    EXPECT_EQ(decoded.cols, 80);
}

TEST_F(ProtocolExtendedTest, RecordControlRoundTrip) {
    RecordControl ctrl;
    ctrl.filePath = "C:\\recordings\\test.mp4";
    ctrl.fps = 60;

    QByteArray encoded = ProtocolManager::encodeRecordControl(ctrl);
    EXPECT_FALSE(encoded.isEmpty());

    RecordControl decoded = ProtocolManager::decodeRecordControl(encoded);
    EXPECT_EQ(decoded.filePath, ctrl.filePath);
    EXPECT_EQ(decoded.fps, ctrl.fps);
}

TEST_F(ProtocolExtendedTest, FileBrowserRequestRoundTrip) {
    FileBrowserRequest req;
    req.path = "C:\\Users";
    req.filter = "*.txt";

    QByteArray encoded = ProtocolManager::encodeFileBrowserRequest(req);
    EXPECT_FALSE(encoded.isEmpty());

    FileBrowserRequest decoded = ProtocolManager::decodeFileBrowserRequest(encoded);
    EXPECT_EQ(decoded.path, req.path);
    EXPECT_EQ(decoded.filter, req.filter);
}

TEST_F(ProtocolExtendedTest, FileBrowserRequestEmptyFilter) {
    FileBrowserRequest req;
    req.path = "D:\\";
    req.filter = "";

    QByteArray encoded = ProtocolManager::encodeFileBrowserRequest(req);
    FileBrowserRequest decoded = ProtocolManager::decodeFileBrowserRequest(encoded);
    EXPECT_EQ(decoded.path, "D:\\");
    EXPECT_TRUE(decoded.filter.isEmpty());
}

TEST_F(ProtocolExtendedTest, FileBrowserEntryRoundTrip) {
    FileBrowserEntry entry;
    entry.name = "document.pdf";
    entry.path = "C:\\Users\\docs\\document.pdf";
    entry.isDir = false;
    entry.fileSize = 1048576;
    entry.lastModified = "2024-01-15 10:30:00";

    QByteArray encoded = ProtocolManager::encodeFileBrowserEntry(entry);
    EXPECT_FALSE(encoded.isEmpty());

    QDataStream stream(encoded);
    stream.setByteOrder(QDataStream::BigEndian);
    FileBrowserEntry decoded = ProtocolManager::decodeFileBrowserEntry(stream, encoded);
    EXPECT_EQ(decoded.name, entry.name);
    EXPECT_EQ(decoded.path, entry.path);
    EXPECT_EQ(decoded.isDir, entry.isDir);
    EXPECT_EQ(decoded.fileSize, entry.fileSize);
    EXPECT_EQ(decoded.lastModified, entry.lastModified);
}

TEST_F(ProtocolExtendedTest, FileBrowserEntryDirectory) {
    FileBrowserEntry entry;
    entry.name = "Subfolder";
    entry.path = "C:\\Users\\docs\\Subfolder";
    entry.isDir = true;
    entry.fileSize = 0;
    entry.lastModified = "2024-01-15 10:30:00";

    QByteArray encoded = ProtocolManager::encodeFileBrowserEntry(entry);
    QDataStream stream(encoded);
    stream.setByteOrder(QDataStream::BigEndian);
    FileBrowserEntry decoded = ProtocolManager::decodeFileBrowserEntry(stream, encoded);
    EXPECT_TRUE(decoded.isDir);
    EXPECT_EQ(decoded.fileSize, 0u);
}

TEST_F(ProtocolExtendedTest, FileBrowserResponseRoundTrip) {
    FileBrowserResponse resp;
    resp.path = "C:\\Users";
    resp.success = true;
    resp.errorMessage = "";

    FileBrowserEntry e1;
    e1.name = "file1.txt"; e1.path = "C:\\Users\\file1.txt";
    e1.isDir = false; e1.fileSize = 100; e1.lastModified = "2024-01-01";
    FileBrowserEntry e2;
    e2.name = "folder1"; e2.path = "C:\\Users\\folder1";
    e2.isDir = true; e2.fileSize = 0; e2.lastModified = "2024-01-02";

    resp.entries.append(e1);
    resp.entries.append(e2);

    QByteArray encoded = ProtocolManager::encodeFileBrowserResponse(resp);
    EXPECT_FALSE(encoded.isEmpty());

    FileBrowserResponse decoded = ProtocolManager::decodeFileBrowserResponse(encoded);
    EXPECT_EQ(decoded.path, resp.path);
    EXPECT_EQ(decoded.success, true);
    EXPECT_TRUE(decoded.errorMessage.isEmpty());
    EXPECT_EQ(decoded.entries.size(), 2);
    EXPECT_EQ(decoded.entries[0].name, "file1.txt");
    EXPECT_EQ(decoded.entries[1].name, "folder1");
    EXPECT_TRUE(decoded.entries[1].isDir);
}

TEST_F(ProtocolExtendedTest, FileBrowserResponseError) {
    FileBrowserResponse resp;
    resp.path = "Z:\\nonexistent";
    resp.success = false;
    resp.errorMessage = "Access denied";

    QByteArray encoded = ProtocolManager::encodeFileBrowserResponse(resp);
    FileBrowserResponse decoded = ProtocolManager::decodeFileBrowserResponse(encoded);
    EXPECT_FALSE(decoded.success);
    EXPECT_EQ(decoded.errorMessage, "Access denied");
}

TEST_F(ProtocolExtendedTest, SysInfoRoundTrip) {
    SysInfo info;
    info.cpuUsage = 45.5;
    info.memoryUsage = 72.3;
    info.memoryTotal = 17179869184ULL;
    info.memoryAvailable = 4831838208ULL;
    info.diskUsage = 65.0;
    info.diskTotal = 512110190592ULL;
    info.diskFree = 179238566912ULL;
    info.networkRx = 104857600;
    info.networkTx = 52428800;
    info.osName = "Windows 11";
    info.osVersion = "23H2";
    info.cpuName = "Intel Core i9-13900K";
    info.uptime = 86400;
    info.processCount = 312;

    QByteArray encoded = ProtocolManager::encodeSysInfo(info);
    EXPECT_FALSE(encoded.isEmpty());

    SysInfo decoded = ProtocolManager::decodeSysInfo(encoded);
    EXPECT_DOUBLE_EQ(decoded.cpuUsage, info.cpuUsage);
    EXPECT_DOUBLE_EQ(decoded.memoryUsage, info.memoryUsage);
    EXPECT_EQ(decoded.memoryTotal, info.memoryTotal);
    EXPECT_EQ(decoded.memoryAvailable, info.memoryAvailable);
    EXPECT_DOUBLE_EQ(decoded.diskUsage, info.diskUsage);
    EXPECT_EQ(decoded.diskTotal, info.diskTotal);
    EXPECT_EQ(decoded.diskFree, info.diskFree);
    EXPECT_EQ(decoded.networkRx, info.networkRx);
    EXPECT_EQ(decoded.networkTx, info.networkTx);
    EXPECT_EQ(decoded.osName, info.osName);
    EXPECT_EQ(decoded.osVersion, info.osVersion);
    EXPECT_EQ(decoded.cpuName, info.cpuName);
    EXPECT_EQ(decoded.uptime, info.uptime);
    EXPECT_EQ(decoded.processCount, info.processCount);
}

TEST_F(ProtocolExtendedTest, ConsentRoundTrip) {
    for (bool allowed : {true, false}) {
        bool decodedAllowed = !allowed;
        QString decodedName;
        QByteArray data = ProtocolManager::encodeConsent(allowed, "MyDevice");
        ProtocolManager::decodeConsent(data, decodedAllowed, decodedName);
        EXPECT_EQ(decodedAllowed, allowed);
        EXPECT_EQ(decodedName, "MyDevice");
    }
}

TEST_F(ProtocolExtendedTest, ConsentEmptyDeviceName) {
    bool allowed = false;
    QString name;
    QByteArray data = ProtocolManager::encodeConsent(true, "");
    ProtocolManager::decodeConsent(data, allowed, name);
    EXPECT_TRUE(allowed);
    EXPECT_TRUE(name.isEmpty());
}

TEST_F(ProtocolExtendedTest, ConsentEmptyPayload) {
    bool allowed = false;
    QString name;
    ProtocolManager::decodeConsent(QByteArray(), allowed, name);
    EXPECT_FALSE(allowed);
    EXPECT_TRUE(name.isEmpty());
}

TEST_F(ProtocolExtendedTest, QualityInfoRoundTrip) {
    QualityInfo info;
    info.jpegQuality = 85;
    info.captureFps = 60;
    info.targetFps = 30;
    info.bandwidthKbps = 10000;
    info.roundTripMs = 25;

    QByteArray encoded = ProtocolManager::encodeQualityInfo(info);
    EXPECT_FALSE(encoded.isEmpty());

    QualityInfo decoded = ProtocolManager::decodeQualityInfo(encoded);
    EXPECT_EQ(decoded.jpegQuality, 85);
    EXPECT_EQ(decoded.captureFps, 60);
    EXPECT_EQ(decoded.targetFps, 30);
    EXPECT_EQ(decoded.bandwidthKbps, 10000);
    EXPECT_EQ(decoded.roundTripMs, 25);
}

TEST_F(ProtocolExtendedTest, QualityInfoZero) {
    QualityInfo info;
    info.jpegQuality = 0;
    info.captureFps = 0;
    info.targetFps = 0;
    info.bandwidthKbps = 0;
    info.roundTripMs = 0;

    QByteArray encoded = ProtocolManager::encodeQualityInfo(info);
    QualityInfo decoded = ProtocolManager::decodeQualityInfo(encoded);
    EXPECT_EQ(decoded.jpegQuality, 0);
    EXPECT_EQ(decoded.roundTripMs, 0);
}

TEST_F(ProtocolExtendedTest, SyncRequestRoundTrip) {
    SyncRequest req;
    req.requestId = "sync-001";
    req.accountHash = "abc123";
    req.syncKeyHash = "def456";
    req.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeSyncRequest(req);
    EXPECT_FALSE(encoded.isEmpty());

    SyncRequest decoded = ProtocolManager::decodeSyncRequest(encoded);
    EXPECT_EQ(decoded.requestId, req.requestId);
    EXPECT_EQ(decoded.accountHash, req.accountHash);
    EXPECT_EQ(decoded.syncKeyHash, req.syncKeyHash);
    EXPECT_EQ(decoded.timestamp, req.timestamp);
}

TEST_F(ProtocolExtendedTest, SyncSnapshotRoundTrip) {
    SyncSnapshot snap;
    snap.version = 42;

    SyncSnapshot::Device d1;
    d1.deviceId = "dev-1"; d1.name = "PC"; d1.ip = "192.168.1.1";
    d1.port = 9999; d1.lastSeen = 1000; d1.updatedAt = 2000; d1.isFriend = true;
    snap.devices.append(d1);

    SyncSnapshot::Group g1;
    g1.groupId = "grp-1"; g1.name = "Family";
    g1.memberIds << "u1" << "u2";
    g1.memberNames << "Alice" << "Bob";
    g1.createdAt = 500; g1.updatedAt = 600;
    snap.groups.append(g1);

    SyncSnapshot::Setting s1;
    s1.key = "theme"; s1.value = "dark"; s1.updatedAt = 700;
    snap.settings.append(s1);

    SyncSnapshot::Message m1;
    m1.messageId = "msg-1"; m1.senderId = "u1"; m1.senderName = "Alice";
    m1.senderIp = "192.168.1.10"; m1.content = "Hello!";
    m1.timestamp = 800; m1.isFile = false; m1.isImage = false;
    m1.isRead = true; m1.isGroup = false; m1.isRecalled = false;
    m1.readBy = "u2";
    snap.messages.append(m1);

    QByteArray encoded = ProtocolManager::encodeSyncSnapshot(snap);
    EXPECT_FALSE(encoded.isEmpty());

    SyncSnapshot decoded = ProtocolManager::decodeSyncSnapshot(encoded);
    EXPECT_EQ(decoded.version, 42);
    EXPECT_EQ(decoded.devices.size(), 1);
    EXPECT_EQ(decoded.devices[0].deviceId, "dev-1");
    EXPECT_TRUE(decoded.devices[0].isFriend);
    EXPECT_EQ(decoded.groups.size(), 1);
    EXPECT_EQ(decoded.groups[0].name, "Family");
    EXPECT_EQ(decoded.groups[0].memberIds.size(), 2);
    EXPECT_EQ(decoded.settings.size(), 1);
    EXPECT_EQ(decoded.settings[0].key, "theme");
    EXPECT_EQ(decoded.messages.size(), 1);
    EXPECT_EQ(decoded.messages[0].content, "Hello!");
    EXPECT_TRUE(decoded.messages[0].isRead);
}

TEST_F(ProtocolExtendedTest, SyncSnapshotEmpty) {
    SyncSnapshot snap;
    snap.version = 0;
    QByteArray encoded = ProtocolManager::encodeSyncSnapshot(snap);
    SyncSnapshot decoded = ProtocolManager::decodeSyncSnapshot(encoded);
    EXPECT_EQ(decoded.version, 0);
    EXPECT_TRUE(decoded.devices.isEmpty());
    EXPECT_TRUE(decoded.groups.isEmpty());
    EXPECT_TRUE(decoded.settings.isEmpty());
    EXPECT_TRUE(decoded.messages.isEmpty());
}

TEST_F(ProtocolExtendedTest, SyncAckRoundTrip) {
    SyncAck ack;
    ack.requestId = "req-001";
    ack.success = true;
    ack.appliedCount = 15;
    ack.errorMessage = "";

    QByteArray encoded = ProtocolManager::encodeSyncAck(ack);
    EXPECT_FALSE(encoded.isEmpty());

    SyncAck decoded = ProtocolManager::decodeSyncAck(encoded);
    EXPECT_EQ(decoded.requestId, ack.requestId);
    EXPECT_TRUE(decoded.success);
    EXPECT_EQ(decoded.appliedCount, 15);
    EXPECT_TRUE(decoded.errorMessage.isEmpty());
}

TEST_F(ProtocolExtendedTest, SyncAckFailure) {
    SyncAck ack;
    ack.requestId = "req-002";
    ack.success = false;
    ack.appliedCount = 0;
    ack.errorMessage = "Merge conflict";

    QByteArray encoded = ProtocolManager::encodeSyncAck(ack);
    SyncAck decoded = ProtocolManager::decodeSyncAck(encoded);
    EXPECT_FALSE(decoded.success);
    EXPECT_EQ(decoded.errorMessage, "Merge conflict");
}

TEST_F(ProtocolExtendedTest, FileChecksumRoundTrip) {
    QByteArray checksum = QByteArray::fromHex("deadbeef01234567");
    QString fileId = "file-abc-123";

    QByteArray encoded = ProtocolManager::encodeFileChecksum(checksum, fileId);
    EXPECT_FALSE(encoded.isEmpty());

    QString decodedFileId;
    QByteArray decodedChecksum = ProtocolManager::decodeFileChecksum(encoded, decodedFileId);
    EXPECT_EQ(decodedFileId, fileId);
    EXPECT_EQ(decodedChecksum, checksum);
}

TEST_F(ProtocolExtendedTest, ScreenTileRoundTrip) {
    ScreenTile tile;
    tile.x = 128; tile.y = 64; tile.w = 64; tile.h = 64;
    tile.seq = 100; tile.frameWidth = 1920; tile.frameHeight = 1080;
    tile.isKeyFrame = 1; tile.encoding = 0; tile.format = 0;
    tile.timestamp = 1700000000000ULL;
    tile.hash = QByteArray::fromHex("abcdef");
    tile.data = QByteArray(256, 'T');

    QByteArray encoded = ProtocolManager::encodeScreenTile(tile);
    EXPECT_FALSE(encoded.isEmpty());

    ScreenTile decoded = ProtocolManager::decodeScreenTile(encoded);
    EXPECT_EQ(decoded.x, 128u);
    EXPECT_EQ(decoded.y, 64u);
    EXPECT_EQ(decoded.w, 64u);
    EXPECT_EQ(decoded.h, 64u);
    EXPECT_EQ(decoded.seq, 100u);
    EXPECT_EQ(decoded.frameWidth, 1920u);
    EXPECT_EQ(decoded.frameHeight, 1080u);
    EXPECT_EQ(decoded.isKeyFrame, 1);
    EXPECT_EQ(decoded.encoding, 0);
    EXPECT_EQ(decoded.timestamp, tile.timestamp);
    EXPECT_EQ(decoded.hash, tile.hash);
    EXPECT_EQ(decoded.data.size(), 256);
}

TEST_F(ProtocolExtendedTest, ScreenTileEmptyHash) {
    ScreenTile tile;
    tile.x = 0; tile.y = 0; tile.w = 64; tile.h = 64;
    tile.seq = 1; tile.frameWidth = 800; tile.frameHeight = 600;
    tile.isKeyFrame = 0; tile.encoding = 1; tile.format = 0;
    tile.timestamp = 12345;
    tile.hash = QByteArray();
    tile.data = QByteArray(64, 'x');

    QByteArray encoded = ProtocolManager::encodeScreenTile(tile);
    ScreenTile decoded = ProtocolManager::decodeScreenTile(encoded);
    EXPECT_TRUE(decoded.hash.isEmpty());
    EXPECT_EQ(decoded.encoding, 1);
}

TEST_F(ProtocolExtendedTest, ScreenTileRequestRoundTrip) {
    ScreenTileRequest req;
    req.frameWidth = 1920;
    req.frameHeight = 1080;
    req.tiles.append(QPoint(0, 0));
    req.tiles.append(QPoint(64, 64));
    req.tiles.append(QPoint(1280, 720));

    QByteArray encoded = ProtocolManager::encodeScreenTileRequest(req);
    EXPECT_FALSE(encoded.isEmpty());

    ScreenTileRequest decoded = ProtocolManager::decodeScreenTileRequest(encoded);
    EXPECT_EQ(decoded.frameWidth, 1920u);
    EXPECT_EQ(decoded.frameHeight, 1080u);
    EXPECT_EQ(decoded.tiles.size(), 3);
    EXPECT_EQ(decoded.tiles[0], QPoint(0, 0));
    EXPECT_EQ(decoded.tiles[1], QPoint(64, 64));
    EXPECT_EQ(decoded.tiles[2], QPoint(1280, 720));
}

TEST_F(ProtocolExtendedTest, ScreenTileRequestEmpty) {
    ScreenTileRequest req;
    req.frameWidth = 800;
    req.frameHeight = 600;

    QByteArray encoded = ProtocolManager::encodeScreenTileRequest(req);
    ScreenTileRequest decoded = ProtocolManager::decodeScreenTileRequest(encoded);
    EXPECT_EQ(decoded.frameWidth, 800u);
    EXPECT_TRUE(decoded.tiles.isEmpty());
}

TEST_F(ProtocolExtendedTest, ScreenAckRoundTrip) {
    ScreenAck ack;
    ack.timestamp = 1700000000000ULL;
    ack.roundTripMs = 35;
    ack.tilesReceived = 1000;
    ack.tilesLost = 5;
    ack.bufferLevel = 80;

    QByteArray encoded = ProtocolManager::encodeScreenAck(ack);
    EXPECT_FALSE(encoded.isEmpty());

    ScreenAck decoded = ProtocolManager::decodeScreenAck(encoded);
    EXPECT_EQ(decoded.timestamp, ack.timestamp);
    EXPECT_EQ(decoded.roundTripMs, ack.roundTripMs);
    EXPECT_EQ(decoded.tilesReceived, ack.tilesReceived);
    EXPECT_EQ(decoded.tilesLost, ack.tilesLost);
    EXPECT_EQ(decoded.bufferLevel, ack.bufferLevel);
}

TEST_F(ProtocolExtendedTest, ScreenAckZero) {
    ScreenAck ack;
    ack.timestamp = 0; ack.roundTripMs = 0;
    ack.tilesReceived = 0; ack.tilesLost = 0; ack.bufferLevel = 0;

    QByteArray encoded = ProtocolManager::encodeScreenAck(ack);
    ScreenAck decoded = ProtocolManager::decodeScreenAck(encoded);
    EXPECT_EQ(decoded.timestamp, 0u);
    EXPECT_EQ(decoded.roundTripMs, 0);
}

TEST_F(ProtocolExtendedTest, VoiceMessageRoundTrip) {
    VoiceMessage msg;
    msg.messageId = "vm-001";
    msg.senderId = "user-1";
    msg.senderName = "Alice";
    msg.voiceData = QByteArray(1024, 'V');
    msg.voiceFileName = "voice.ogg";
    msg.duration = 15;
    msg.timestamp = 1700000000000LL;
    msg.isRead = true;

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
    EXPECT_TRUE(decoded.isRead);
}

TEST_F(ProtocolExtendedTest, VideoMessageRoundTrip) {
    VideoMessage msg;
    msg.messageId = "vid-001";
    msg.senderId = "user-2";
    msg.senderName = "Bob";
    msg.videoData = QByteArray(4096, 'W');
    msg.videoFileName = "clip.mp4";
    msg.duration = 30;
    msg.width = 1920.0;
    msg.height = 1080.0;
    msg.timestamp = 1700000000000LL;
    msg.isRead = false;

    QByteArray encoded = ProtocolManager::encodeVideoMessage(msg);
    EXPECT_FALSE(encoded.isEmpty());

    VideoMessage decoded = ProtocolManager::decodeVideoMessage(encoded);
    EXPECT_EQ(decoded.messageId, msg.messageId);
    EXPECT_EQ(decoded.senderId, msg.senderId);
    EXPECT_EQ(decoded.videoData, msg.videoData);
    EXPECT_EQ(decoded.videoFileName, msg.videoFileName);
    EXPECT_EQ(decoded.duration, msg.duration);
    EXPECT_DOUBLE_EQ(decoded.width, msg.width);
    EXPECT_DOUBLE_EQ(decoded.height, msg.height);
    EXPECT_EQ(decoded.timestamp, msg.timestamp);
    EXPECT_FALSE(decoded.isRead);
}

TEST_F(ProtocolExtendedTest, LocationMessageRoundTrip) {
    LocationMessage msg;
    msg.messageId = "loc-001";
    msg.senderId = "user-3";
    msg.senderName = "Charlie";
    msg.latitude = 39.9042;
    msg.longitude = 116.4074;
    msg.locationName = "Beijing";
    msg.timestamp = 1700000000000LL;
    msg.isRead = true;

    QByteArray encoded = ProtocolManager::encodeLocationMessage(msg);
    EXPECT_FALSE(encoded.isEmpty());

    LocationMessage decoded = ProtocolManager::decodeLocationMessage(encoded);
    EXPECT_DOUBLE_EQ(decoded.latitude, msg.latitude);
    EXPECT_DOUBLE_EQ(decoded.longitude, msg.longitude);
    EXPECT_EQ(decoded.locationName, msg.locationName);
}

TEST_F(ProtocolExtendedTest, CardMessageRoundTrip) {
    CardMessage msg;
    msg.messageId = "card-001";
    msg.senderId = "user-4";
    msg.senderName = "Dave";
    msg.vCardData = "BEGIN:VCARD\nFN:Dave\nEND:VCARD";
    msg.timestamp = 1700000000000LL;
    msg.isRead = false;

    QByteArray encoded = ProtocolManager::encodeCardMessage(msg);
    EXPECT_FALSE(encoded.isEmpty());

    CardMessage decoded = ProtocolManager::decodeCardMessage(encoded);
    EXPECT_EQ(decoded.vCardData, msg.vCardData);
    EXPECT_FALSE(decoded.isRead);
}

TEST_F(ProtocolExtendedTest, MergeForwardMessageRoundTrip) {
    MergeForwardMessage msg;
    msg.messageId = "mf-001";
    msg.senderId = "user-5";
    msg.senderName = "Eve";
    msg.timestamp = 1700000000000LL;
    msg.isRead = true;

    ForwardedMessage fwd1;
    fwd1.messageId = "fwd-1"; fwd1.senderId = "u1"; fwd1.senderName = "Alice";
    fwd1.content = "Text message"; fwd1.timestamp = 1000; fwd1.msgType = 0;
    ForwardedMessage fwd2;
    fwd2.messageId = "fwd-2"; fwd2.senderId = "u2"; fwd2.senderName = "Bob";
    fwd2.content = ""; fwd2.timestamp = 2000; fwd2.msgType = 1;

    msg.messages.append(fwd1);
    msg.messages.append(fwd2);

    QByteArray encoded = ProtocolManager::encodeMergeForwardMessage(msg);
    EXPECT_FALSE(encoded.isEmpty());

    MergeForwardMessage decoded = ProtocolManager::decodeMergeForwardMessage(encoded);
    EXPECT_EQ(decoded.messages.size(), 2);
    EXPECT_EQ(decoded.messages[0].content, "Text message");
    EXPECT_EQ(decoded.messages[0].msgType, 0);
    EXPECT_EQ(decoded.messages[1].msgType, 1);
}

TEST_F(ProtocolExtendedTest, MergeForwardEmpty) {
    MergeForwardMessage msg;
    msg.messageId = "mf-empty";
    msg.messages.clear();

    QByteArray encoded = ProtocolManager::encodeMergeForwardMessage(msg);
    MergeForwardMessage decoded = ProtocolManager::decodeMergeForwardMessage(encoded);
    EXPECT_TRUE(decoded.messages.isEmpty());
}

TEST_F(ProtocolExtendedTest, CallInviteRoundTrip) {
    CallInvite invite;
    invite.callId = "call-001";
    invite.callerId = "user-1";
    invite.callerName = "Alice";
    invite.callType = "video";
    invite.sdp = "v=0\r\no=- 0 0 IN IP4 127.0.0.1";
    invite.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeCallInvite(invite);
    EXPECT_FALSE(encoded.isEmpty());

    CallInvite decoded = ProtocolManager::decodeCallInvite(encoded);
    EXPECT_EQ(decoded.callId, invite.callId);
    EXPECT_EQ(decoded.callerId, invite.callerId);
    EXPECT_EQ(decoded.callerName, invite.callerName);
    EXPECT_EQ(decoded.callType, invite.callType);
    EXPECT_EQ(decoded.sdp, invite.sdp);
}

TEST_F(ProtocolExtendedTest, CallAcceptRoundTrip) {
    CallAccept accept;
    accept.callId = "call-002";
    accept.calleeId = "user-2";
    accept.sdp = "v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF";
    accept.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeCallAccept(accept);
    EXPECT_FALSE(encoded.isEmpty());

    CallAccept decoded = ProtocolManager::decodeCallAccept(encoded);
    EXPECT_EQ(decoded.callId, accept.callId);
    EXPECT_EQ(decoded.calleeId, accept.calleeId);
    EXPECT_EQ(decoded.sdp, accept.sdp);
}

TEST_F(ProtocolExtendedTest, CallRejectRoundTrip) {
    CallReject reject;
    reject.callId = "call-003";
    reject.calleeId = "user-3";
    reject.reason = "User busy";
    reject.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeCallReject(reject);
    EXPECT_FALSE(encoded.isEmpty());

    CallReject decoded = ProtocolManager::decodeCallReject(encoded);
    EXPECT_EQ(decoded.callId, reject.callId);
    EXPECT_EQ(decoded.calleeId, reject.calleeId);
    EXPECT_EQ(decoded.reason, reject.reason);
}

TEST_F(ProtocolExtendedTest, CallEndRoundTrip) {
    CallEnd end;
    end.callId = "call-004";
    end.peerId = "user-4";
    end.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeCallEnd(end);
    EXPECT_FALSE(encoded.isEmpty());

    CallEnd decoded = ProtocolManager::decodeCallEnd(encoded);
    EXPECT_EQ(decoded.callId, end.callId);
    EXPECT_EQ(decoded.peerId, end.peerId);
    EXPECT_EQ(decoded.timestamp, end.timestamp);
}

TEST_F(ProtocolExtendedTest, IceCandidateRoundTrip) {
    IceCandidate cand;
    cand.callId = "call-005";
    cand.candidate = "candidate:1 1 UDP 2122252543 192.168.1.1 50000 typ host";
    cand.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeIceCandidate(cand);
    EXPECT_FALSE(encoded.isEmpty());

    IceCandidate decoded = ProtocolManager::decodeIceCandidate(encoded);
    EXPECT_EQ(decoded.callId, cand.callId);
    EXPECT_EQ(decoded.candidate, cand.candidate);
}

TEST_F(ProtocolExtendedTest, VideoCallStartRoundTrip) {
    VideoCallStart start;
    start.callId = "vc-001";
    start.callerId = "user-1";
    start.callerName = "Alice";
    start.width = 1280;
    start.height = 720;
    start.fps = 30;
    start.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeVideoCallStart(start);
    EXPECT_FALSE(encoded.isEmpty());

    VideoCallStart decoded = ProtocolManager::decodeVideoCallStart(encoded);
    EXPECT_EQ(decoded.callId, start.callId);
    EXPECT_EQ(decoded.width, 1280);
    EXPECT_EQ(decoded.height, 720);
    EXPECT_EQ(decoded.fps, 30);
}

TEST_F(ProtocolExtendedTest, VideoCallStopRoundTrip) {
    VideoCallStop stop;
    stop.callId = "vc-002";
    stop.peerId = "user-2";
    stop.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeVideoCallStop(stop);
    EXPECT_FALSE(encoded.isEmpty());

    VideoCallStop decoded = ProtocolManager::decodeVideoCallStop(encoded);
    EXPECT_EQ(decoded.callId, stop.callId);
    EXPECT_EQ(decoded.peerId, stop.peerId);
}

TEST_F(ProtocolExtendedTest, VideoCallFrameRoundTrip) {
    VideoCallFrame frame;
    frame.callId = "vc-003";
    frame.frameData = QByteArray(4096, 'H');
    frame.timestamp = 12345;
    frame.sequenceNumber = 100;
    frame.isKeyFrame = true;
    frame.captureTime = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeVideoCallFrame(frame);
    EXPECT_FALSE(encoded.isEmpty());

    VideoCallFrame decoded = ProtocolManager::decodeVideoCallFrame(encoded);
    EXPECT_EQ(decoded.callId, frame.callId);
    EXPECT_EQ(decoded.frameData, frame.frameData);
    EXPECT_EQ(decoded.timestamp, frame.timestamp);
    EXPECT_EQ(decoded.sequenceNumber, frame.sequenceNumber);
    EXPECT_TRUE(decoded.isKeyFrame);
    EXPECT_EQ(decoded.captureTime, frame.captureTime);
}

TEST_F(ProtocolExtendedTest, ScreenShareStartRoundTrip) {
    ScreenShareStart start;
    start.sessionId = "ss-001";
    start.callerId = "user-1";
    start.callerName = "Alice";
    start.width = 1920;
    start.height = 1080;
    start.fps = 60;
    start.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeScreenShareStart(start);
    EXPECT_FALSE(encoded.isEmpty());

    ScreenShareStart decoded = ProtocolManager::decodeScreenShareStart(encoded);
    EXPECT_EQ(decoded.sessionId, start.sessionId);
    EXPECT_EQ(decoded.width, 1920);
    EXPECT_EQ(decoded.fps, 60);
}

TEST_F(ProtocolExtendedTest, ScreenShareStopRoundTrip) {
    ScreenShareStop stop;
    stop.sessionId = "ss-002";
    stop.peerId = "user-2";
    stop.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeScreenShareStop(stop);
    EXPECT_FALSE(encoded.isEmpty());

    ScreenShareStop decoded = ProtocolManager::decodeScreenShareStop(encoded);
    EXPECT_EQ(decoded.sessionId, stop.sessionId);
    EXPECT_EQ(decoded.peerId, stop.peerId);
}

TEST_F(ProtocolExtendedTest, ScreenShareFrameRoundTrip) {
    ScreenShareFrame frame;
    frame.sessionId = "ss-003";
    frame.frameData = QByteArray(8192, 'S');
    frame.timestamp = 54321;
    frame.sequenceNumber = 200;
    frame.isKeyFrame = false;
    frame.captureTime = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeScreenShareFrame(frame);
    EXPECT_FALSE(encoded.isEmpty());

    ScreenShareFrame decoded = ProtocolManager::decodeScreenShareFrame(encoded);
    EXPECT_EQ(decoded.sessionId, frame.sessionId);
    EXPECT_EQ(decoded.frameData, frame.frameData);
    EXPECT_FALSE(decoded.isKeyFrame);
}

TEST_F(ProtocolExtendedTest, GroupAnnouncementRoundTrip) {
    GroupAnnouncement ann;
    ann.groupId = "grp-001";
    ann.groupName = "Team";
    ann.announcement = "Meeting at 3pm";
    ann.announcerId = "user-1";
    ann.announcerName = "Alice";
    ann.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeGroupAnnouncement(ann);
    EXPECT_FALSE(encoded.isEmpty());

    GroupAnnouncement decoded = ProtocolManager::decodeGroupAnnouncement(encoded);
    EXPECT_EQ(decoded.groupId, ann.groupId);
    EXPECT_EQ(decoded.announcement, ann.announcement);
    EXPECT_EQ(decoded.announcerName, ann.announcerName);
}

TEST_F(ProtocolExtendedTest, GroupMentionRoundTrip) {
    GroupMention mention;
    mention.groupId = "grp-002";
    mention.groupName = "Friends";
    mention.message = "@Alice @Bob check this";
    mention.mentionedMemberIds << "u1" << "u2";
    mention.mentionedMemberNames << "Alice" << "Bob";
    mention.senderId = "u3";
    mention.senderName = "Charlie";
    mention.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeGroupMention(mention);
    EXPECT_FALSE(encoded.isEmpty());

    GroupMention decoded = ProtocolManager::decodeGroupMention(encoded);
    EXPECT_EQ(decoded.mentionedMemberIds.size(), 2);
    EXPECT_EQ(decoded.mentionedMemberIds[0], "u1");
    EXPECT_EQ(decoded.mentionedMemberNames[1], "Bob");
}

TEST_F(ProtocolExtendedTest, GroupVoteRoundTrip) {
    GroupVote vote;
    vote.groupId = "grp-003";
    vote.groupName = "Project";
    vote.voteTitle = "Lunch place?";
    vote.options << "Pizza" << "Sushi" << "Burger";
    vote.durationSeconds = 3600;
    vote.creatorId = "u1";
    vote.creatorName = "Alice";
    vote.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeGroupVote(vote);
    EXPECT_FALSE(encoded.isEmpty());

    GroupVote decoded = ProtocolManager::decodeGroupVote(encoded);
    EXPECT_EQ(decoded.options.size(), 3);
    EXPECT_EQ(decoded.options[0], "Pizza");
    EXPECT_EQ(decoded.options[2], "Burger");
    EXPECT_EQ(decoded.durationSeconds, 3600);
}

TEST_F(ProtocolExtendedTest, GroupFileRoundTrip) {
    GroupFile file;
    file.groupId = "grp-004";
    file.groupName = "Team";
    file.fileId = "f-001";
    file.fileName = "report.pdf";
    file.fileSize = 2048;
    file.md5 = "abc123";
    file.uploaderId = "u1";
    file.uploaderName = "Alice";
    file.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeGroupFile(file);
    EXPECT_FALSE(encoded.isEmpty());

    GroupFile decoded = ProtocolManager::decodeGroupFile(encoded);
    EXPECT_EQ(decoded.fileName, file.fileName);
    EXPECT_EQ(decoded.fileSize, file.fileSize);
    EXPECT_EQ(decoded.md5, file.md5);
}

TEST_F(ProtocolExtendedTest, GroupAlbumRoundTrip) {
    GroupAlbum album;
    album.groupId = "grp-005";
    album.groupName = "Family";
    album.albumId = "alb-001";
    album.albumName = "Vacation";
    album.fileIds << "f1" << "f2" << "f3";
    album.fileNames << "beach.jpg" << "sunset.jpg" << "food.png";
    album.creatorId = "u1";
    album.creatorName = "Alice";
    album.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeGroupAlbum(album);
    EXPECT_FALSE(encoded.isEmpty());

    GroupAlbum decoded = ProtocolManager::decodeGroupAlbum(encoded);
    EXPECT_EQ(decoded.fileIds.size(), 3);
    EXPECT_EQ(decoded.fileNames.size(), 3);
    EXPECT_EQ(decoded.fileNames[0], "beach.jpg");
    EXPECT_EQ(decoded.albumName, "Vacation");
}

TEST_F(ProtocolExtendedTest, GroupTodoRoundTrip) {
    GroupTodo todo;
    todo.groupId = "grp-006";
    todo.groupName = "Work";
    todo.todoId = "todo-001";
    todo.title = "Fix bug #42";
    todo.description = "The login page crashes on mobile";
    todo.status = 1;
    todo.priority = 2;
    todo.assigneeId = "u1";
    todo.assigneeName = "Alice";
    todo.creatorId = "u2";
    todo.creatorName = "Bob";
    todo.dueDate = 1700100000000LL;
    todo.timestamp = 1700000000000LL;

    QByteArray encoded = ProtocolManager::encodeGroupTodo(todo);
    EXPECT_FALSE(encoded.isEmpty());

    GroupTodo decoded = ProtocolManager::decodeGroupTodo(encoded);
    EXPECT_EQ(decoded.title, todo.title);
    EXPECT_EQ(decoded.status, 1);
    EXPECT_EQ(decoded.priority, 2);
    EXPECT_EQ(decoded.assigneeName, "Alice");
    EXPECT_EQ(decoded.dueDate, todo.dueDate);
}

TEST_F(ProtocolExtendedTest, E2EEKeyExchangeRoundTrip) {
    QByteArray publicKey = QByteArray(32, 'P');
    QString sessionId = "sess-001";

    QByteArray encoded = ProtocolManager::encodeE2EEKeyExchange(publicKey, sessionId);
    EXPECT_FALSE(encoded.isEmpty());

    QByteArray decodedKey;
    QString decodedSession;
    ProtocolManager::decodeE2EEKeyExchange(encoded, decodedKey, decodedSession);
    EXPECT_EQ(decodedKey, publicKey);
    EXPECT_EQ(decodedSession, sessionId);
}

TEST_F(ProtocolExtendedTest, E2EEKeyResponseRoundTrip) {
    QByteArray publicKey = QByteArray(32, 'P');
    QByteArray secret = QByteArray(16, 'S');
    QString sessionId = "sess-002";

    QByteArray encoded = ProtocolManager::encodeE2EEKeyResponse(publicKey, secret, sessionId);
    EXPECT_FALSE(encoded.isEmpty());

    QByteArray decodedKey, decodedSecret;
    QString decodedSession;
    ProtocolManager::decodeE2EEKeyResponse(encoded, decodedKey, decodedSecret, decodedSession);
    EXPECT_EQ(decodedKey, publicKey);
    EXPECT_EQ(decodedSecret, secret);
    EXPECT_EQ(decodedSession, sessionId);
}

TEST_F(ProtocolExtendedTest, E2EESessionEstablishedRoundTrip) {
    QString sessionId = "sess-003";
    QByteArray nonce = QByteArray(16, 'N');

    QByteArray encoded = ProtocolManager::encodeE2EESessionEstablished(sessionId, nonce);
    EXPECT_FALSE(encoded.isEmpty());

    QString decodedSession;
    QByteArray decodedNonce;
    ProtocolManager::decodeE2EESessionEstablished(encoded, decodedSession, decodedNonce);
    EXPECT_EQ(decodedSession, sessionId);
    EXPECT_EQ(decodedNonce, nonce);
}

TEST_F(ProtocolExtendedTest, E2EEMessageRoundTrip) {
    QByteArray payload = QByteArray(256, 'E');
    QString sessionId = "sess-004";
    QByteArray nonce = QByteArray(12, 'n');

    QByteArray encoded = ProtocolManager::encodeE2EEMessage(payload, sessionId, nonce);
    EXPECT_FALSE(encoded.isEmpty());

    QByteArray decodedPayload;
    QString decodedSession;
    QByteArray decodedNonce;
    ProtocolManager::decodeE2EEMessage(encoded, decodedPayload, decodedSession, decodedNonce);
    EXPECT_EQ(decodedPayload, payload);
    EXPECT_EQ(decodedSession, sessionId);
    EXPECT_EQ(decodedNonce, nonce);
}

TEST_F(ProtocolExtendedTest, E2EEAckRoundTrip) {
    QString sessionId = "sess-005";
    QByteArray messageId = QByteArray::fromHex("deadbeef");

    QByteArray encoded = ProtocolManager::encodeE2EEAck(sessionId, messageId);
    EXPECT_FALSE(encoded.isEmpty());

    QString decodedSession;
    QByteArray decodedMsgId;
    ProtocolManager::decodeE2EEAck(encoded, decodedSession, decodedMsgId);
    EXPECT_EQ(decodedSession, sessionId);
    EXPECT_EQ(decodedMsgId, messageId);
}

TEST_F(ProtocolExtendedTest, PrivacyScreenRoundTrip) {
    for (bool val : {true, false}) {
        QByteArray encoded = ProtocolManager::encodePrivacyScreen(val);
        EXPECT_FALSE(encoded.isEmpty());
        EXPECT_EQ(ProtocolManager::decodePrivacyScreen(encoded), val);
    }
}

TEST_F(ProtocolExtendedTest, PrivacyScreenEmptyPayload) {
    EXPECT_FALSE(ProtocolManager::decodePrivacyScreen(QByteArray()));
}

TEST_F(ProtocolExtendedTest, ProtocolEncodeDecodeRoundTrip) {
    QByteArray payload = "test payload";
    MessageType type = MessageType::CHAT_MESSAGE;
    QString sessionId = "sess-xyz";

    QByteArray encoded = ProtocolManager::encode(type, payload, sessionId);
    EXPECT_FALSE(encoded.isEmpty());

    MessageType decType;
    QByteArray decPayload;
    QString decSession;
    ASSERT_TRUE(ProtocolManager::decode(encoded, decType, decPayload, decSession));
    EXPECT_EQ(decType, type);
    EXPECT_EQ(decPayload, payload);
    EXPECT_EQ(decSession, sessionId);
}

TEST_F(ProtocolExtendedTest, MonitorListThroughMessage) {
    QList<MonitorInfo> monitors;
    MonitorInfo m;
    m.index = 0; m.name = "Main"; m.width = 1920; m.height = 1080; m.isPrimary = true;
    monitors.append(m);

    QByteArray payload = ProtocolManager::encodeMonitorList(monitors, 0);
    QByteArray message = ProtocolManager::encode(MessageType::MONITOR_LIST, payload, "sess-1");

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    ASSERT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::MONITOR_LIST);

    int idx = -1;
    QList<MonitorInfo> decoded = ProtocolManager::decodeMonitorList(outPayload, idx);
    EXPECT_EQ(decoded.size(), 1);
    EXPECT_EQ(decoded[0].name, "Main");
}

TEST_F(ProtocolExtendedTest, ChatMessageThroughMessage) {
    ChatMessage msg;
    msg.sender = "Alice"; msg.content = "Hello!"; msg.timestamp = 12345;

    QByteArray payload = ProtocolManager::encodeChatMessage(msg);
    QByteArray message = ProtocolManager::encode(MessageType::CHAT_MESSAGE, payload, "sess-2");

    MessageType type;
    QByteArray outPayload;
    QString sessionId;
    ASSERT_TRUE(ProtocolManager::decode(message, type, outPayload, sessionId));
    EXPECT_EQ(type, MessageType::CHAT_MESSAGE);

    ChatMessage decoded = ProtocolManager::decodeChatMessage(outPayload);
    EXPECT_EQ(decoded.sender, "Alice");
    EXPECT_EQ(decoded.content, "Hello!");
}

TEST_F(ProtocolExtendedTest, ProcessListResponseRoundTrip) {
    ProcessListResponse resp;
    resp.success = true;
    resp.errorMessage = "";
    ProcessEntry a;
    a.pid = 1234; a.name = "notepad.exe"; a.memoryBytes = 1024 * 1024 * 12;
    ProcessEntry b;
    b.pid = 5678; b.name = "explorer.exe"; b.memoryBytes = 0;
    resp.entries.append(a);
    resp.entries.append(b);

    QByteArray encoded = ProtocolManager::encodeProcessListResponse(resp);
    EXPECT_FALSE(encoded.isEmpty());

    ProcessListResponse decoded = ProtocolManager::decodeProcessListResponse(encoded);
    EXPECT_TRUE(decoded.success);
    ASSERT_EQ(decoded.entries.size(), 2);
    EXPECT_EQ(decoded.entries[0].pid, 1234LL);
    EXPECT_EQ(decoded.entries[0].name, "notepad.exe");
    EXPECT_EQ(decoded.entries[0].memoryBytes, 1024 * 1024 * 12LL);
    EXPECT_EQ(decoded.entries[1].pid, 5678LL);
    EXPECT_EQ(decoded.entries[1].name, "explorer.exe");
}

TEST_F(ProtocolExtendedTest, ProcessKillStartRoundTrip) {
    ProcessKillRequest killReq;
    killReq.pid = 4321;
    QByteArray kEnc = ProtocolManager::encodeProcessKillRequest(killReq);
    ProcessKillRequest kDec = ProtocolManager::decodeProcessKillRequest(kEnc);
    EXPECT_EQ(kDec.pid, 4321LL);

    ProcessKillResponse killResp;
    killResp.success = true; killResp.pid = 4321; killResp.errorMessage = "";
    ProcessKillResponse kRespDec = ProtocolManager::decodeProcessKillResponse(
        ProtocolManager::encodeProcessKillResponse(killResp));
    EXPECT_TRUE(kRespDec.success);
    EXPECT_EQ(kRespDec.pid, 4321LL);

    ProcessStartRequest startReq;
    startReq.command = "notepad"; startReq.workingDir = "C:/tmp";
    QByteArray sEnc = ProtocolManager::encodeProcessStartRequest(startReq);
    ProcessStartRequest sDec = ProtocolManager::decodeProcessStartRequest(sEnc);
    EXPECT_EQ(sDec.command, "notepad");
    EXPECT_EQ(sDec.workingDir, "C:/tmp");

    ProcessStartResponse startResp;
    startResp.success = true; startResp.pid = 9999; startResp.errorMessage = "";
    ProcessStartResponse sRespDec = ProtocolManager::decodeProcessStartResponse(
        ProtocolManager::encodeProcessStartResponse(startResp));
    EXPECT_TRUE(sRespDec.success);
    EXPECT_EQ(sRespDec.pid, 9999LL);
}

TEST_F(ProtocolExtendedTest, ProcessMessageTypeValues) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::PROCESS_LIST_REQ), 172u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::PROCESS_LIST_RESP), 173u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::PROCESS_KILL_REQ), 174u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::PROCESS_KILL_RESP), 175u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::PROCESS_START_REQ), 176u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::PROCESS_START_RESP), 177u);
}

// ───────────── Real-time Screen Annotation (v1.6.0) ─────────────
TEST_F(ProtocolExtendedTest, AnnotationUpdateRoundTrip) {
    AnnotationUpdate update;
    update.frameWidth = 1920;
    update.frameHeight = 1080;

    AnnotationStroke s1;
    s1.color = Qt::red;
    s1.width = 4;
    s1.points.append(QPoint(10, 20));
    s1.points.append(QPoint(100, 200));
    s1.points.append(QPoint(300, 50));

    AnnotationStroke s2;
    s2.color = QColor(0, 128, 255);
    s2.width = 2;
    s2.points.append(QPoint(5, 5));
    s2.points.append(QPoint(400, 400));

    update.strokes.append(s1);
    update.strokes.append(s2);

    QByteArray encoded = ProtocolManager::encodeAnnotationUpdate(update);
    EXPECT_FALSE(encoded.isEmpty());

    AnnotationUpdate decoded = ProtocolManager::decodeAnnotationUpdate(encoded);
    EXPECT_EQ(decoded.frameWidth, 1920);
    EXPECT_EQ(decoded.frameHeight, 1080);
    ASSERT_EQ(decoded.strokes.size(), 2);

    EXPECT_EQ(decoded.strokes[0].color, QColor(Qt::red));
    EXPECT_EQ(decoded.strokes[0].width, 4);
    ASSERT_EQ(decoded.strokes[0].points.size(), 3);
    EXPECT_EQ(decoded.strokes[0].points[0], QPoint(10, 20));
    EXPECT_EQ(decoded.strokes[0].points[2], QPoint(300, 50));

    EXPECT_EQ(decoded.strokes[1].color, QColor(0, 128, 255));
    EXPECT_EQ(decoded.strokes[1].width, 2);
    ASSERT_EQ(decoded.strokes[1].points.size(), 2);
    EXPECT_EQ(decoded.strokes[1].points[1], QPoint(400, 400));
}

TEST_F(ProtocolExtendedTest, AnnotationEmptyUpdate) {
    AnnotationUpdate update;
    update.frameWidth = 0;
    update.frameHeight = 0;
    QByteArray encoded = ProtocolManager::encodeAnnotationUpdate(update);
    AnnotationUpdate decoded = ProtocolManager::decodeAnnotationUpdate(encoded);
    EXPECT_TRUE(decoded.strokes.isEmpty());
}

TEST_F(ProtocolExtendedTest, AnnotationMessageTypeValues) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::ANNOTATION_UPDATE), 183u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::ANNOTATION_CLEAR), 184u);
}

TEST_F(ProtocolExtendedTest, FileOpMessageTypeValues) {
    EXPECT_EQ(static_cast<uint32_t>(MessageType::FILE_OP_REQ), 162u);
    EXPECT_EQ(static_cast<uint32_t>(MessageType::FILE_OP_RESP), 163u);
}

TEST_F(ProtocolExtendedTest, FileOpRenameRoundTrip) {
    FileOpRequest req;
    req.op = FileOp::Rename;
    req.path = "C:/data/old.txt";
    req.newPath = "C:/data/new.txt";

    QByteArray encoded = ProtocolManager::encodeFileOpRequest(req);
    EXPECT_FALSE(encoded.isEmpty());
    FileOpRequest decoded = ProtocolManager::decodeFileOpRequest(encoded);
    EXPECT_EQ(decoded.op, FileOp::Rename);
    EXPECT_EQ(decoded.path, req.path);
    EXPECT_EQ(decoded.newPath, req.newPath);
}

TEST_F(ProtocolExtendedTest, FileOpDeleteRoundTrip) {
    FileOpRequest req;
    req.op = FileOp::Delete;
    req.path = "C:/data/todelete.bin";

    QByteArray encoded = ProtocolManager::encodeFileOpRequest(req);
    FileOpRequest decoded = ProtocolManager::decodeFileOpRequest(encoded);
    EXPECT_EQ(decoded.op, FileOp::Delete);
    EXPECT_EQ(decoded.path, req.path);
    EXPECT_TRUE(decoded.newPath.isEmpty());
}

TEST_F(ProtocolExtendedTest, FileOpMkdirRoundTrip) {
    FileOpRequest req;
    req.op = FileOp::Mkdir;
    req.path = "C:/data/newfolder";

    QByteArray encoded = ProtocolManager::encodeFileOpRequest(req);
    FileOpRequest decoded = ProtocolManager::decodeFileOpRequest(encoded);
    EXPECT_EQ(decoded.op, FileOp::Mkdir);
    EXPECT_EQ(decoded.path, req.path);
}

TEST_F(ProtocolExtendedTest, FileOpResponseRoundTrip) {
    FileOpResponse resp;
    resp.op = FileOp::Delete;
    resp.path = "C:/data/todelete.bin";
    resp.success = false;
    resp.errorMessage = "权限不足";

    QByteArray encoded = ProtocolManager::encodeFileOpResponse(resp);
    EXPECT_FALSE(encoded.isEmpty());
    FileOpResponse decoded = ProtocolManager::decodeFileOpResponse(encoded);
    EXPECT_EQ(decoded.op, FileOp::Delete);
    EXPECT_EQ(decoded.path, resp.path);
    EXPECT_FALSE(decoded.success);
    EXPECT_EQ(decoded.errorMessage, "权限不足");
}

// ───────────── User Permission Management protocol (v1.8.0) ─────────────

TEST_F(ProtocolExtendedTest, AuthRequestLegacyRoundTrip) {
    // Legacy controllers/Web send the raw password with no version byte.
    AuthRequest req;
    req.legacy = true;
    req.password = "123456789";
    QByteArray encoded = ProtocolManager::encodeAuthRequest(req);
    EXPECT_FALSE(encoded.isEmpty());
    EXPECT_NE(encoded.at(0), static_cast<char>(0x02)); // no magic
    AuthRequest decoded = ProtocolManager::decodeAuthRequest(encoded);
    EXPECT_TRUE(decoded.legacy);
    EXPECT_EQ(decoded.password, "123456789");
    EXPECT_TRUE(decoded.username.isEmpty());
}

TEST_F(ProtocolExtendedTest, AuthRequestV2RoundTrip) {
    AuthRequest req;
    req.legacy = false;
    req.username = "alice";
    req.password = "s3cret";
    QByteArray encoded = ProtocolManager::encodeAuthRequest(req);
    ASSERT_GE(encoded.size(), 4);
    EXPECT_EQ(static_cast<uint8_t>(encoded.at(0)), 0x02);
    AuthRequest decoded = ProtocolManager::decodeAuthRequest(encoded);
    EXPECT_FALSE(decoded.legacy);
    EXPECT_EQ(decoded.username, "alice");
    EXPECT_EQ(decoded.password, "s3cret");
}

TEST_F(ProtocolExtendedTest, AuthResponseV2RoundTrip) {
    AuthResponse resp;
    resp.ok = true;
    resp.sessionKey = QByteArray(32, 'K');
    resp.iv = QByteArray(16, 'V');
    resp.grantedLevel = 2; // Operator
    resp.grantedCaps = 0x1234u;
    QByteArray encoded = ProtocolManager::encodeAuthResponse(resp);
    EXPECT_EQ(encoded.size(), 55); // 2 + 32 + 16 + 1 + 4
    AuthResponse decoded = ProtocolManager::decodeAuthResponse(encoded);
    EXPECT_TRUE(decoded.ok);
    EXPECT_EQ(decoded.sessionKey, resp.sessionKey);
    EXPECT_EQ(decoded.iv, resp.iv);
    EXPECT_EQ(decoded.grantedLevel, 2);
    EXPECT_EQ(decoded.grantedCaps, 0x1234u);
}

TEST_F(ProtocolExtendedTest, AuthResponseFailed) {
    AuthResponse resp;
    resp.ok = false;
    QByteArray encoded = ProtocolManager::encodeAuthResponse(resp);
    EXPECT_EQ(encoded, QByteArray("FAILED"));
    AuthResponse decoded = ProtocolManager::decodeAuthResponse(encoded);
    EXPECT_FALSE(decoded.ok);
}

TEST_F(ProtocolExtendedTest, AuthResponseLegacyClientIgnoresTail) {
    // A new host may still talk to an old controller: the old client only reads
    // the first 50 bytes ("OK"+key+iv) and ignores the trailing level/caps.
    AuthResponse resp;
    resp.ok = true;
    resp.sessionKey = QByteArray(32, 'K');
    resp.iv = QByteArray(16, 'V');
    resp.grantedLevel = 3;
    resp.grantedCaps = 0xFFFFu;
    QByteArray encoded = ProtocolManager::encodeAuthResponse(resp);
    EXPECT_EQ(encoded.size(), 55);
    // Old client parses only the first 50 bytes.
    AuthResponse decoded = ProtocolManager::decodeAuthResponse(encoded.left(50));
    EXPECT_TRUE(decoded.ok);
    EXPECT_EQ(decoded.sessionKey, resp.sessionKey);
    EXPECT_EQ(decoded.iv, resp.iv);
    EXPECT_EQ(decoded.grantedLevel, 0);   // not present in 50-byte form
    EXPECT_EQ(decoded.grantedCaps, 0u);
}

TEST_F(ProtocolExtendedTest, PermissionDeniedRoundTrip) {
    PermissionDenied denied;
    denied.capability = static_cast<uint32_t>(Capability::Terminal);
    denied.reason = "需要 Terminal 权限";
    QByteArray encoded = ProtocolManager::encodePermissionDenied(denied);
    PermissionDenied decoded = ProtocolManager::decodePermissionDenied(encoded);
    EXPECT_EQ(decoded.capability, static_cast<uint32_t>(Capability::Terminal));
    EXPECT_EQ(decoded.reason, "需要 Terminal 权限");
}

TEST_F(ProtocolExtendedTest, UserListResponseRoundTrip) {
    QList<UserRecord> users;
    UserRecord u1; u1.username = "admin"; u1.level = 3; u1.enabled = true; u1.lastLogin = 100;
    UserRecord u2; u2.username = "bob"; u2.level = 1; u2.enabled = false; u2.lastLogin = 0;
    users << u1 << u2;
    QByteArray encoded = ProtocolManager::encodeUserListResponse(users);
    QList<UserRecord> decoded = ProtocolManager::decodeUserListResponse(encoded);
    ASSERT_EQ(decoded.size(), 2);
    EXPECT_EQ(decoded[0].username, "admin");
    EXPECT_EQ(decoded[0].level, 3);
    EXPECT_TRUE(decoded[0].enabled);
    EXPECT_EQ(decoded[0].lastLogin, 100);
    EXPECT_EQ(decoded[1].username, "bob");
    EXPECT_EQ(decoded[1].level, 1);
    EXPECT_FALSE(decoded[1].enabled);
}

TEST_F(ProtocolExtendedTest, DevicePermissionResponseRoundTrip) {
    QList<DevicePermission> devs;
    DevicePermission d1; d1.deviceId = "dev-aaa"; d1.level = 2; d1.capMask = -1; d1.note = "笔记本";
    devs << d1;
    QByteArray encoded = ProtocolManager::encodeDevicePermissionResponse(devs);
    QList<DevicePermission> decoded = ProtocolManager::decodeDevicePermissionResponse(encoded);
    ASSERT_EQ(decoded.size(), 1);
    EXPECT_EQ(decoded[0].deviceId, "dev-aaa");
    EXPECT_EQ(decoded[0].level, 2);
    EXPECT_EQ(decoded[0].capMask, -1);
    EXPECT_EQ(decoded[0].note, "笔记本");
}

TEST_F(ProtocolExtendedTest, PermissionToggleResponseRoundTrip) {
    QByteArray encoded = ProtocolManager::encodePermissionToggleResponse(0xABCDu);
    EXPECT_EQ(ProtocolManager::decodePermissionToggleResponse(encoded), 0xABCDu);
}
