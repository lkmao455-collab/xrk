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

    static QByteArray encodeScreenTile(const ScreenTile& tile);
    static ScreenTile decodeScreenTile(const QByteArray& data);

    static QByteArray encodeScreenTileRequest(const ScreenTileRequest& req);
    static ScreenTileRequest decodeScreenTileRequest(const QByteArray& data);

    static QByteArray encodeScreenAck(const ScreenAck& ack);
    static ScreenAck decodeScreenAck(const QByteArray& data);
    
    static QByteArray encodeFileRequest(const FileRequest& request);
    static FileRequest decodeFileRequest(const QByteArray& data);
    
static QByteArray encodeFileData(const FileData& data);
    static FileData decodeFileData(const QByteArray& data);

    static QByteArray encodeFileChecksum(const QByteArray& checksum, const QString& fileId);
    static QByteArray decodeFileChecksum(const QByteArray& data, QString& fileId);

    static QByteArray encodeVoiceMessage(const VoiceMessage& msg);
    static VoiceMessage decodeVoiceMessage(const QByteArray& data);

    static QByteArray encodeVideoMessage(const VideoMessage& msg);
    static VideoMessage decodeVideoMessage(const QByteArray& data);

    static QByteArray encodeLocationMessage(const LocationMessage& msg);
    static LocationMessage decodeLocationMessage(const QByteArray& data);

    static QByteArray encodeCardMessage(const CardMessage& msg);
    static CardMessage decodeCardMessage(const QByteArray& data);

    static QByteArray encodeMergeForwardMessage(const MergeForwardMessage& msg);
    static MergeForwardMessage decodeMergeForwardMessage(const QByteArray& data);

    static QByteArray encodeCallInvite(const CallInvite& invite);
    static CallInvite decodeCallInvite(const QByteArray& data);

    static QByteArray encodeCallAccept(const CallAccept& accept);
    static CallAccept decodeCallAccept(const QByteArray& data);

    static QByteArray encodeCallReject(const CallReject& reject);
    static CallReject decodeCallReject(const QByteArray& data);

    static QByteArray encodeCallEnd(const CallEnd& end);
    static CallEnd decodeCallEnd(const QByteArray& data);

    static QByteArray encodeIceCandidate(const IceCandidate& candidate);
    static IceCandidate decodeIceCandidate(const QByteArray& data);

    static QByteArray encodeVideoCallStart(const VideoCallStart& start);
    static VideoCallStart decodeVideoCallStart(const QByteArray& data);

    static QByteArray encodeVideoCallStop(const VideoCallStop& stop);
    static VideoCallStop decodeVideoCallStop(const QByteArray& data);

    static QByteArray encodeVideoCallFrame(const VideoCallFrame& frame);
    static VideoCallFrame decodeVideoCallFrame(const QByteArray& data);

    static QByteArray encodeScreenShareStart(const ScreenShareStart& start);
    static ScreenShareStart decodeScreenShareStart(const QByteArray& data);

    static QByteArray encodeScreenShareStop(const ScreenShareStop& stop);
    static ScreenShareStop decodeScreenShareStop(const QByteArray& data);

    static QByteArray encodeScreenShareFrame(const ScreenShareFrame& frame);
    static ScreenShareFrame decodeScreenShareFrame(const QByteArray& data);

    static QByteArray encodeGroupAnnouncement(const GroupAnnouncement& announcement);
    static GroupAnnouncement decodeGroupAnnouncement(const QByteArray& data);

    static QByteArray encodeGroupMention(const GroupMention& mention);
    static GroupMention decodeGroupMention(const QByteArray& data);

    static QByteArray encodeGroupVote(const GroupVote& vote);
    static GroupVote decodeGroupVote(const QByteArray& data);

    static QByteArray encodeGroupFile(const GroupFile& file);
    static GroupFile decodeGroupFile(const QByteArray& data);

    static QByteArray encodeGroupAlbum(const GroupAlbum& album);
    static GroupAlbum decodeGroupAlbum(const QByteArray& data);

    static QByteArray encodeGroupTodo(const GroupTodo& todo);
    static GroupTodo decodeGroupTodo(const QByteArray& data);

    static QByteArray encodeSyncRequest(const SyncRequest& request);
    static SyncRequest decodeSyncRequest(const QByteArray& data);

    static QByteArray encodeSyncSnapshot(const SyncSnapshot& snapshot);
    static SyncSnapshot decodeSyncSnapshot(const QByteArray& data);

    static QByteArray encodeSyncAck(const SyncAck& ack);
    static SyncAck decodeSyncAck(const QByteArray& data);

    static QByteArray encodeClipboardData(const ClipboardData& data);
    static ClipboardData decodeClipboardData(const QByteArray& data);
    
    static QByteArray encodeMonitorInfo(const MonitorInfo& info);
    static MonitorInfo decodeMonitorInfo(const QByteArray& data);
    
    static QByteArray encodeMonitorList(const QList<MonitorInfo>& monitors, int currentMonitorIndex = 0);
    QList<MonitorInfo> static decodeMonitorList(const QByteArray& data, int& currentMonitorIndex);
    
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

    static QByteArray encodeFileOpRequest(const FileOpRequest& req);
    static FileOpRequest decodeFileOpRequest(const QByteArray& data);
    static QByteArray encodeFileOpResponse(const FileOpResponse& resp);
    static FileOpResponse decodeFileOpResponse(const QByteArray& data);

    static QByteArray encodeQualityInfo(const QualityInfo& info);
    static QualityInfo decodeQualityInfo(const QByteArray& data);

    static QByteArray encodeQualityRequest(const QualityRequest& req);
    static QualityRequest decodeQualityRequest(const QByteArray& data);

    static QByteArray encodeSyncPair(const SyncPair& pair);
    static SyncPair decodeSyncPair(const QByteArray& data);
    static QByteArray encodeSyncNotify(const SyncNotify& note);
    static SyncNotify decodeSyncNotify(const QByteArray& data);

    static QByteArray encodeSysInfo(const SysInfo& info);
    static SysInfo decodeSysInfo(const QByteArray& data);

    // Remote Process Manager (v1.5.0)
    static QByteArray encodeProcessListResponse(const ProcessListResponse& resp);
    static ProcessListResponse decodeProcessListResponse(const QByteArray& data);
    static QByteArray encodeProcessKillRequest(const ProcessKillRequest& req);
    static ProcessKillRequest decodeProcessKillRequest(const QByteArray& data);
    static QByteArray encodeProcessKillResponse(const ProcessKillResponse& resp);
    static ProcessKillResponse decodeProcessKillResponse(const QByteArray& data);
    static QByteArray encodeProcessStartRequest(const ProcessStartRequest& req);
    static ProcessStartRequest decodeProcessStartRequest(const QByteArray& data);
    static QByteArray encodeProcessStartResponse(const ProcessStartResponse& resp);
    static ProcessStartResponse decodeProcessStartResponse(const QByteArray& data);

    static QByteArray encodePrivacyScreen(bool enabled);
    static bool decodePrivacyScreen(const QByteArray& data);

    // Real-time Screen Annotation (v1.6.0)
    static QByteArray encodeAnnotationUpdate(const AnnotationUpdate& update);
    static AnnotationUpdate decodeAnnotationUpdate(const QByteArray& data);

    // Consent dialog payload: whether the host user allowed the session, plus
    // the controller's device name (shown on the host's confirmation prompt).
    static QByteArray encodeConsent(bool allowed, const QString& deviceName);
    static void decodeConsent(const QByteArray& data, bool& allowed, QString& deviceName);

    // ── User Permission Management (v1.8.0) ──
    static QByteArray encodeAuthRequest(const AuthRequest& req);
    static AuthRequest decodeAuthRequest(const QByteArray& data);
    static QByteArray encodeAuthResponse(const AuthResponse& resp);
    static AuthResponse decodeAuthResponse(const QByteArray& data);
    static QByteArray encodePermissionDenied(const PermissionDenied& denied);
    static PermissionDenied decodePermissionDenied(const QByteArray& data);
    static QByteArray encodeUserListResponse(const QList<UserRecord>& users);
    static QList<UserRecord> decodeUserListResponse(const QByteArray& data);
    static QByteArray encodeDevicePermissionResponse(const QList<DevicePermission>& devices);
    static QList<DevicePermission> decodeDevicePermissionResponse(const QByteArray& data);
    static QByteArray encodePermissionToggleResponse(uint32_t toggles);
    static uint32_t decodePermissionToggleResponse(const QByteArray& data);
    // PERMISSION_TOGGLE_REQ (212): [u32 capability][u8 enabled]
    static QByteArray encodePermissionToggleRequest(uint32_t capability, bool enabled);
    static bool decodePermissionToggleRequest(const QByteArray& data,
                                              uint32_t& capability, bool& enabled);
    // USER_ADD (216) / USER_UPDATE (218)
    static QByteArray encodeUserMutation(const UserMutation& mutation);
    static UserMutation decodeUserMutation(const QByteArray& data);

    // E2EE (End-to-End Encryption) encode/decode functions
    static QByteArray encodeE2EEKeyExchange(const QByteArray& publicKey, const QString& sessionId);
    static QByteArray decodeE2EEKeyExchange(const QByteArray& data, QByteArray& publicKey, QString& sessionId);
    
    static QByteArray encodeE2EEKeyResponse(const QByteArray& publicKey, const QByteArray& encryptedSharedSecret, const QString& sessionId);
    static QByteArray decodeE2EEKeyResponse(const QByteArray& data, QByteArray& publicKey, QByteArray& encryptedSharedSecret, QString& sessionId);
    
    static QByteArray encodeE2EESessionEstablished(const QString& sessionId, const QByteArray& encryptedNonce);
    static QByteArray decodeE2EESessionEstablished(const QByteArray& data, QString& sessionId, QByteArray& encryptedNonce);
    
    static QByteArray encodeE2EEMessage(const QByteArray& encryptedPayload, const QString& sessionId, const QByteArray& nonce);
    static QByteArray decodeE2EEMessage(const QByteArray& data, QByteArray& encryptedPayload, QString& sessionId, QByteArray& nonce);
    
    static QByteArray encodeE2EEAck(const QString& sessionId, const QByteArray& messageId);
    static QByteArray decodeE2EEAck(const QByteArray& data, QString& sessionId, QByteArray& messageId);

    static bool validate(const QByteArray& data);
    static uint32_t calculateChecksum(const QByteArray& data);
};

} // namespace xrk
