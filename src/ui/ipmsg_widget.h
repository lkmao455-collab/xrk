#pragma once

#include <QWidget>
#include <QToolButton>
#include <QListWidget>
#include <QTextEdit>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QProgressBar>
#include <QTimer>
#include <QMap>
#include <QScrollArea>
#include <QFrame>
#include "app/ipmsg_manager.h"
#include "group_statistics_widget.h"
#include "group_member_management_widget.h"
#include "chat_search_widget.h"
#include "send_preview_dialog.h"
#include "send_preview_dialog.h"

namespace xrk {

class IPMsgManager;
class TransferTaskWidget;
class AudioCapture;
class CameraCapture;

class IPMsgWidget : public QWidget {
    Q_OBJECT
public:
    explicit IPMsgWidget(IPMsgManager* manager, QWidget* parent = nullptr);
    ~IPMsgWidget();

signals:
    void sendMessage(const QString& targetIp, const QString& message);
    void sendFile(const QString& targetIp, const QString& filePath);
    void sendFolder(const QString& targetIp, const QString& folderPath);
    void sendImage(const QString& targetIp, const QByteArray& imageData, const QString& fileName);
    void sendReply(const QString& targetIp, const QString& message, const QString& replyTo, const QString& replyContent);
    void sendGroupMessage(const QString& groupId, const QString& message);
    void userNameChanged(const QString& name);

private slots:
    void onSendClicked();
    void onSendFileClicked();
    void onSendFolderClicked();
    void onSendImageClicked();
    void onSendEmoji();
    void onMessageReceived(const QString& sender, const QString& message);
    void onReplyToMessage(const QString& sender, const QString& message, const QString& timestamp);
    void onClearReply();
    void onRecallMessage(const QString& recallId);
    void onMessageRecalled(const QString& recallId, const QString& senderName);
    void onSearchTextChanged(const QString& text);
    void onNotificationClicked();
    void onCreateGroup();
    void onGroupClicked(const QString& groupId);
    void onChangeBackground();
    void onForwardMessage(const QString& message);
    void onMessageRead(const QString& messageId, const QString& readerName);
    void onTypingIndicator(const QString& senderName);
    void showImageViewer(const QByteArray& imageData, const QString& fileName);
    void onFriendRequest(const QString& senderId, const QString& senderName, const QString& message);
    void onFriendRequestAccepted(const QString& senderId, const QString& senderName);
    void onFriendRequestRejected(const QString& senderId, const QString& senderName);
    void sendFriendRequestToContact(const QString& ip);
    void searchChatHistory(const QString& keyword);
    void onGroupSettings();
    void exportChatHistory();
    void onMultiSelectToggled(bool checked);
    void onBatchDelete();
    void onBatchForward();
    void onBatchCopy();
    void showBatchActionBar();
    void hideBatchActionBar();
    void refreshChatDisplay();
    void cleanupOldChatHistory();
    void showCleanupSettings();
    void onVoiceMessageReceived(const IPMsgMessage& message);
    void onVideoMessageReceived(const IPMsgMessage& message);
    void onLocationMessageReceived(const IPMsgMessage& message);
    void onCardMessageReceived(const IPMsgMessage& message);
    void onIncomingCall(const QString& callerId, const QString& callerName);
    void onCallAccepted(const QString& calleeId);
    void onCallRejected(const QString& calleeId);
    void onCallEnded(const QString& peerId);
    void onIceCandidateReceived(const QString& peerId, const QByteArray& sdp);
    void onGroupAnnouncementReceived(const QString& groupId, const QString& announcement, const QString& announcer);
    void onGroupMentionReceived(const QString& groupId, const QStringList& mentionedMembers, const QString& message, const QString& senderName);
    void onGroupVoteReceived(const QString& groupId, const QString& voteTitle, const QStringList& options, int durationSeconds, const QString& creator);
    void onGroupVoteResponseReceived(const QString& groupId, const QString& voteTitle, int selectedOption, const QString& voterId, const QString& voterName);
    void onMergeForwardMessageReceived(const IPMsgMessage& message);
    void showVoiceCallWindow(const QString& peerId);
    void endCallInternal();
    void onFileProgress(const QString& fileName, qint64 bytesTransferred, qint64 totalBytes);
    void onFileCompleted(const QString& fileName, bool integrityOk);
    void onFileResuming(const QString& fileName, qint64 offset);
    void onRefreshDevices();
    void onNameEditFinished();
    void onContactClicked(const QString& ip);
    void onSyncClicked();
    void onSyncCompleted(const QString& deviceId, int appliedCount);
    void onSyncFailed(const QString& deviceId, const QString& reason);
    void onDataSynced();
    void onSameAccountDeviceFound(const QString& deviceId, const QString& deviceName);
    void onStatsToggled(bool checked);
    void onMemberManagementToggled(bool checked);
    void onTransferToggled(bool checked);

private:
    // In-memory chat message storage
    struct ChatMessage {
        QString sender;
        QString content;
        bool isSelf = false;
        QString timestamp;
        QString replyTo;
        QString replyContent;
        bool isFile = false;
        QString fileName;
        bool isImage = false;
        QByteArray imageData;
        qint64 msgTimestamp = 0; // epoch ms
    };
    QList<ChatMessage> m_chatMessages;

    void setupUI();
    void addChatMessage(const QString& sender, const QString& message, bool isSelf, const QString& timestamp = "", const QString& replyTo = "", const QString& replyContent = "");
    void addFileMessage(const QString& sender, const QString& fileName, qint64 fileSize, bool isSelf);
    void addImageMessage(const QString& sender, const QByteArray& imageData, const QString& fileName, bool isSelf);
    void updateContactList();
    void selectContact(const QString& ip);
    QString getAvatarColor(const QString& name) const;
    QString getAvatarLetter(const QString& name) const;
    void showEmojiPanel();
    void scrollToBottom();
    void clearReplyPreview();
    void showContactDetails(const QString& ip);
    void updateRecentChats(const QString& ip, const QString& name, const QString& message);
    void highlightSearchMatches(const QString& keyword);
    void navigateSearchMatch(bool forward);
    void scrollToMatch(int matchIndex);
    void addChatMessageDirect(const ChatMessage& cm, int msgIndex = -1);
    void addFileMessageDirect(const ChatMessage& cm, int msgIndex = -1);
    void addImageMessageDirect(const ChatMessage& cm, int msgIndex = -1);

    // Batch send helpers (Phase 4)
    bool e2eeActiveForTarget() const;
    void sendItems(const QList<SendPreviewItem>& items);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

    IPMsgManager* m_manager = nullptr;
    QString m_targetIp;
    QString m_targetName;

    // Left panel - Contact list
    QWidget* m_contactListWidget = nullptr;
    QVBoxLayout* m_contactListLayout = nullptr;
    QLineEdit* m_searchInput = nullptr;

    // Right panel - Chat
    QWidget* m_chatWidget = nullptr;
    QLabel* m_chatTitleLabel = nullptr;
    QTextBrowser* m_chatDisplay = nullptr;
    QLineEdit* m_messageInput = nullptr;
    QPushButton* m_sendBtn = nullptr;
    QPushButton* m_emojiBtn = nullptr;
    QToolButton* m_fileBtn = nullptr;
    QPushButton* m_imageBtn = nullptr;
    QPushButton* m_groupSettingsBtn = nullptr;
    QPushButton* m_exportChatBtn = nullptr;
    QPushButton* m_multiSelectBtn = nullptr;
    QPushButton* m_syncBtn = nullptr;

    // Status
    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;

    // Name edit
    QLineEdit* m_nameEdit = nullptr;

    // Emoji panel
    QWidget* m_emojiPanel = nullptr;
    bool m_emojiPanelVisible = false;

    // Reply preview
    QWidget* m_replyPreview = nullptr;
    QLabel* m_replyPreviewLabel = nullptr;
    QString m_replyTo;
    QString m_replyContent;

    // Recall support
    QMap<QString, qint64> m_recallableMessages; // recallId -> timestamp
    QTimer* m_recallTimer = nullptr;

    // Search
    QString m_searchFilter;

    // Recent chats
    struct RecentChat {
        QString ip;
        QString name;
        QString lastMessage;
        qint64 timestamp;
        int unreadCount;
    };
    QList<RecentChat> m_recentChats;
    
    // Forward message
    QString m_forwardMessage;
    
    // Group chat
    QString m_targetGroupId;
    QList<IPMsgGroup> m_groups;
    
    // Chat background
    QString m_backgroundPath;
    QColor m_backgroundColor = QColor(60, 60, 60);
    
    // Typing indicator
    QLabel* m_typingLabel = nullptr;
    QTimer* m_typingTimer = nullptr;
    
    // Image viewer
    QMap<QString, QByteArray> m_imageDataMap;

    // Multi-select mode
    bool m_multiSelectMode = false;
    QList<int> m_selectedMessageIndices;
    QWidget* m_batchActionBar = nullptr;
    QLabel* m_batchCountLabel = nullptr;

    // Auto cleanup
    QTimer* m_cleanupTimer = nullptr;
    int m_maxChatHistoryDays = 30;

    // Contact info
    struct ContactInfo {
        QString ip;
        QString name;
        QString deviceId;
        bool online;
        bool dnd;
    };
    QMap<QString, ContactInfo> m_contacts;

    // VoIP call
    QDialog* m_callDialog = nullptr;
    QWidget* m_callWidget = nullptr;
    QTimer* m_callTimer = nullptr;
    int m_callDuration = 0; // seconds
    bool m_isMuted = false;
    bool m_isCameraOn = true;
    QString m_callPeerId;
    AudioCapture* m_callAudioCapture = nullptr;  // microphone capture for the active call
    CameraCapture* m_callCameraCapture = nullptr;  // camera capture for the active call
    QMap<QString, QList<QByteArray>> m_iceCandidates;  // peerId -> ICE candidates

    // Group Statistics
    GroupStatisticsWidget* m_groupStatsWidget = nullptr;
    QWidget* m_statsPanel = nullptr;
    QPushButton* m_statsBtn = nullptr;

    // Group Member Management
    GroupMemberManagementWidget* m_memberManagementWidget = nullptr;
    QWidget* m_memberManagementPanel = nullptr;
    QPushButton* m_memberManagementBtn = nullptr;

    // Transfer task panel (Phase 3)
    TransferTaskWidget* m_transferPanel = nullptr;
    QPushButton* m_transferBtn = nullptr;

    // Cached base directories for incoming folder transfers (keyed by sender+top folder)
    QMap<QString, QString> m_recvFolderBases;

    // Chat Search
    ChatSearchWidget* m_chatSearchWidget = nullptr;
    QList<int> m_searchMatchIndices; // indices into m_chatMessages
    int m_currentMatchIndex = -1;

    // Emoji list
    static const QStringList s_emojis;
};

} // namespace xrk
