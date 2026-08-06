#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QMutex>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QList>
#include <QString>
#include <QMap>
#include <memory>

namespace xrk {

struct IPMsgDevice;
struct IPMsgGroup;
struct IPMsgMessage;
struct IPMsgTransferState;
class IPMsgManager;

class DatabaseManager : public QObject {
    Q_OBJECT
public:
    static DatabaseManager& instance() {
        static DatabaseManager* inst = nullptr;
        if (!inst) {
            inst = new DatabaseManager();
        }
        return *inst;
    }

    bool initialize(const QString& dbPath = "");
    void shutdown();

    // Messages
    bool saveMessage(const IPMsgMessage& msg, const QString& targetId, bool isGroup = false);
    QList<IPMsgMessage> loadMessages(const QString& targetId, int limit = 50, qint64 beforeTimestamp = 0, bool isGroup = false);
    bool deleteMessage(const QString& recallId);
    bool markMessageRead(const QString& messageId, const QString& readerName);
    int getUnreadCount(const QString& targetId, bool isGroup = false);

    // Contacts/Devices
    bool saveDevice(const IPMsgDevice& device);
    bool updateDeviceLastSeen(const QString& deviceId, qint64 timestamp);
    QList<IPMsgDevice> loadAllDevices();
    bool removeDevice(const QString& deviceId);

    // Friends
    bool addFriend(const QString& deviceId);
    bool removeFriend(const QString& deviceId);
    QList<QString> loadFriends();
    bool isFriend(const QString& deviceId) const;

    // Groups
    bool saveGroup(const IPMsgGroup& group);
    bool updateGroup(const IPMsgGroup& group);
    bool deleteGroup(const QString& groupId);
    QList<IPMsgGroup> loadAllGroups();
    IPMsgGroup loadGroup(const QString& groupId);

    // Settings
    bool setSetting(const QString& key, const QVariant& value);
    QVariant getSetting(const QString& key, const QVariant& defaultValue = QVariant()) const;

    // Recent chats
    struct RecentChat {
        QString targetId;
        QString targetName;
        QString lastMessage;
        qint64 timestamp;
        int unreadCount;
        bool isGroup;
    };
    QList<RecentChat> loadRecentChats(int limit = 20);
    bool updateRecentChat(const QString& targetId, const QString& targetName, const QString& lastMessage, qint64 timestamp, bool isGroup);

    // File transfers
    struct TransferRecord {
        QString fileId;
        QString fileName;
        qint64 fileSize;
        QString md5;
        QString localPath;
        QString remotePath;
        qint64 transferredSize;
        int status;
        qint64 startTime;
        qint64 endTime;
        bool isSender;
        QString targetId;
        bool isGroup;
        QByteArray checksum;      // SHA-256 checksum for integrity verification
        qint64 resumeOffset;      // Offset to resume from for interrupted transfers
        QString sessionId;        // Session ID for resumable transfer tracking
    };
    bool saveTransferRecord(const TransferRecord& record);
    bool updateTransferProgress(const QString& fileId, qint64 transferred, int status);
    bool updateTransferProgress(const QString& fileId, qint64 transferred, int status, qint64 resumeOffset);
    QList<TransferRecord> loadTransferRecords(int limit = 50);
    QList<TransferRecord> loadPendingTransfers(const QString& targetId, bool isGroup = false);

    // Offline messages
    struct OfflineMessage {
        QString id;
        QString targetDeviceId;
        QByteArray payload;
        int retryCount;
        qint64 createdAt;
        qint64 expiresAt;
    };
    bool saveOfflineMessage(const OfflineMessage& msg);
    QList<OfflineMessage> loadPendingOfflineMessages(const QString& deviceId, int limit = 100);
    bool markOfflineMessageDelivered(const QString& id);
    bool incrementOfflineRetry(const QString& id);
    void cleanupExpiredOfflineMessages();

    // E2EE sessions
    struct E2EESessionRow {
        QString deviceId;
        QByteArray publicKey;
        QByteArray sharedSecret;
        QString sessionId;
        qint64 establishedAt;
        qint64 lastActivity;
        QString encryptionAlgorithm;
        QByteArray nonce;
        bool isActive;
    };
    bool saveE2EESession(const E2EESessionRow& session);
    E2EESessionRow loadE2EESession(const QString& deviceId);
    QList<E2EESessionRow> loadAllE2EESessions();
    bool updateE2EESession(const E2EESessionRow& session);
    bool removeE2EESession(const QString& deviceId);
    bool deactivateE2EESession(const QString& deviceId);

    // Voice messages
    struct VoiceMessageRow {
        QString messageId;
        QString senderId;
        QString senderName;
        QByteArray voiceData;
        QString voiceFileName;
        int duration = 0;
        qint64 timestamp = 0;
        bool isRead = false;
        QString targetId;
        bool isGroup = false;
    };
    bool saveVoiceMessage(const VoiceMessageRow& msg);
    QList<VoiceMessageRow> loadVoiceMessages(const QString& targetId, int limit = 50, bool isGroup = false);
    bool markVoiceMessageRead(const QString& messageId);

    // Video messages
    struct VideoMessageRow {
        QString messageId;
        QString senderId;
        QString senderName;
        QByteArray videoData;
        QString videoFileName;
        int duration = 0;
        double width = 0;
        double height = 0;
        qint64 timestamp = 0;
        bool isRead = false;
        QString targetId;
        bool isGroup = false;
    };
    bool saveVideoMessage(const VideoMessageRow& msg);
    QList<VideoMessageRow> loadVideoMessages(const QString& targetId, int limit = 50, bool isGroup = false);
    bool markVideoMessageRead(const QString& messageId);

    // Location messages
    struct LocationMessageRow {
        QString messageId;
        QString senderId;
        QString senderName;
        double latitude = 0;
        double longitude = 0;
        QString locationName;
        qint64 timestamp = 0;
        bool isRead = false;
        QString targetId;
        bool isGroup = false;
    };
    bool saveLocationMessage(const LocationMessageRow& msg);
    QList<LocationMessageRow> loadLocationMessages(const QString& targetId, int limit = 50, bool isGroup = false);
    bool markLocationMessageRead(const QString& messageId);

    // Card messages
    struct CardMessageRow {
        QString messageId;
        QString senderId;
        QString senderName;
        QString vCardData;
        qint64 timestamp = 0;
        bool isRead = false;
        QString targetId;
        bool isGroup = false;
    };
    bool saveCardMessage(const CardMessageRow& msg);
    QList<CardMessageRow> loadCardMessages(const QString& targetId, int limit = 50, bool isGroup = false);
    bool markCardMessageRead(const QString& messageId);

    // Merge Forward messages
    struct MergeForwardMessageRow {
        QString messageId;
        QString senderId;
        QString senderName;
        QByteArray mergedData; // Serialized list of forwarded messages
        qint64 timestamp = 0;
        bool isRead = false;
        QString targetId;
        bool isGroup = false;
    };
    bool saveMergeForwardMessage(const MergeForwardMessageRow& msg);
    QList<MergeForwardMessageRow> loadMergeForwardMessages(const QString& targetId, int limit = 50, bool isGroup = false);
    bool markMergeForwardMessageRead(const QString& messageId);

    // Group Announcements
    struct GroupAnnouncementRow {
        QString id;
        QString groupId;
        QString groupName;
        QString announcement;
        QString announcerId;
        QString announcerName;
        qint64 timestamp = 0;
    };
    bool saveGroupAnnouncement(const GroupAnnouncementRow& announcement);
    QList<GroupAnnouncementRow> loadGroupAnnouncements(const QString& groupId, int limit = 50);

    // Group Mentions
    struct GroupMentionRow {
        QString id;
        QString groupId;
        QString groupName;
        QString message;
        QStringList mentionedMemberIds;
        QStringList mentionedMemberNames;
        QString senderId;
        QString senderName;
        qint64 timestamp = 0;
    };
    bool saveGroupMention(const GroupMentionRow& mention);
    QList<GroupMentionRow> loadGroupMentions(const QString& groupId, int limit = 50);

    // Group Votes
    struct GroupVoteRow {
        QString id;
        QString groupId;
        QString groupName;
        QString voteTitle;
        QStringList options;
        int durationSeconds = 0;
        QString creatorId;
        QString creatorName;
        qint64 timestamp = 0;
    };
    bool saveGroupVote(const GroupVoteRow& vote);
    QList<GroupVoteRow> loadGroupVotes(const QString& groupId, int limit = 50);

    // Group Files
    struct GroupFileRow {
        QString id;
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
    bool saveGroupFile(const GroupFileRow& file);
    QList<GroupFileRow> loadGroupFiles(const QString& groupId, int limit = 50);

    // Group Albums
    struct GroupAlbumRow {
        QString id;
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
    bool saveGroupAlbum(const GroupAlbumRow& album);
    QList<GroupAlbumRow> loadGroupAlbums(const QString& groupId, int limit = 50);

    // Group Todos
    struct GroupTodoRow {
        QString id;
        QString groupId;
        QString groupName;
        QString todoId;
        QString title;
        QString description;
        int status = 0;
        int priority = 0;
        QString assigneeId;
        QString assigneeName;
        QString creatorId;
        QString creatorName;
        qint64 dueDate = 0;
        qint64 timestamp = 0;
    };
    bool saveGroupTodo(const GroupTodoRow& todo);
    QList<GroupTodoRow> loadGroupTodos(const QString& groupId, int limit = 50);
    bool updateGroupTodoStatus(const QString& todoId, int status);

    // Multi-device sync (Phase E1) - LWW merge support
    struct SyncDeviceRow {
        QString deviceId;
        QString name;
        QString ip;
        quint16 port = 2425;
        qint64 lastSeen = 0;
        qint64 updatedAt = 0;
        bool isFriend = false;
    };
    struct SyncGroupRow {
        QString groupId;
        QString name;
        QList<QString> memberIds;
        QList<QString> memberNames;
        qint64 createdAt = 0;
        qint64 updatedAt = 0;
    };
    struct SyncSettingRow {
        QString key;
        QString value;
        qint64 updatedAt = 0;
    };
    struct SyncMessageRow {
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

    // Sync tables (Phase E1)
    struct SyncRequestRow {
        QString requestId;
        QString accountHash;
        QString syncKeyHash;
        qint64 timestamp = 0;
    };
    struct SyncSnapshotRow {
        qint64 version = 0;
        QList<SyncDeviceRow> devices;
        QList<SyncGroupRow> groups;
        QList<SyncSettingRow> settings;
        QList<SyncMessageRow> messages;
    };
    struct SyncAckRow {
        QString requestId;
        bool success = false;
        int appliedCount = 0;
        QString errorMessage;
    };

    // Sync CRUD
    bool saveSyncRequest(const SyncRequestRow& request);
    SyncRequestRow loadSyncRequest(const QString& requestId);
    bool saveSyncSnapshot(const SyncSnapshotRow& snapshot);
    SyncSnapshotRow loadSyncSnapshot(qint64 minVersion = 0);
    bool saveSyncAck(const SyncAckRow& ack);
    SyncAckRow loadSyncAck(const QString& requestId);

    struct SyncSnapshot {
        qint64 version = 0;
        QList<SyncDeviceRow> devices;
        QList<SyncGroupRow> groups;
        QList<SyncSettingRow> settings;
        QList<SyncMessageRow> messages;
    };

    SyncSnapshot buildSyncSnapshot(int messageLimit = 1000);
    // Last-write-wins merge by updated_at; returns number of rows applied.
    int applySyncSnapshot(const SyncSnapshot& snap);

signals:
    void databaseError(const QString& error);

public:
    // Access to the underlying QSqlDatabase for direct queries (e.g., cleanup)
    QSqlDatabase database() const { return m_db; }

private:
    explicit DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager();

    bool createTables();
    void runMigrations();

    QSqlDatabase m_db;
    // Recursive so internal helper calls (e.g. saveMessage -> updateRecentChat)
    // can re-enter without self-deadlocking on the same thread.
    QRecursiveMutex m_mutex;
    QString m_dbPath;
    bool m_initialized = false;

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;
};

} // namespace xrk