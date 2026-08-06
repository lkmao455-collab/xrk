#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QMap>
#include <QSet>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDateTime>

#include "database_manager.h"
#include "core/types.h"

namespace xrk {

class IpmsgCryptoTest; // test fixture friend (GTest)

struct IPMsgDevice {
    QString id;
    QString name;
    QString ip;
    quint16 port;
    qint64 lastSeen;
};

struct IPMsgGroup {
    QString id;
    QString name;
    QList<QString> memberIds;
    QList<QString> memberNames;
    qint64 createdAt;
};

struct IPMsgFileItem {
    QString name;
    QString absolutePath;
    QString relativePath;
    qint64 size;
    bool isDirectory;
    QString md5;
    QList<IPMsgFileItem> children;
    QList<QByteArray> chunkMd5s; // Per-chunk MD5 for verification
    qint64 chunkSize = 1024 * 1024; // 1MB default chunk size
};

struct IPMsgMessage {
    QString senderId;
    QString senderName;
    QString senderIp;
    QString content;
    qint64 timestamp;
    bool isFile;
    QString filePath;
    qint64 fileSize;
    bool isDirectory;
    bool isImage;
    QByteArray imageData;
    QString imageFileName;
    QString replyTo;
    QString replyContent;
    QString recallId;
    bool isRecalled;
    bool isVoiceMessage;
    QByteArray voiceData;
    QString voiceFileName;
    int voiceDuration; // in seconds
    bool isVideoMessage;
    QByteArray videoData;
    QString videoFileName;
    int videoDuration;
    double videoWidth;
    double videoHeight;
    QString locationLatitude;
    QString locationLongitude;
    QString locationName;
    QString vCardData;
};

struct IPMsgTransferState {
    QString fileId;
    QString filePath;
    QString savePath;
    qint64 totalSize;
    qint64 transferredSize;
    QString md5;
    bool isDirectory;
    bool isSender;
    qint64 lastOffset;
};

class IPMsgManager : public QObject {
    Q_OBJECT
public:
    explicit IPMsgManager(QObject* parent = nullptr);
    ~IPMsgManager();

    friend class IpmsgCryptoTest;

    bool start(quint16 port = 2425);
    void stop();
    bool isRunning() const;

    void broadcastPresence();
    void sendMessage(const QString& targetIp, const QString& message);
    void sendReply(const QString& targetIp, const QString& message, const QString& replyTo, const QString& replyContent);
    void sendImage(const QString& targetIp, const QByteArray& imageData, const QString& fileName);
    void recallMessage(const QString& targetIp, const QString& recallId, const QString& senderName);
    void sendGroupMessage(const QString& groupId, const QString& message);
    QString createGroup(const QString& name, const QList<QString>& memberIds);
    QList<IPMsgGroup> getGroups() const;
    void sendReadReceipt(const QString& targetIp, const QString& messageId);
    void sendTypingIndicator(const QString& targetIp);
    void sendFriendRequest(const QString& targetIp, const QString& message);
    void acceptFriendRequest(const QString& targetIp);
    void rejectFriendRequest(const QString& targetIp);
    bool isFriend(const QString& deviceId) const;
    QList<QString> getFriends() const;
    void updateGroupName(const QString& groupId, const QString& newName);
    void inviteToGroup(const QString& groupId, const QString& memberId);
    void removeFromGroup(const QString& groupId, const QString& memberId);
    void dissolveGroup(const QString& groupId);
    void sendFile(const QString& targetIp, const QString& filePath);
    void sendFolder(const QString& targetIp, const QString& folderPath);
    void acceptFile(const QString& fileId, const QString& savePath);
    void rejectFile(const QString& fileId);
    void resumeFile(const QString& fileId);
    bool verifyFileIntegrity(const QString& filePath, const QString& expectedMd5);

    // Offline messaging
    bool sendOfflineMessage(const QString& targetDeviceId, const QByteArray& payload);
    QList<DatabaseManager::OfflineMessage> fetchOfflineMessages();
    void markOfflineMessageDelivered(const QString& msgId);

    void cleanupExpiredOfflineMessages();

    // Voice/Video messages
    void sendVoiceMessage(const QString& targetIp, const QByteArray& voiceData, int duration);
    void sendVideoMessage(const QString& targetIp, const QByteArray& videoData, int duration, double width, double height);
    void sendLocationMessage(const QString& targetIp, const QString& lat, const QString& lon, const QString& name);
    void sendCardMessage(const QString& targetIp, const QString& vCardData);

    // Voice messages (protocol-based, Phase B1)
    void sendVoiceMessageProtocol(const QString& targetId, const QByteArray& voiceData, int duration);
    void sendVideoMessageProtocol(const QString& targetId, const QByteArray& videoData, int duration, double width, double height);
    void sendLocationMessageProtocol(const QString& targetId, double latitude, double longitude, const QString& name);
    void sendCardMessageProtocol(const QString& targetId, const QString& vCardData);
    void sendMergeForwardMessageProtocol(const QString& targetId, const QList<ForwardedMessage>& messages);
    void handleVoiceMessage(const IPMsgMessage& message);
    void handleVideoMessage(const IPMsgMessage& message);
    void handleLocationMessage(const IPMsgMessage& message);
    void handleCardMessage(const IPMsgMessage& message);
    void handleMergeForwardMessage(const IPMsgMessage& message);

    // End-to-end encryption
    void initiateKeyExchange(const QString& targetIp);
    QByteArray encryptMessage(const QByteArray& plaintext, const QString& recipientId);
    QByteArray decryptMessage(const QByteArray& ciphertext, const QString& senderId);
    bool hasEstablishedSession(const QString& deviceId) const;

    // VoIP signaling
    void initiateCall(const QString& targetIp, const QString& callType = "voice");
    void acceptCall(const QString& calleeIp);
    void rejectCall(const QString& calleeIp);
    void endCall(const QString& calleeIp);
    void sendIceCandidate(const QString& targetIp, const QByteArray& sdp);

    // Group chat advanced
    void sendGroupAnnouncement(const QString& groupId, const QString& announcement);
    void sendGroupMention(const QString& groupId, const QStringList& mentionedMembers, const QString& message);
    void sendGroupVote(const QString& groupId, const QString& voteTitle, const QStringList& options, int durationSeconds);
    void sendGroupVoteResponse(const QString& groupId, const QString& voteTitle, int selectedOption, const QString& creatorId);

    // Multi-device sync (Phase E1)
    bool setAccount(const QString& accountId, const QString& syncKey);
    bool hasAccountConfigured() const;
    QString accountId() const;
    QString accountHash() const;
    void syncWith(const QString& targetIp);
    QList<QString> sameAccountDevices() const;

    QList<IPMsgDevice> getOnlineDevices() const;
    IPMsgDevice deviceInfo(const QString& deviceId) const;
    QList<IPMsgMessage> getMessages() const;
    QList<IPMsgMessage> getMessagesWith(const QString& deviceId) const;

    void setUserName(const QString& name);
    QString userName() const;
    QString userId() const;

    static QString calculateFileMd5(const QString& filePath);
    static QString calculateFileMd5(QIODevice* device);

signals:
    void deviceFound(const IPMsgDevice& device);
    void deviceLeft(const QString& deviceId);
    void messageReceived(const IPMsgMessage& message);
    void messageRecalled(const QString& recallId, const QString& senderName);
    void messageRead(const QString& messageId, const QString& readerName);
    void typingIndicatorReceived(const QString& senderName);
    void friendRequestReceived(const QString& senderId, const QString& senderName, const QString& message);
    void friendRequestAccepted(const QString& senderId, const QString& senderName);
    void friendRequestRejected(const QString& senderId, const QString& senderName);
    void fileReceiveRequest(const QString& fileId, const QString& senderName, 
                           const QString& fileName, qint64 fileSize, bool isDirectory,
                           const QString& md5);
    void fileProgress(const QString& fileId, qint64 bytesTransferred, qint64 totalBytes);
    void fileCompleted(const QString& fileId, const QString& filePath, bool integrityOk);
    void fileError(const QString& fileId, const QString& error);
    void fileResuming(const QString& fileId, qint64 resumeOffset);
    void error(const QString& message);
    void offlineMessagesAvailable(const QList<DatabaseManager::OfflineMessage>& messages);
    void keyExchangeNeeded(const QString& deviceId);
    void encryptionReady(const QString& deviceId);
    void voiceMessageReceived(const IPMsgMessage& message);
    void videoMessageReceived(const IPMsgMessage& message);
    void locationMessageReceived(const IPMsgMessage& message);
    void cardMessageReceived(const IPMsgMessage& message);
    void mergeForwardMessageReceived(const IPMsgMessage& message);
    void incomingCall(const QString& callerId, const QString& callerName);
    void callAccepted(const QString& calleeId);
    void callRejected(const QString& calleeId);
    void callEnded(const QString& peerId);
    void iceCandidateReceived(const QString& peerId, const QByteArray& sdp);
    void groupAnnouncementReceived(const QString& groupId, const QString& announcement, const QString& announcer);
    void groupMentionReceived(const QString& groupId, const QStringList& mentionedMembers, const QString& message, const QString& senderName);
    void groupVoteReceived(const QString& groupId, const QString& voteTitle, const QStringList& options, int durationSeconds, const QString& creator);
    void groupVoteResponseReceived(const QString& groupId, const QString& voteTitle, int selectedOption, const QString& voterId, const QString& voterName);

    // Multi-device sync
    void syncCompleted(const QString& deviceId, int appliedCount);
    void syncFailed(const QString& deviceId, const QString& reason);
    void sameAccountDeviceFound(const QString& deviceId, const QString& deviceName);
    void dataSynced();

private slots:
    void onUdpBroadcastReceived();
    void onNewTcpConnection();
    void onTcpDataReceived();
    void onTcpDisconnected();
    void onSendProgress(qint64 bytesWritten);
    void retryOfflineMessages(const QString& deviceId);

private:
    void processUdpMessage(const QByteArray& data, const QHostAddress& sender);
    void processTcpCommand(const QByteArray& data, QTcpSocket* socket);
    void sendUdpBroadcast(const QByteArray& data);
    void sendFileData(const QString& targetIp, const QList<IPMsgFileItem>& items);
    void receiveFileData(QTcpSocket* socket, const QJsonObject& header);
    IPMsgFileItem buildFileTree(const QString& path, const QString& basePath);
    qint64 calculateFolderSize(const QString& folderPath);
    void saveReceivedFile(QTcpSocket* socket, const QString& savePath, qint64 fileSize);
    QString generateFileId();
    QString getLocalIp();
    QList<QByteArray> calculateChunkMd5s(const QString& filePath, qint64 chunkSize = 1024 * 1024);
    void loadTransferStates();
    void saveTransferState(const IPMsgTransferState& state);
    void removeTransferState(const QString& fileId);
    IPMsgTransferState getTransferState(const QString& fileId) const;
    bool hasTransferState(const QString& fileId) const;
    void sendResumeRequest(const QString& targetIp, const QString& fileId, qint64 offset);
    void handleResumeRequest(const QString& fileId, qint64 offset, QTcpSocket* socket);

    QUdpSocket* m_udpSocket = nullptr;
    QTcpServer* m_tcpServer = nullptr;
    QMap<QString, IPMsgDevice> m_devices;
    QList<IPMsgMessage> m_messages;
    QMap<QString, QTcpSocket*> m_sendingSockets;
    QMap<QString, qint64> m_sendingProgress;
    QMap<QString, QString> m_pendingFiles;
    QMap<QString, IPMsgTransferState> m_transferStates;
    QMap<QString, IPMsgGroup> m_groups;
    QList<QString> m_friends;
    QList<QString> m_pendingFriendRequests;
    QString m_userName;
    QString m_userId;
    quint16 m_port;
    bool m_running = false;
    QString m_transferStateFile;
    QTimer* m_broadcastTimer = nullptr;
    DatabaseManager* m_database = nullptr;

    // E2EE - End-to-end encryption
    QByteArray localPublicKey;
    QByteArray localPrivateKey;

    struct E2EESession {
        QByteArray peerPublicKey;
        QByteArray sharedSecret;
        QByteArray privateKey;
        QByteArray publicKey;
        qint64 establishedAt;
    };
    QMap<QString, E2EESession> m_e2eeSessions;

    // E2EE helper methods
    QByteArray generateEcdhKeyPair(QByteArray* outPublicKey);
    QByteArray computeSharedSecret(const QByteArray& peerPublicKey, const QByteArray& privateKey);
    QByteArray deriveAesKey(const QByteArray& sharedSecret, const QByteArray& salt);
    QByteArray aesGcmEncrypt(const QByteArray& plaintext, const QByteArray& key, QByteArray* outNonce);
    QByteArray aesGcmDecrypt(const QByteArray& ciphertext, const QByteArray& key, const QByteArray& nonce, const QByteArray& tag);

    // Multi-device sync helpers
    QByteArray serializeSnapshot(const DatabaseManager::SyncSnapshot& snap);
    DatabaseManager::SyncSnapshot deserializeSnapshot(const QJsonObject& json);
    bool syncPeerValid(const QJsonObject& json) const;
    void refreshLocalStateFromDb();
    QString deviceIdByIp(const QString& ip) const;
    void sendSyncSnapshotResponse(const QString& targetIp, bool isReply);
    void sendSyncAck(const QString& targetIp, bool success, int applied);

    QString m_syncAccountId;
    QString m_syncAccountHash;
    QString m_syncKeyHash;
    QSet<QString> m_sameAccountDevices; // device ids sharing our account hash
    QSet<QString> m_pendingSyncs;       // ips awaiting sync ack
};

} // namespace xrk
