#pragma once

#include <QString>
#include <QImage>
#include <QDateTime>
#include <cstdint>

namespace xrk {

constexpr uint32_t MAGIC = 0x58524B00;
constexpr uint32_t PROTOCOL_VERSION = 1;
constexpr uint16_t DEFAULT_PORT = 9999;
constexpr uint16_t UDP_BROADCAST_PORT = 9998;
constexpr uint16_t P2P_PORT = 9997; // dedicated port for P2P TCP hole-punching (simultaneous open)

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
    MOUSE_EVENT = 30,
    KEY_EVENT = 31,
    FILE_REQ = 40,
    FILE_DATA = 41,
    FILE_ACK = 42,
    CLIPBOARD_DATA = 50,
    CLIPBOARD_ACK = 51,
    MONITOR_LIST = 60,
    MONITOR_SWITCH = 61,
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
    
    POWER_COMMAND = 150,
    FILE_BROWSER_REQ = 160,
    FILE_BROWSER_RESP = 161,
    SYSINFO_REQ = 170,
    SYSINFO_RESP = 171,
    PRIVACY_SCREEN = 180,
    SET_QUALITY = 181,
    CONSENT_REQUEST = 190,
    CONSENT_RESPONSE = 191
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
};

struct ScreenFrame {
    QByteArray data;
    uint32_t width = 0;
    uint32_t height = 0;
    FrameFormat format = FrameFormat::JPEG;
    uint64_t timestamp = 0;
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

} // namespace xrk
