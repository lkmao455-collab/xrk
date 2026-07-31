#pragma once

#include <QObject>
#include <QByteArray>
#include "types.h"

namespace xrk {

class ProtocolManager {
public:
    static QByteArray encode(MessageType type, const QByteArray& payload, const QString& sessionId = QString());
    static bool decode(const QByteArray& data, MessageType& type, QByteArray& payload, QString& sessionId);
    
    static QByteArray encodeDeviceInfo(const DeviceInfo& info);
    static DeviceInfo decodeDeviceInfo(const QByteArray& data);
    
    static QByteArray encodeMouseEvent(const MouseEvent& event);
    static MouseEvent decodeMouseEvent(const QByteArray& data);
    
    static QByteArray encodeKeyEvent(const KeyEvent& event);
    static KeyEvent decodeKeyEvent(const QByteArray& data);
    
    static QByteArray encodeScreenFrame(const ScreenFrame& frame);
    static ScreenFrame decodeScreenFrame(const QByteArray& data);
    
    static QByteArray encodeFileRequest(const FileRequest& request);
    static FileRequest decodeFileRequest(const QByteArray& data);
    
    static QByteArray encodeFileData(const FileData& data);
    static FileData decodeFileData(const QByteArray& data);
    
    static QByteArray encodeClipboardData(const ClipboardData& data);
    static ClipboardData decodeClipboardData(const QByteArray& data);
    
    static QByteArray encodeMonitorInfo(const MonitorInfo& info);
    static MonitorInfo decodeMonitorInfo(const QByteArray& data);
    
    static QByteArray encodeMonitorList(const QList<MonitorInfo>& monitors);
    QList<MonitorInfo> static decodeMonitorList(const QByteArray& data);
    
    static QByteArray encodeChatMessage(const ChatMessage& msg);
    static ChatMessage decodeChatMessage(const QByteArray& data);
    
    static QByteArray encodeEncryptionKeyExchange(const EncryptionKeyExchange& exchange);
    static EncryptionKeyExchange decodeEncryptionKeyExchange(const QByteArray& data);
    
    static QByteArray encodeTerminalData(const TerminalData& data);
    static TerminalData decodeTerminalData(const QByteArray& data);
    
    static QByteArray encodeRecordControl(const RecordControl& control);
    static RecordControl decodeRecordControl(const QByteArray& data);
    
    static QByteArray encodeFileBrowserRequest(const FileBrowserRequest& req);
    static FileBrowserRequest decodeFileBrowserRequest(const QByteArray& data);
    static QByteArray encodeFileBrowserEntry(const FileBrowserEntry& entry);
    static FileBrowserEntry decodeFileBrowserEntry(QDataStream& stream, const QByteArray& data);
    static QByteArray encodeFileBrowserResponse(const FileBrowserResponse& resp);
    static FileBrowserResponse decodeFileBrowserResponse(const QByteArray& data);
    
    static QByteArray encodeQualityInfo(const QualityInfo& info);
    static QualityInfo decodeQualityInfo(const QByteArray& data);

    static QByteArray encodeSysInfo(const SysInfo& info);
    static SysInfo decodeSysInfo(const QByteArray& data);

    static QByteArray encodePrivacyScreen(bool enabled);
    static bool decodePrivacyScreen(const QByteArray& data);

    // Consent dialog payload: whether the host user allowed the session, plus
    // the controller's device name (shown on the host's confirmation prompt).
    static QByteArray encodeConsent(bool allowed, const QString& deviceName);
    static void decodeConsent(const QByteArray& data, bool& allowed, QString& deviceName);
    
    static bool validate(const QByteArray& data);
    static uint32_t calculateChecksum(const QByteArray& data);
};

} // namespace xrk
