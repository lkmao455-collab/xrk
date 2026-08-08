#include "ipmsg_widget.h"
#include "app/ipmsg_manager.h"
#include "hw/audio_capture.h"
#include "hw/camera_capture.h"
#include "app/database_manager.h"
#include "transfer_task_widget.h"
#include "send_preview_dialog.h"
#include "send_preview_dialog.h"
#include <QToolButton>
#include <QMenu>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollBar>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCheckBox>
#include <QScrollArea>
#include <QFrame>
#include <QGridLayout>
#include <QTextCursor>
#include <QDesktopServices>
#include <QUrl>
#include <QEvent>
#include <functional>
#include <QSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QImage>
#include <QBuffer>
#include <QApplication>
#include <QClipboard>
#include <QTimer>
#include <QIcon>
#include <QFileInfo>
#include <QDirIterator>
#include <QDir>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QMenu>
#include <QInputDialog>
#include <QMouseEvent>
#include <QButtonGroup>
#include <QRadioButton>
#include "chat_search_widget.h"

namespace xrk {

const QStringList IPMsgWidget::s_emojis = {
    "😀", "😁", "😂", "🤣", "😃", "😄", "😅", "😆",
    "😉", "😊", "😋", "😎", "😍", "🥰", "😘", "😗",
    "😙", "😚", "🙂", "🤗", "🤩", "🤔", "🤨", "😐",
    "😑", "😶", "🙄", "😏", "😣", "😥", "😮", "🤐",
    "😯", "😪", "😫", "🥱", "😴", "😌", "😛", "😜",
    "😝", "🤤", "😒", "😓", "😔", "😕", "🙃", "🤑",
    "😲", "🙁", "😖", "😞", "😟", "😤", "😢", "😭",
    "😦", "😧", "😨", "😩", "🤯", "😬", "😰", "😱",
    "🥵", "🥶", "😳", "🤪", "😵", "🥴", "😠", "😡",
    "🤬", "👍", "👎", "👌", "✌️", "🤞", "🤟", "🤘",
    "🤙", "👋", "🤚", "🖐️", "✋", "🖖", "👏", "🙌",
    "🤝", "🙏", "❤️", "🧡", "💛", "💚", "💙", "💜",
    "🖤", "💔", "❣️", "💕", "💞", "💓", "💗", "💖",
    "💘", "💝", "🔥", "⭐", "🌟", "💫", "✨", "🎉"
};

IPMsgWidget::IPMsgWidget(IPMsgManager* manager, QWidget* parent)
    : QWidget(parent), m_manager(manager) {
    setupUI();

    if (m_manager) {
        connect(m_manager, &IPMsgManager::deviceFound, this, [this](const IPMsgDevice&) {
            updateContactList();
        });

        connect(m_manager, &IPMsgManager::deviceLeft, this, [this](const QString&) {
            updateContactList();
        });

        connect(m_manager, &IPMsgManager::messageReceived, this, [this](const IPMsgMessage& msg) {
            if (msg.isImage) {
                addImageMessage(msg.senderName, msg.imageData, msg.imageFileName, false);
            } else if (!msg.replyTo.isEmpty()) {
                addChatMessage(msg.senderName, msg.content, false, "", msg.replyTo, msg.replyContent);
            } else {
                onMessageReceived(msg.senderName, msg.content);
            }
        });

        connect(m_manager, &IPMsgManager::fileProgress, this, [this](const QString& fileId, qint64 bytes, qint64 total, int) {
            onFileProgress(fileId, bytes, total);
        });

        connect(m_manager, &IPMsgManager::fileCompleted, this, [this](const QString& fileId, const QString& filePath, bool integrityOk) {
            onFileCompleted(fileId, integrityOk);
        });

        connect(m_manager, &IPMsgManager::fileResuming, this, [this](const QString& fileId, qint64 offset) {
            onFileResuming(fileId, offset);
        });

        connect(m_manager, &IPMsgManager::fileReceiveRequest, this, [this](const QString& fileId, const QString& senderName,
                           const QString& fileName, qint64 fileSize, bool isDirectory,
                           const QString& md5, const QString& relativePath) {
            QString sizeStr;
            if (fileSize < 1024) sizeStr = QString("%1 B").arg(fileSize);
            else if (fileSize < 1024 * 1024) sizeStr = QString("%1 KB").arg(fileSize / 1024);
            else sizeStr = QString("%1 MB").arg(double(fileSize) / (1024.0 * 1024.0), 0, 'f', 1);

            // Files belonging to a folder share a base directory (chosen once per folder).
            bool isFolderFile = relativePath.contains('/');
            if (isFolderFile) {
                QString key = senderName + "|" + relativePath.section('/', 0, 0);
                if (m_recvFolderBases.value(key).isEmpty()) {
                    QString base = QFileDialog::getExistingDirectory(this, tr("选择保存文件夹"), QDir::homePath());
                    if (base.isEmpty()) {
                        m_manager->rejectFile(fileId);
                        return;
                    }
                    m_recvFolderBases[key] = base;
                }
                QString savePath = QDir(m_recvFolderBases.value(key)).filePath(relativePath);
                m_manager->acceptFile(fileId, savePath);
                addChatMessage("", tr("正在接收 %1...").arg(relativePath), false);
                return;
            }

            QMessageBox::StandardButton reply = QMessageBox::question(this,
                tr("文件接收请求"),
                tr("%1 发送了文件 %2 (%3)\n是否接收？").arg(senderName, fileName, sizeStr),
                QMessageBox::Yes | QMessageBox::No);

            if (reply == QMessageBox::Yes) {
                QString savePath = QFileDialog::getSaveFileName(this, tr("保存文件"), fileName);
                if (!savePath.isEmpty()) {
                    m_manager->acceptFile(fileId, savePath);
                    addChatMessage("", tr("正在接收文件 %1...").arg(fileName), false);
                }
            } else {
                m_manager->rejectFile(fileId);
            }
        });

        connect(m_manager, &IPMsgManager::messageRecalled, this, &IPMsgWidget::onMessageRecalled);
        connect(m_manager, &IPMsgManager::messageRead, this, &IPMsgWidget::onMessageRead);
        connect(m_manager, &IPMsgManager::typingIndicatorReceived, this, &IPMsgWidget::onTypingIndicator);
        connect(m_manager, &IPMsgManager::friendRequestReceived, this, &IPMsgWidget::onFriendRequest);
        connect(m_manager, &IPMsgManager::friendRequestAccepted, this, &IPMsgWidget::onFriendRequestAccepted);
        connect(m_manager, &IPMsgManager::friendRequestRejected, this, &IPMsgWidget::onFriendRequestRejected);
        connect(m_manager, &IPMsgManager::voiceMessageReceived, this, &IPMsgWidget::onVoiceMessageReceived);
        connect(m_manager, &IPMsgManager::videoMessageReceived, this, &IPMsgWidget::onVideoMessageReceived);
        connect(m_manager, &IPMsgManager::locationMessageReceived, this, &IPMsgWidget::onLocationMessageReceived);
    connect(m_manager, &IPMsgManager::cardMessageReceived, this, &IPMsgWidget::onCardMessageReceived);
    connect(m_manager, &IPMsgManager::incomingCall, this, &IPMsgWidget::onIncomingCall);
    connect(m_manager, &IPMsgManager::callAccepted, this, &IPMsgWidget::onCallAccepted);
    connect(m_manager, &IPMsgManager::callRejected, this, &IPMsgWidget::onCallRejected);
    connect(m_manager, &IPMsgManager::callEnded, this, &IPMsgWidget::onCallEnded);
    connect(m_manager, &IPMsgManager::iceCandidateReceived, this, &IPMsgWidget::onIceCandidateReceived);
    connect(m_manager, &IPMsgManager::groupAnnouncementReceived, this, &IPMsgWidget::onGroupAnnouncementReceived);
    connect(m_manager, &IPMsgManager::groupMentionReceived, this, &IPMsgWidget::onGroupMentionReceived);
    connect(m_manager, &IPMsgManager::groupVoteReceived, this, &IPMsgWidget::onGroupVoteReceived);
    connect(m_manager, &IPMsgManager::groupVoteResponseReceived, this, &IPMsgWidget::onGroupVoteResponseReceived);
    connect(m_manager, &IPMsgManager::mergeForwardMessageReceived, this, &IPMsgWidget::onMergeForwardMessageReceived);
    connect(m_manager, &IPMsgManager::syncCompleted, this, &IPMsgWidget::onSyncCompleted);
    connect(m_manager, &IPMsgManager::syncFailed, this, &IPMsgWidget::onSyncFailed);
    connect(m_manager, &IPMsgManager::dataSynced, this, &IPMsgWidget::onDataSynced);
    connect(m_manager, &IPMsgManager::sameAccountDeviceFound, this, &IPMsgWidget::onSameAccountDeviceFound);
    }
    
    // Setup auto cleanup timer (daily at 3 AM)
    m_cleanupTimer = new QTimer(this);
    connect(m_cleanupTimer, &QTimer::timeout, this, &IPMsgWidget::cleanupOldChatHistory);
    m_cleanupTimer->start(24 * 60 * 60 * 1000); // 24 hours
    
    // Initial cleanup check
    QTimer::singleShot(5000, this, &IPMsgWidget::cleanupOldChatHistory);
}

IPMsgWidget::~IPMsgWidget() {
}

QString IPMsgWidget::getAvatarColor(const QString& name) const {
    QStringList colors = {"#1E88E5", "#43A047", "#E53935", "#FB8C00", "#8E24AA", "#00ACC1", "#F4511E", "#3949AB"};
    int hash = qHash(name) % colors.size();
    return colors[hash];
}

QString IPMsgWidget::getAvatarLetter(const QString& name) const {
    if (name.isEmpty()) return "?";
    return QString(name.at(0)).toUpper();
}

void IPMsgWidget::setupUI() {
    // Apply global dark theme stylesheet
    this->setStyleSheet(
        "QWidget { font-family: 'Microsoft YaHei', 'Segoe UI', sans-serif; }"
        "QScrollBar:vertical { width: 8px; background: transparent; }"
        "QScrollBar::handle:vertical { background: #4A4A4A; border-radius: 4px; min-height: 40px; }"
        "QScrollBar::handle:vertical:hover { background: #5A5A5A; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        "QMenu { background-color: #2E2E2E; color: #E0E0E0; border: 1px solid #444; border-radius: 6px; padding: 4px; }"
        "QMenu::item { padding: 8px 20px; border-radius: 4px; }"
        "QMenu::item:selected { background-color: #07C160; }"
        "QMenu::separator { height: 1px; background: #444; margin: 4px 8px; }"
        "QToolTip { background-color: #3A3A3A; color: #E0E0E0; border: 1px solid #444; border-radius: 4px; padding: 4px 8px; }"
        "QLineEdit { background-color: #3A3A3A; color: #E0E0E0; border: 1px solid #444; border-radius: 6px; padding: 8px 12px; }"
        "QLineEdit:focus { border-color: #07C160; }"
        "QTextEdit { background-color: #2E2E2E; color: #E0E0E0; border: none; }"
        "QToolTip { background-color: #3A3A3A; color: #E0E0E0; border: 1px solid #444; border-radius: 4px; padding: 6px 10px; font-size: 12px; }"
    );

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ========== Left Panel ==========
    auto* leftPanel = new QWidget();
    leftPanel->setFixedWidth(320);
    leftPanel->setStyleSheet("QWidget { background-color: #1E1E1E; border-right: 1px solid #333; }");
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    // Header with name edit
    auto* headerWidget = new QWidget();
    headerWidget->setFixedHeight(72);
    headerWidget->setStyleSheet("QWidget { background-color: #252525; border-bottom: 1px solid #333; }");
    auto* headerLayout = new QVBoxLayout(headerWidget);
    headerLayout->setContentsMargins(16, 12, 16, 12);

    auto* nameRow = new QHBoxLayout();
    auto* avatarLabel = new QLabel(this);
    avatarLabel->setFixedSize(44, 44);
    QString myName = m_manager ? m_manager->userName() : "Me";
    avatarLabel->setStyleSheet(QString(
        "background-color: %1; color: white; border-radius: 22px; font-weight: 600; font-size: 18px;")
        .arg(getAvatarColor(myName)));
    avatarLabel->setAlignment(Qt::AlignCenter);
    avatarLabel->setText(getAvatarLetter(myName));
    nameRow->addWidget(avatarLabel);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setText(myName);
    m_nameEdit->setStyleSheet("QLineEdit { background: transparent; color: white; border: none; font-size: 16px; font-weight: 600; padding: 0; }");
    m_nameEdit->setPlaceholderText(tr("输入昵称"));
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &IPMsgWidget::onNameEditFinished);
    nameRow->addWidget(m_nameEdit, 1);
    headerLayout->addLayout(nameRow);

    leftLayout->addWidget(headerWidget);

    // Search box
    auto* searchWidget = new QWidget();
    searchWidget->setStyleSheet("QWidget { background-color: #252525; }");
    auto* searchLayout = new QHBoxLayout(searchWidget);
    searchLayout->setContentsMargins(16, 12, 16, 12);

    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText(tr("搜索联系人"));
    m_searchInput->setStyleSheet(
        "QLineEdit { background-color: #2E2E2E; color: #E0E0E0; border: 1px solid #444; border-radius: 20px; "
        "padding: 8px 16px; font-size: 13px; }");
    connect(m_searchInput, &QLineEdit::textChanged, this, &IPMsgWidget::onSearchTextChanged);
    searchLayout->addWidget(m_searchInput);
    leftLayout->addWidget(searchWidget);

    // Contact list
    auto* contactScrollArea = new QScrollArea(this);
    contactScrollArea->setWidgetResizable(true);
    contactScrollArea->setFrameShape(QFrame::NoFrame);
    contactScrollArea->setStyleSheet("QScrollArea { background-color: #1E1E1E; border: none; }");

    m_contactListWidget = new QWidget();
    m_contactListLayout = new QVBoxLayout(m_contactListWidget);
    m_contactListLayout->setAlignment(Qt::AlignTop);
    m_contactListLayout->setContentsMargins(0, 0, 0, 0);
    m_contactListLayout->setSpacing(4);
    contactScrollArea->setWidget(m_contactListWidget);

    leftLayout->addWidget(contactScrollArea, 1);

    mainLayout->addWidget(leftPanel);

    // ========== Right Panel ==========
    auto* rightPanel = new QWidget();
    rightPanel->setStyleSheet("QWidget { background-color: #252525; }");
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    // Chat header
    auto* chatHeader = new QWidget();
    chatHeader->setFixedHeight(60);
    chatHeader->setStyleSheet("QWidget { background-color: #252525; border-bottom: 1px solid #333; }");
    auto* chatHeaderLayout = new QHBoxLayout(chatHeader);
    chatHeaderLayout->setContentsMargins(16, 0, 16, 0);

    m_chatTitleLabel = new QLabel(tr("选择联系人开始聊天"), this);
    m_chatTitleLabel->setStyleSheet("color: white; font-size: 17px; font-weight: 600;");
    chatHeaderLayout->addWidget(m_chatTitleLabel);
    
    m_typingLabel = new QLabel(this);
    m_typingLabel->setStyleSheet("color: #07C160; font-size: 12px; font-style: italic;");
    m_typingLabel->setVisible(false);
    chatHeaderLayout->addWidget(m_typingLabel);
    
    chatHeaderLayout->addStretch();

    auto* refreshBtn = new QPushButton(QIcon(":/icons/refresh.svg"), tr("刷新"), this);
    refreshBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 6px; "
        "padding: 6px 14px; font-size: 12px; font-weight: 500; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }");
    refreshBtn->setIconSize(QSize(16, 16));
    refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(refreshBtn, &QPushButton::clicked, this, &IPMsgWidget::onRefreshDevices);
    chatHeaderLayout->addWidget(refreshBtn);

    auto* searchChatBtn = new QPushButton(QIcon(":/icons/search.svg"), "", this);
    searchChatBtn->setFixedSize(36, 36);
    searchChatBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 6px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }");
    searchChatBtn->setToolTip(tr("搜索聊天记录"));
    searchChatBtn->setIconSize(QSize(20, 20));
    searchChatBtn->setCursor(Qt::PointingHandCursor);
    connect(searchChatBtn, &QPushButton::clicked, this, [this]() {
        if (m_targetIp.isEmpty() && m_targetGroupId.isEmpty()) {
            QMessageBox::warning(this, tr("提示"), tr("请先选择联系人"));
            return;
        }
        m_chatSearchWidget->activate();
    });
    chatHeaderLayout->addWidget(searchChatBtn);

    m_exportChatBtn = new QPushButton(QIcon(":/icons/export.svg"), "", this);
    m_exportChatBtn->setFixedSize(36, 36);
    m_exportChatBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 6px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }");
    m_exportChatBtn->setToolTip(tr("导出聊天记录"));
    m_exportChatBtn->setIconSize(QSize(20, 20));
    m_exportChatBtn->setCursor(Qt::PointingHandCursor);
    connect(m_exportChatBtn, &QPushButton::clicked, this, &IPMsgWidget::exportChatHistory);
    chatHeaderLayout->addWidget(m_exportChatBtn);

    m_multiSelectBtn = new QPushButton(QIcon(":/icons/multi-select.svg"), "", this);
    m_multiSelectBtn->setFixedSize(36, 36);
    m_multiSelectBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 6px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }"
        "QPushButton:checked { background-color: #07C160; color: white; }");
    m_multiSelectBtn->setCheckable(true);
    m_multiSelectBtn->setToolTip(tr("多选模式"));
    m_multiSelectBtn->setIconSize(QSize(20, 20));
    m_multiSelectBtn->setCursor(Qt::PointingHandCursor);
    connect(m_multiSelectBtn, &QPushButton::toggled, this, &IPMsgWidget::onMultiSelectToggled);
    chatHeaderLayout->addWidget(m_multiSelectBtn);

    // Voice/Video call buttons
    QPushButton* voiceCallBtn = new QPushButton(QIcon(":/icons/voice-call.svg"), "", this);
    voiceCallBtn->setFixedSize(36, 36);
    voiceCallBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 6px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }");
    voiceCallBtn->setToolTip(tr("语音通话"));
    voiceCallBtn->setIconSize(QSize(20, 20));
    voiceCallBtn->setCursor(Qt::PointingHandCursor);
    connect(voiceCallBtn, &QPushButton::clicked, this, [this]() {
        if (!m_targetIp.isEmpty()) {
            m_manager->initiateCall(m_targetIp, "voice");
        }
    });
    chatHeaderLayout->addWidget(voiceCallBtn);

    QPushButton* videoCallBtn = new QPushButton(QIcon(":/icons/video-call.svg"), "", this);
    videoCallBtn->setFixedSize(36, 36);
    videoCallBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 6px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }");
    videoCallBtn->setToolTip(tr("视频通话"));
    videoCallBtn->setIconSize(QSize(20, 20));
    videoCallBtn->setCursor(Qt::PointingHandCursor);
    connect(videoCallBtn, &QPushButton::clicked, this, [this]() {
        if (!m_targetIp.isEmpty()) {
            m_manager->initiateCall(m_targetIp, "video");
        }
    });
    chatHeaderLayout->addWidget(videoCallBtn);

    m_syncBtn = new QPushButton(QIcon(":/icons/sync.svg"), "", this);
    m_syncBtn->setFixedSize(36, 36);
    m_syncBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 6px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }");
    m_syncBtn->setToolTip(tr("多设备同步"));
    m_syncBtn->setIconSize(QSize(20, 20));
    m_syncBtn->setCursor(Qt::PointingHandCursor);
    connect(m_syncBtn, &QPushButton::clicked, this, &IPMsgWidget::onSyncClicked);
    chatHeaderLayout->addWidget(m_syncBtn);

    m_groupSettingsBtn = new QPushButton(QIcon(":/icons/settings.svg"), "", this);
    m_groupSettingsBtn->setFixedSize(36, 36);
    m_groupSettingsBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 6px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }");
    m_groupSettingsBtn->setToolTip(tr("群组设置"));
    m_groupSettingsBtn->setIconSize(QSize(20, 20));
    m_groupSettingsBtn->setCursor(Qt::PointingHandCursor);
    m_groupSettingsBtn->setVisible(false);
    connect(m_groupSettingsBtn, &QPushButton::clicked, this, &IPMsgWidget::onGroupSettings);
    chatHeaderLayout->addWidget(m_groupSettingsBtn);

    // Statistics button
    m_statsBtn = new QPushButton(QIcon(":/icons/sync.svg"), "", this);
    m_statsBtn->setFixedSize(36, 36);
    m_statsBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 6px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }"
        "QPushButton:checked { background-color: #07C160; color: white; }");
    m_statsBtn->setCheckable(true);
    m_statsBtn->setToolTip(tr("群组统计"));
    m_statsBtn->setIconSize(QSize(20, 20));
    m_statsBtn->setCursor(Qt::PointingHandCursor);
    m_statsBtn->setVisible(false);
    connect(m_statsBtn, &QPushButton::toggled, this, &IPMsgWidget::onStatsToggled);
    chatHeaderLayout->addWidget(m_statsBtn);

    // Member management button
    m_memberManagementBtn = new QPushButton(QIcon(":/icons/group.svg"), "", this);
    m_memberManagementBtn->setFixedSize(36, 36);
    m_memberManagementBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 6px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }"
        "QPushButton:checked { background-color: #07C160; color: white; }");
    m_memberManagementBtn->setCheckable(true);
    m_memberManagementBtn->setToolTip(tr("成员管理"));
    m_memberManagementBtn->setIconSize(QSize(20, 20));
    m_memberManagementBtn->setCursor(Qt::PointingHandCursor);
    m_memberManagementBtn->setVisible(false);
    connect(m_memberManagementBtn, &QPushButton::toggled, this, &IPMsgWidget::onMemberManagementToggled);
    chatHeaderLayout->addWidget(m_memberManagementBtn);

    // Transfer task button
    m_transferBtn = new QPushButton(QIcon(":/icons/files.svg"), "", this);
    m_transferBtn->setFixedSize(36, 36);
    m_transferBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 6px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }"
        "QPushButton:checked { background-color: #07C160; color: white; }");
    m_transferBtn->setCheckable(true);
    m_transferBtn->setToolTip(tr("传输任务"));
    m_transferBtn->setIconSize(QSize(20, 20));
    m_transferBtn->setCursor(Qt::PointingHandCursor);
    connect(m_transferBtn, &QPushButton::toggled, this, &IPMsgWidget::onTransferToggled);
    chatHeaderLayout->addWidget(m_transferBtn);

    rightLayout->addWidget(chatHeader);

    // Chat display area
    auto* chatContainer = new QWidget();
    chatContainer->setStyleSheet("QWidget { background-color: #1E1E1E; }");
    auto* chatContainerLayout = new QVBoxLayout(chatContainer);
    chatContainerLayout->setContentsMargins(0, 0, 0, 0);

    // Chat search widget (floating overlay)
    m_chatSearchWidget = new ChatSearchWidget(chatContainer);
    connect(m_chatSearchWidget, &ChatSearchWidget::searchChanged, this, &IPMsgWidget::highlightSearchMatches);
    connect(m_chatSearchWidget, &ChatSearchWidget::searchNext, this, [this](const QString& kw) {
        m_currentMatchIndex++;
        navigateSearchMatch(true);
    });
    connect(m_chatSearchWidget, &ChatSearchWidget::searchPrev, this, [this](const QString& kw) {
        m_currentMatchIndex--;
        navigateSearchMatch(false);
    });
    connect(m_chatSearchWidget, &ChatSearchWidget::searchClose, this, [this]() {
        m_searchMatchIndices.clear();
        m_currentMatchIndex = -1;
        // Rebuild chat display without highlights
        refreshChatDisplay();
    });

    m_chatDisplay = new QTextBrowser(this);
    m_chatDisplay->setReadOnly(true);
    m_chatDisplay->setStyleSheet(
        "QTextEdit { background-color: #1E1E1E; border: none; padding: 16px; }"
        "QScrollBar:vertical { width: 8px; background: transparent; }"
        "QScrollBar::handle:vertical { background: #4A4A4A; border-radius: 4px; min-height: 40px; }"
        "QScrollBar::handle:vertical:hover { background: #5A5A5A; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");
    
    // Handle image clicks
    connect(m_chatDisplay, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        QString scheme = url.scheme();
        if (scheme == "image") {
            QString imageId = url.host();
            if (m_imageDataMap.contains(imageId)) {
                showImageViewer(m_imageDataMap[imageId], imageId);
            }
        } else if (scheme == "select") {
            int msgIndex = url.host().toInt();
            if (m_multiSelectMode && msgIndex >= 0 && msgIndex < m_chatMessages.size()) {
                if (m_selectedMessageIndices.contains(msgIndex)) {
                    m_selectedMessageIndices.removeOne(msgIndex);
                } else {
                    m_selectedMessageIndices.append(msgIndex);
                }
                m_batchCountLabel->setText(tr("已选择 %1 条").arg(m_selectedMessageIndices.size()));
                refreshChatDisplay();
            }
        }
    });
    
    m_chatDisplay->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_chatDisplay, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu* menu = new QMenu(this);
        menu->setStyleSheet("QMenu { background-color: #2E2E2E; color: #E0E0E0; border: 1px solid #444; border-radius: 6px; padding: 4px; }"
                           "QMenu::item { padding: 8px 20px; border-radius: 4px; }"
                           "QMenu::item:selected { background-color: #07C160; }"
                           "QMenu::separator { height: 1px; background: #444; margin: 4px 8px; }");
        
        // Get selected text
        QTextCursor cursor = m_chatDisplay->cursorForPosition(pos);
        cursor.select(QTextCursor::BlockUnderCursor);
        QString selectedText = cursor.selectedText();
        
        // Copy option
        if (!selectedText.isEmpty()) {
            menu->addAction(tr("复制"), this, [this, selectedText]() {
                QApplication::clipboard()->setText(selectedText);
            });
            menu->addSeparator();
        }
        
        // Forward option
        if (!selectedText.isEmpty()) {
            menu->addAction(tr("转发"), this, [this, selectedText]() {
                onForwardMessage(selectedText);
            });
        }
        
        // Check if we have recallable messages
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        bool hasRecallable = false;
        for (auto it = m_recallableMessages.constBegin(); it != m_recallableMessages.constEnd(); ++it) {
            if (now - it.value() < 120000) { // 2 minutes
                hasRecallable = true;
                break;
            }
        }
        
        if (hasRecallable) {
            menu->addAction(tr("撤回消息"), this, [this]() {
                qint64 now = QDateTime::currentMSecsSinceEpoch();
                for (auto it = m_recallableMessages.constBegin(); it != m_recallableMessages.constEnd(); ++it) {
                    if (now - it.value() < 120000) {
                        onRecallMessage(it.key());
                        break;
                    }
                }
            });
        }
        
        // Multi-select mode
        menu->addSeparator();
        menu->addAction(tr("多选模式"), this, [this]() {
            QMessageBox::information(this, tr("多选模式"), tr("按住Ctrl点击消息可多选"));
        });
        
        menu->popup(m_chatDisplay->mapToGlobal(pos));
    });
    chatContainerLayout->addWidget(m_chatDisplay);

    rightLayout->addWidget(chatContainer, 1);

    // Statistics panel (hidden by default)
    m_statsPanel = new QWidget(this);
    m_statsPanel->setStyleSheet("QWidget { background-color: #1E1E1E; border-top: 1px solid #333; }");
    m_statsPanel->setVisible(false);
    auto* statsPanelLayout = new QVBoxLayout(m_statsPanel);
    statsPanelLayout->setContentsMargins(0, 0, 0, 0);
    statsPanelLayout->setSpacing(0);

    // Statistics panel header
    auto* statsHeader = new QWidget(m_statsPanel);
    statsHeader->setFixedHeight(40);
    statsHeader->setStyleSheet("QWidget { background-color: #252525; border-bottom: 1px solid #333; }");
    auto* statsHeaderLayout = new QHBoxLayout(statsHeader);
    statsHeaderLayout->setContentsMargins(16, 0, 16, 0);
    statsHeaderLayout->setSpacing(8);

    QLabel* statsTitle = new QLabel(tr("📊 实时群组统计"), statsHeader);
    statsTitle->setStyleSheet("color: #07C160; font-size: 14px; font-weight: 600;");
    statsHeaderLayout->addWidget(statsTitle);

    statsHeaderLayout->addStretch();

    QPushButton* statsCloseBtn = new QPushButton(QIcon(":/icons/export.svg"), "", statsHeader);
    statsCloseBtn->setFixedSize(28, 28);
    statsCloseBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4A4A4A; }");
    statsCloseBtn->setIconSize(QSize(16, 16));
    statsCloseBtn->setCursor(Qt::PointingHandCursor);
    connect(statsCloseBtn, &QPushButton::clicked, this, [this]() {
        m_statsBtn->setChecked(false);
    });
    statsHeaderLayout->addWidget(statsCloseBtn);

    statsPanelLayout->addWidget(statsHeader);

    // Create and add GroupStatisticsWidget
    m_groupStatsWidget = new GroupStatisticsWidget(m_statsPanel);
    m_groupStatsWidget->setManager(m_manager);
    statsPanelLayout->addWidget(m_groupStatsWidget, 1);

    rightLayout->addWidget(m_statsPanel);

    // Member management panel (hidden by default)
    m_memberManagementPanel = new QWidget(this);
    m_memberManagementPanel->setStyleSheet("QWidget { background-color: #1E1E1E; border-top: 1px solid #333; }");
    m_memberManagementPanel->setVisible(false);
    auto* memberManagementPanelLayout = new QVBoxLayout(m_memberManagementPanel);
    memberManagementPanelLayout->setContentsMargins(0, 0, 0, 0);
    memberManagementPanelLayout->setSpacing(0);

    // Member management panel header
    auto* memberHeader = new QWidget(m_memberManagementPanel);
    memberHeader->setFixedHeight(40);
    memberHeader->setStyleSheet("QWidget { background-color: #252525; border-bottom: 1px solid #333; }");
    auto* memberHeaderLayout = new QHBoxLayout(memberHeader);
    memberHeaderLayout->setContentsMargins(16, 0, 16, 0);
    memberHeaderLayout->setSpacing(8);

    QLabel* memberTitle = new QLabel(tr("👥 成员管理"), memberHeader);
    memberTitle->setStyleSheet("color: #07C160; font-size: 14px; font-weight: 600;");
    memberHeaderLayout->addWidget(memberTitle);

    memberHeaderLayout->addStretch();

    QPushButton* memberCloseBtn = new QPushButton(QIcon(":/icons/export.svg"), "", memberHeader);
    memberCloseBtn->setFixedSize(28, 28);
    memberCloseBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4A4A4A; }");
    memberCloseBtn->setIconSize(QSize(16, 16));
    memberCloseBtn->setCursor(Qt::PointingHandCursor);
    connect(memberCloseBtn, &QPushButton::clicked, this, [this]() {
        m_memberManagementBtn->setChecked(false);
    });
    memberHeaderLayout->addWidget(memberCloseBtn);

    memberManagementPanelLayout->addWidget(memberHeader);

    // Create and add GroupMemberManagementWidget
    m_memberManagementWidget = new GroupMemberManagementWidget(m_memberManagementPanel);
    m_memberManagementWidget->setManager(m_manager);
    memberManagementPanelLayout->addWidget(m_memberManagementWidget, 1);

    rightLayout->addWidget(m_memberManagementPanel);

    // Transfer task panel (hidden by default)
    m_transferPanel = new TransferTaskWidget(m_manager, this);
    m_transferPanel->setStyleSheet("QWidget { background-color: #1E1E1E; border-top: 1px solid #333; }");
    m_transferPanel->setVisible(false);
    rightLayout->addWidget(m_transferPanel);

    // Emoji panel (hidden by default)
m_emojiPanel = new QWidget(this);
    m_emojiPanel->setFixedHeight(220);
    m_emojiPanel->setStyleSheet("QWidget { background-color: #252525; border-top: 1px solid #333; }");
    m_emojiPanel->setVisible(false);
    auto* emojiLayout = new QGridLayout(m_emojiPanel);
    emojiLayout->setContentsMargins(12, 12, 12, 12);
    emojiLayout->setSpacing(4);

    int row = 0, col = 0;
    for (const QString& emoji : s_emojis) {
        auto* emojiBtn = new QPushButton(emoji, this);
        emojiBtn->setFixedSize(40, 40);
        emojiBtn->setStyleSheet(
            "QPushButton { background-color: #2E2E2E; border: none; border-radius: 8px; font-size: 22px; }"
            "QPushButton:hover { background-color: #3A3A3A; }"
            "QPushButton:pressed { background-color: #252525; }");
        emojiBtn->setCursor(Qt::PointingHandCursor);
        connect(emojiBtn, &QPushButton::clicked, this, [this, emoji]() {
            m_messageInput->insert(emoji);
            m_emojiPanel->setVisible(false);
            m_emojiPanelVisible = false;
        });
        emojiLayout->addWidget(emojiBtn, row, col);
        col++;
        if (col >= 10) {
            col = 0;
            row++;
        }
    }
    rightLayout->addWidget(m_emojiPanel);

    // Progress bar
    m_progressBar = new QProgressBar(this);
    m_progressBar->setFixedHeight(3);
    m_progressBar->setVisible(false);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { background-color: transparent; border: none; }"
        "QProgressBar::chunk { background-color: #07C160; border-radius: 1.5px; }");
    rightLayout->addWidget(m_progressBar);

    // Input area
    auto* inputArea = new QWidget();
    inputArea->setStyleSheet("QWidget { background-color: #252525; border-top: 1px solid #333; }");
    auto* inputLayout = new QVBoxLayout(inputArea);
    inputLayout->setContentsMargins(16, 12, 16, 12);
    inputLayout->setSpacing(10);

    // Toolbar row
    auto* toolbarLayout = new QHBoxLayout();
    toolbarLayout->setSpacing(8);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);

    m_emojiBtn = new QPushButton(QIcon(":/icons/emoji.svg"), "", this);
    m_emojiBtn->setFixedSize(40, 40);
    m_emojiBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; border: none; border-radius: 8px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }");
    m_emojiBtn->setToolTip(tr("表情"));
    m_emojiBtn->setIconSize(QSize(24, 24));
    m_emojiBtn->setCursor(Qt::PointingHandCursor);
    connect(m_emojiBtn, &QPushButton::clicked, this, &IPMsgWidget::onSendEmoji);
    toolbarLayout->addWidget(m_emojiBtn);

    m_fileBtn = new QToolButton(this);
    m_fileBtn->setIcon(QIcon(":/icons/file.svg"));
    m_fileBtn->setFixedSize(40, 40);
    m_fileBtn->setPopupMode(QToolButton::MenuButtonPopup);
    m_fileBtn->setStyleSheet(
        "QToolButton { background-color: #3A3A3A; border: none; border-radius: 8px; }"
        "QToolButton:hover { background-color: #4A4A4A; }"
        "QToolButton:pressed { background-color: #2A2A2A; }"
        "QToolButton::menu-indicator { subcontrol-position: right center; padding-right: 2px; }");
    m_fileBtn->setToolTip(tr("发送文件/文件夹"));
    m_fileBtn->setIconSize(QSize(24, 24));
    m_fileBtn->setCursor(Qt::PointingHandCursor);
    auto* fileMenu = new QMenu(m_fileBtn);
    fileMenu->setStyleSheet("QMenu { background-color: #2A2A2A; color: #E0E0E0; border: 1px solid #444; }"
                            "QMenu::item:selected { background-color: #3A3A3A; }");
    QAction* actFile = fileMenu->addAction(tr("发送文件"));
    QAction* actFolder = fileMenu->addAction(tr("发送文件夹"));
    connect(actFile, &QAction::triggered, this, &IPMsgWidget::onSendFileClicked);
    connect(actFolder, &QAction::triggered, this, &IPMsgWidget::onSendFolderClicked);
    m_fileBtn->setMenu(fileMenu);
    m_fileBtn->setDefaultAction(actFile);
    toolbarLayout->addWidget(m_fileBtn);

    m_imageBtn = new QPushButton(QIcon(":/icons/image.svg"), "", this);
    m_imageBtn->setFixedSize(40, 40);
    m_imageBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; border: none; border-radius: 8px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }");
    m_imageBtn->setToolTip(tr("发送图片"));
    m_imageBtn->setIconSize(QSize(24, 24));
    m_imageBtn->setCursor(Qt::PointingHandCursor);
    connect(m_imageBtn, &QPushButton::clicked, this, &IPMsgWidget::onSendImageClicked);
    toolbarLayout->addWidget(m_imageBtn);

    auto* groupBtn = new QPushButton(QIcon(":/icons/group.svg"), "", this);
    groupBtn->setFixedSize(40, 40);
    groupBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; border: none; border-radius: 8px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }");
    groupBtn->setToolTip(tr("创建群组"));
    groupBtn->setIconSize(QSize(24, 24));
    groupBtn->setCursor(Qt::PointingHandCursor);
    connect(groupBtn, &QPushButton::clicked, this, &IPMsgWidget::onCreateGroup);
    toolbarLayout->addWidget(groupBtn);

    auto* bgBtn = new QPushButton(QIcon(":/icons/background.svg"), "", this);
    bgBtn->setFixedSize(40, 40);
    bgBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; border: none; border-radius: 8px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }");
    bgBtn->setToolTip(tr("背景设置"));
    bgBtn->setIconSize(QSize(24, 24));
    bgBtn->setCursor(Qt::PointingHandCursor);
    connect(bgBtn, &QPushButton::clicked, this, &IPMsgWidget::onChangeBackground);
    toolbarLayout->addWidget(bgBtn);

    toolbarLayout->addStretch();

    // Send button in toolbar - prominent accent button
    m_sendBtn = new QPushButton(QIcon(":/icons/send.svg"), "", this);
    m_sendBtn->setFixedSize(80, 40);
    m_sendBtn->setStyleSheet(
        "QPushButton { background-color: #07C160; color: white; border: none; border-radius: 8px; font-weight: 600; }"
        "QPushButton:hover { background-color: #06AD56; }"
        "QPushButton:pressed { background-color: #059A4C; }"
        "QPushButton:disabled { background-color: #444; color: #888; }");
    m_sendBtn->setToolTip(tr("发送消息 (Enter)"));
    m_sendBtn->setIconSize(QSize(20, 20));
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    connect(m_sendBtn, &QPushButton::clicked, this, &IPMsgWidget::onSendClicked);
    toolbarLayout->addWidget(m_sendBtn);

    inputLayout->addLayout(toolbarLayout);

    // Reply preview (hidden by default)
    m_replyPreview = new QWidget(this);
    m_replyPreview->setStyleSheet("background-color: #2E2E2E; border-left: 3px solid #07C160; border-radius: 0 4px 4px 0; padding: 8px 12px;");
    m_replyPreview->setVisible(false);
    auto* replyLayout = new QHBoxLayout(m_replyPreview);
    replyLayout->setContentsMargins(12, 8, 12, 8);
    m_replyPreviewLabel = new QLabel(m_replyPreview);
    m_replyPreviewLabel->setStyleSheet("color: #999; font-size: 12px;");
    m_replyPreviewLabel->setWordWrap(true);
    replyLayout->addWidget(m_replyPreviewLabel, 1);
    auto* clearReplyBtn = new QPushButton("×", m_replyPreview);
    clearReplyBtn->setFixedSize(24, 24);
    clearReplyBtn->setStyleSheet("QPushButton { background: transparent; color: #999; border: none; border-radius: 12px; font-size: 18px; } QPushButton:hover { background-color: #3A3A3A; color: white; }");
    clearReplyBtn->setCursor(Qt::PointingHandCursor);
    connect(clearReplyBtn, &QPushButton::clicked, this, &IPMsgWidget::onClearReply);
    replyLayout->addWidget(clearReplyBtn);
    inputLayout->addWidget(m_replyPreview);

    // Message input
    m_messageInput = new QLineEdit(this);
    m_messageInput->setPlaceholderText(tr("输入消息..."));
    m_messageInput->setStyleSheet(
        "QLineEdit { background-color: #2E2E2E; color: #E0E0E0; border: 1px solid #444; border-radius: 20px; "
        "padding: 10px 16px; font-size: 14px; }"
        "QLineEdit:focus { border-color: #07C160; }");
    m_messageInput->setMinimumHeight(44);
    connect(m_messageInput, &QLineEdit::returnPressed, this, &IPMsgWidget::onSendClicked);
    
    // Send typing indicator when user types
    QTimer* typingSendTimer = new QTimer(this);
    typingSendTimer->setSingleShot(true);
    connect(m_messageInput, &QLineEdit::textChanged, this, [this, typingSendTimer]() {
        if (!m_targetIp.isEmpty() && !typingSendTimer->isActive()) {
            m_manager->sendTypingIndicator(m_targetIp);
            typingSendTimer->start(1000); // Limit to once per second
        }
    });
    
    inputLayout->addWidget(m_messageInput);

    rightLayout->addWidget(inputArea);

    mainLayout->addWidget(rightPanel, 1);
}

void IPMsgWidget::addChatMessage(const QString& sender, const QString& message, bool isSelf, const QString& timestamp, const QString& replyTo, const QString& replyContent) {
    // Store in memory for search
    ChatMessage cm;
    cm.sender = sender;
    cm.content = message;
    cm.isSelf = isSelf;
    cm.timestamp = timestamp.isEmpty() ? QDateTime::currentDateTime().toString("HH:mm") : timestamp;
    cm.replyTo = replyTo;
    cm.replyContent = replyContent;
    cm.msgTimestamp = QDateTime::currentMSecsSinceEpoch();
    m_chatMessages.append(cm);

    QString time = cm.timestamp;
    QString avatarBg = getAvatarColor(sender);
    QString avatarLetter = getAvatarLetter(sender);

    // Build reply section if replying to something
    QString replyHtml;
    if (!replyTo.isEmpty() && !replyContent.isEmpty()) {
        QString escapedReply = replyContent.toHtmlEscaped();
        if (escapedReply.length() > 50) escapedReply = escapedReply.left(50) + "...";
        replyHtml = QString(
            "<div style='background-color: rgba(0,0,0,0.08); border-left: 3px solid #07C160; "
            "padding: 4px 10px; margin-bottom: 4px; border-radius: 4px; font-size: 12px; color: #888;'>"
            "回复: %1</div>").arg(escapedReply);
    }

    QString html;
    if (isSelf) {
        html = QString(
            "<div style='margin: 6px 0 6px 60px; text-align: right;'>"
            "<span style='color: #999; font-size: 10px; margin-right: 8px;'>%1</span><br>"
            "<span style='background-color: #95EC69; color: #000; padding: 10px 14px; "
            "border-radius: 12px 12px 2px 12px; display: inline-block; max-width: 65%%; text-align: left; "
            "word-wrap: break-word; box-shadow: 0 1px 2px rgba(0,0,0,0.1);'>%2%3</span>"
            "<span style='display: inline-block; width: 36px; height: 36px; border-radius: 18px; "
            "background-color: %4; color: white; text-align: center; line-height: 36px; "
            "font-weight: 600; font-size: 15px; margin-left: 8px; vertical-align: bottom;'>%5</span>"
            "</div>")
            .arg(time)
            .arg(replyHtml)
            .arg(message.toHtmlEscaped())
            .arg(avatarBg)
            .arg(avatarLetter);
    } else {
        html = QString(
            "<div style='margin: 6px 60px 6px 0;'>"
            "<span style='display: inline-block; width: 36px; height: 36px; border-radius: 18px; "
            "background-color: %1; color: white; text-align: center; line-height: 36px; "
            "font-weight: 600; font-size: 15px; margin-right: 8px; vertical-align: bottom;'>%2</span>"
            "<span style='background-color: #FFFFFF; color: #000; padding: 10px 14px; "
            "border-radius: 12px 12px 12px 2px; display: inline-block; max-width: 65%%; text-align: left; "
            "word-wrap: break-word; box-shadow: 0 1px 2px rgba(0,0,0,0.1);'>%3%4</span>"
            "<br><span style='color: #999; font-size: 10px; margin-left: 44px;'>%5</span>"
            "</div>")
            .arg(avatarBg)
            .arg(avatarLetter)
            .arg(replyHtml)
            .arg(message.toHtmlEscaped())
            .arg(time);
    }

    m_chatDisplay->append(html);
    scrollToBottom();
}

void IPMsgWidget::addFileMessage(const QString& sender, const QString& fileName, qint64 fileSize, bool isSelf) {
    // Store in memory for search
    ChatMessage cm;
    cm.sender = sender;
    cm.content = fileName;
    cm.isSelf = isSelf;
    cm.isFile = true;
    cm.fileName = fileName;
    cm.msgTimestamp = QDateTime::currentMSecsSinceEpoch();
    m_chatMessages.append(cm);

    QString sizeStr;
    if (fileSize < 1024) sizeStr = QString("%1 B").arg(fileSize);
    else if (fileSize < 1024 * 1024) sizeStr = QString("%1 KB").arg(fileSize / 1024);
    else sizeStr = QString("%1 MB").arg(double(fileSize) / (1024.0 * 1024.0), 0, 'f', 1);

    QString time = QDateTime::currentDateTime().toString("HH:mm");
    QString avatarBg = getAvatarColor(sender);
    QString avatarLetter = getAvatarLetter(sender);

    QString html;
    if (isSelf) {
        html = QString(
            "<div style='margin: 6px 0 6px 60px; text-align: right;'>"
            "<span style='color: #999; font-size: 10px; margin-right: 8px;'>%1</span><br>"
            "<span style='background-color: #95EC69; color: #000; padding: 12px 16px; "
            "border-radius: 12px 12px 2px 12px; display: inline-block; max-width: 65%%; text-align: left; "
            "box-shadow: 0 1px 2px rgba(0,0,0,0.1);'>"
            "<span style='font-size: 18px; margin-right: 8px; vertical-align: middle;'>📄</span>"
            "<span style='vertical-align: middle;'><b>%2</b><br>"
            "<span style='color: #666; font-size: 11px;'>%3</span></span></span>"
            "<span style='display: inline-block; width: 36px; height: 36px; border-radius: 18px; "
            "background-color: %4; color: white; text-align: center; line-height: 36px; "
            "font-weight: 600; font-size: 15px; margin-left: 8px; vertical-align: bottom;'>%5</span>"
            "</div>")
            .arg(time, fileName.toHtmlEscaped(), sizeStr, avatarBg, avatarLetter);
    } else {
        html = QString(
            "<div style='margin: 6px 60px 6px 0;'>"
            "<span style='display: inline-block; width: 36px; height: 36px; border-radius: 18px; "
            "background-color: %1; color: white; text-align: center; line-height: 36px; "
            "font-weight: 600; font-size: 15px; margin-right: 8px; vertical-align: bottom;'>%2</span>"
            "<span style='background-color: #FFFFFF; color: #000; padding: 12px 16px; "
            "border-radius: 12px 12px 12px 2px; display: inline-block; max-width: 65%%; text-align: left; "
            "box-shadow: 0 1px 2px rgba(0,0,0,0.1);'>"
            "<span style='font-size: 18px; margin-right: 8px; vertical-align: middle;'>📄</span>"
            "<span style='vertical-align: middle;'><b>%3</b><br>"
            "<span style='color: #666; font-size: 11px;'>%4</span></span></span>"
            "<br><span style='color: #999; font-size: 10px; margin-left: 44px;'>%5</span>"
            "</div>")
            .arg(avatarBg, avatarLetter, fileName.toHtmlEscaped(), sizeStr, time);
    }

    m_chatDisplay->append(html);
    scrollToBottom();
}

void IPMsgWidget::addImageMessage(const QString& sender, const QByteArray& imageData, const QString& fileName, bool isSelf) {
    // Store in memory for search
    ChatMessage cm;
    cm.sender = sender;
    cm.content = QString("[图片: %1]").arg(fileName);
    cm.isSelf = isSelf;
    cm.isImage = true;
    cm.imageData = imageData;
    cm.fileName = fileName;
    cm.msgTimestamp = QDateTime::currentMSecsSinceEpoch();
    m_chatMessages.append(cm);

    QString time = QDateTime::currentDateTime().toString("HH:mm");
    QString avatarBg = getAvatarColor(sender);
    QString avatarLetter = getAvatarLetter(sender);

    QString base64Data = QString::fromLatin1(imageData.toBase64());
    
    // Store image data for viewer
    QString imageId = QString("img_%1_%2").arg(fileName).arg(QDateTime::currentMSecsSinceEpoch());
    m_imageDataMap[imageId] = imageData;

    QString html;
    if (isSelf) {
        html = QString(
            "<div style='margin: 6px 0 6px 60px; text-align: right;'>"
            "<span style='color: #999; font-size: 10px; margin-right: 8px;'>%1</span><br>"
            "<span style='background-color: #95EC69; color: #000; padding: 8px; "
            "border-radius: 12px 12px 2px 12px; display: inline-block; max-width: 65%%; text-align: left; "
            "box-shadow: 0 1px 2px rgba(0,0,0,0.1);'>"
            "<a href='image://%5' style='text-decoration: none;'>"
            "<img src='data:image/png;base64,%2' style='max-width: 220px; max-height: 220px; border-radius: 6px;' />"
            "</a>"
            "<br><span style='color: #666; font-size: 10px;'>%3</span></span>"
            "<span style='display: inline-block; width: 36px; height: 36px; border-radius: 18px; "
            "background-color: %4; color: white; text-align: center; line-height: 36px; "
            "font-weight: 600; font-size: 15px; margin-left: 8px; vertical-align: bottom;'>%6</span>"
            "</div>")
            .arg(time, base64Data, fileName.toHtmlEscaped(), avatarBg, imageId, avatarLetter);
    } else {
        html = QString(
            "<div style='margin: 6px 60px 6px 0;'>"
            "<span style='display: inline-block; width: 36px; height: 36px; border-radius: 18px; "
            "background-color: %1; color: white; text-align: center; line-height: 36px; "
            "font-weight: 600; font-size: 15px; margin-right: 8px; vertical-align: bottom;'>%2</span>"
            "<span style='background-color: #FFFFFF; color: #000; padding: 8px; "
            "border-radius: 12px 12px 12px 2px; display: inline-block; max-width: 65%%; text-align: left; "
            "box-shadow: 0 1px 2px rgba(0,0,0,0.1);'>"
            "<a href='image://%6' style='text-decoration: none;'>"
            "<img src='data:image/png;base64,%3' style='max-width: 220px; max-height: 220px; border-radius: 6px;' />"
            "</a>"
            "<br><span style='color: #666; font-size: 10px;'>%4</span></span>"
            "<br><span style='color: #999; font-size: 10px; margin-left: 44px;'>%5</span>"
            "</div>")
            .arg(avatarBg, avatarLetter, base64Data, fileName.toHtmlEscaped(), time, imageId);
    }

    m_chatDisplay->append(html);
    scrollToBottom();
}

void IPMsgWidget::addChatMessageDirect(const ChatMessage& cm, int msgIndex) {
    QString avatarBg = getAvatarColor(cm.sender);
    QString avatarLetter = getAvatarLetter(cm.sender);
    if (msgIndex < 0) msgIndex = m_chatMessages.size() - 1;

    QString replyHtml;
    if (!cm.replyTo.isEmpty() && !cm.replyContent.isEmpty()) {
        QString escapedReply = cm.replyContent.toHtmlEscaped();
        if (escapedReply.length() > 50) escapedReply = escapedReply.left(50) + "...";
        replyHtml = QString(
            "<div style='background-color: rgba(0,0,0,0.08); border-left: 3px solid #07C160; "
            "padding: 4px 10px; margin-bottom: 4px; border-radius: 4px; font-size: 12px; color: #888;'>"
            "回复: %1</div>").arg(escapedReply);
    }

    // In multi-select mode, wrap message in a clickable select link
    bool isSelected = m_multiSelectMode && m_selectedMessageIndices.contains(msgIndex);
    QString selectPrefix;
    QString selectSuffix;
    if (m_multiSelectMode) {
        QString bgColor = isSelected ? "rgba(7, 193, 96, 0.3)" : "transparent";
        selectPrefix = QString("<a href='select://%1' style='text-decoration: none; display: block; background: %2; border-radius: 8px; padding: 2px;'>").arg(msgIndex).arg(bgColor);
        selectSuffix = "</a>";
    }

    QString html;
    if (cm.isSelf) {
        html = QString(
            "%6"
            "<div style='margin: 6px 0 6px 60px; text-align: right;'>"
            "<span style='color: #999; font-size: 10px; margin-right: 8px;'>%1</span><br>"
            "<span style='background-color: #95EC69; color: #000; padding: 10px 14px; "
            "border-radius: 12px 12px 2px 12px; display: inline-block; max-width: 65%%; text-align: left; "
            "word-wrap: break-word; box-shadow: 0 1px 2px rgba(0,0,0,0.1);'>%2%3</span>"
            "<span style='display: inline-block; width: 36px; height: 36px; border-radius: 18px; "
            "background-color: %4; color: white; text-align: center; line-height: 36px; "
            "font-weight: 600; font-size: 15px; margin-left: 8px; vertical-align: bottom;'>%5</span>"
            "</div>"
            "%7")
            .arg(cm.timestamp, replyHtml, cm.content.toHtmlEscaped(), avatarBg, avatarLetter,
                 selectPrefix, selectSuffix);
    } else {
        html = QString(
            "%6"
            "<div style='margin: 6px 60px 6px 0;'>"
            "<span style='display: inline-block; width: 36px; height: 36px; border-radius: 18px; "
            "background-color: %1; color: white; text-align: center; line-height: 36px; "
            "font-weight: 600; font-size: 15px; margin-right: 8px; vertical-align: bottom;'>%2</span>"
            "<span style='background-color: #FFFFFF; color: #000; padding: 10px 14px; "
            "border-radius: 12px 12px 12px 2px; display: inline-block; max-width: 65%%; text-align: left; "
            "word-wrap: break-word; box-shadow: 0 1px 2px rgba(0,0,0,0.1);'>%3%4</span>"
            "<br><span style='color: #999; font-size: 10px; margin-left: 44px;'>%5</span>"
            "</div>"
            "%7")
            .arg(avatarBg, avatarLetter, replyHtml, cm.content.toHtmlEscaped(), cm.timestamp,
                 selectPrefix, selectSuffix);
    }

    m_chatDisplay->append(html);
}

void IPMsgWidget::addFileMessageDirect(const ChatMessage& cm, int msgIndex) {
    QString sizeStr = "文件";
    QString time = cm.timestamp;
    QString avatarBg = getAvatarColor(cm.sender);
    QString avatarLetter = getAvatarLetter(cm.sender);

    QString html;
    if (cm.isSelf) {
        html = QString(
            "<div style='margin: 6px 0 6px 60px; text-align: right;'>"
            "<span style='color: #999; font-size: 10px; margin-right: 8px;'>%1</span><br>"
            "<span style='background-color: #95EC69; color: #000; padding: 12px 16px; "
            "border-radius: 12px 12px 2px 12px; display: inline-block; max-width: 65%%; text-align: left; "
            "box-shadow: 0 1px 2px rgba(0,0,0,0.1);'>"
            "<span style='font-size: 13px; font-weight: 500;'>%2</span><br>"
            "<span style='font-size: 11px; color: #555;'>%3</span></span>"
            "<span style='display: inline-block; width: 36px; height: 36px; border-radius: 18px; "
            "background-color: %4; color: white; text-align: center; line-height: 36px; "
            "font-weight: 600; font-size: 15px; margin-left: 8px; vertical-align: bottom;'>%5</span>"
            "</div>")
            .arg(time, cm.fileName.toHtmlEscaped(), sizeStr, avatarBg, avatarLetter);
    } else {
        html = QString(
            "<div style='margin: 6px 60px 6px 0;'>"
            "<span style='display: inline-block; width: 36px; height: 36px; border-radius: 18px; "
            "background-color: %1; color: white; text-align: center; line-height: 36px; "
            "font-weight: 600; font-size: 15px; margin-right: 8px; vertical-align: bottom;'>%2</span>"
            "<span style='background-color: #FFFFFF; color: #000; padding: 12px 16px; "
            "border-radius: 12px 12px 12px 2px; display: inline-block; max-width: 65%%; text-align: left; "
            "box-shadow: 0 1px 2px rgba(0,0,0,0.1);'>"
            "<span style='font-size: 13px; font-weight: 500;'>%3</span><br>"
            "<span style='font-size: 11px; color: #666;'>%4</span></span>"
            "<br><span style='color: #999; font-size: 10px; margin-left: 44px;'>%5</span>"
            "</div>")
            .arg(avatarBg, avatarLetter, cm.fileName.toHtmlEscaped(), sizeStr, time);
    }

    m_chatDisplay->append(html);
}

void IPMsgWidget::addImageMessageDirect(const ChatMessage& cm, int msgIndex) {
    QString time = cm.timestamp;
    QString avatarBg = getAvatarColor(cm.sender);
    QString avatarLetter = getAvatarLetter(cm.sender);
    QString base64Data = QString::fromLatin1(cm.imageData.toBase64());
    QString imageId = QString("img_%1").arg(cm.msgTimestamp);

    // Store image data for viewer
    m_imageDataMap[imageId] = cm.imageData;

    QString html;
    if (cm.isSelf) {
        html = QString(
            "<div style='margin: 6px 0 6px 60px; text-align: right;'>"
            "<span style='color: #999; font-size: 10px; margin-right: 8px;'>%1</span><br>"
            "<span style='background-color: #95EC69; color: #000; padding: 8px; "
            "border-radius: 12px 12px 2px 12px; display: inline-block; max-width: 65%%; text-align: left; "
            "box-shadow: 0 1px 2px rgba(0,0,0,0.1);'>"
            "<a href='image://%5' style='text-decoration: none;'>"
            "<img src='data:image/png;base64,%2' style='max-width: 220px; max-height: 220px; border-radius: 6px;' />"
            "</a>"
            "<br><span style='color: #666; font-size: 10px;'>%3</span></span>"
            "<span style='display: inline-block; width: 36px; height: 36px; border-radius: 18px; "
            "background-color: %4; color: white; text-align: center; line-height: 36px; "
            "font-weight: 600; font-size: 15px; margin-left: 8px; vertical-align: bottom;'>%6</span>"
            "</div>")
            .arg(time, base64Data, cm.fileName.toHtmlEscaped(), avatarBg, imageId, avatarLetter);
    } else {
        html = QString(
            "<div style='margin: 6px 60px 6px 0;'>"
            "<span style='display: inline-block; width: 36px; height: 36px; border-radius: 18px; "
            "background-color: %1; color: white; text-align: center; line-height: 36px; "
            "font-weight: 600; font-size: 15px; margin-right: 8px; vertical-align: bottom;'>%2</span>"
            "<span style='background-color: #FFFFFF; color: #000; padding: 8px; "
            "border-radius: 12px 12px 12px 2px; display: inline-block; max-width: 65%%; text-align: left; "
            "box-shadow: 0 1px 2px rgba(0,0,0,0.1);'>"
            "<a href='image://%6' style='text-decoration: none;'>"
            "<img src='data:image/png;base64,%3' style='max-width: 220px; max-height: 220px; border-radius: 6px;' />"
            "</a>"
            "<br><span style='color: #666; font-size: 10px;'>%4</span></span>"
            "<br><span style='color: #999; font-size: 10px; margin-left: 44px;'>%5</span>"
            "</div>")
            .arg(avatarBg, avatarLetter, base64Data, cm.fileName.toHtmlEscaped(), time, imageId);
    }

    m_chatDisplay->append(html);
}

void IPMsgWidget::updateContactList() {
    if (!m_manager) return;

    QString ownUserId = m_manager->userId();
    auto devices = m_manager->getOnlineDevices();

    QSet<QString> onlineIps;
    QMap<QString, IPMsgDevice> deviceMap;
    for (const auto& device : devices) {
        if (device.id == ownUserId) continue;
        onlineIps.insert(device.ip);
        deviceMap[device.ip] = device;
    }

    // Update contact map
    for (const QString& ip : onlineIps) {
        if (!m_contacts.contains(ip)) {
            m_contacts[ip] = {ip, deviceMap[ip].name, deviceMap[ip].id, true};
        } else {
            m_contacts[ip].name = deviceMap[ip].name;
            m_contacts[ip].online = true;
        }
    }
    for (auto it = m_contacts.begin(); it != m_contacts.end(); ) {
        if (!onlineIps.contains(it.key())) {
            it->online = false;
            ++it;
        } else {
            ++it;
        }
    }

    // Clear and rebuild contact list
    QLayoutItem* item;
    while ((item = m_contactListLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    // Show recent chats first
    for (const RecentChat& chat : m_recentChats) {
        if (!m_contacts.contains(chat.ip) || !m_contacts[chat.ip].online) continue;
        if (!m_searchFilter.isEmpty()) {
            if (!chat.name.toLower().contains(m_searchFilter) && 
                !chat.ip.toLower().contains(m_searchFilter)) {
                continue;
            }
        }
        
        const ContactInfo& info = m_contacts[chat.ip];
        auto* contactWidget = new QWidget();
        contactWidget->setFixedHeight(72);
        contactWidget->setProperty("contact_ip", info.ip);

        bool isSelected = (info.ip == m_targetIp);
        contactWidget->setStyleSheet(isSelected ?
            "QWidget { background-color: #3C3C3C; border-left: 3px solid #07C160; }" :
            "QWidget { background-color: #2E2E2E; border-left: 3px solid transparent; }"
            "QWidget:hover { background-color: #363636; }");

        auto* contactLayout = new QHBoxLayout(contactWidget);
        contactLayout->setContentsMargins(12, 8, 12, 8);
        contactLayout->setSpacing(12);

        // Avatar with online indicator
        auto* avatarContainer = new QWidget(contactWidget);
        avatarContainer->setFixedSize(48, 48);
        auto* avatarLayout = new QVBoxLayout(avatarContainer);
        avatarLayout->setContentsMargins(0, 0, 0, 0);
        avatarLayout->setAlignment(Qt::AlignCenter);

        auto* avatar = new QLabel(avatarContainer);
        avatar->setFixedSize(42, 42);
        avatar->setStyleSheet(QString(
            "background-color: %1; color: white; border-radius: 21px; font-weight: bold; font-size: 18px;")
            .arg(getAvatarColor(info.name)));
        avatar->setAlignment(Qt::AlignCenter);
        avatar->setText(getAvatarLetter(info.name));
        avatarLayout->addWidget(avatar);

        // Online indicator dot
        auto* onlineDot = new QLabel(avatarContainer);
        onlineDot->setFixedSize(12, 12);
        onlineDot->setStyleSheet("background-color: #07C160; border-radius: 6px; border: 2px solid #2E2E2E;");
        onlineDot->move(33, 33);
        onlineDot->raise();
        contactLayout->addWidget(avatarContainer);

        // Name and last message
        auto* textLayout = new QVBoxLayout();
        textLayout->setSpacing(2);

        auto* nameLabel = new QLabel(info.name, contactWidget);
        nameLabel->setStyleSheet("color: #E0E0E0; font-size: 14px; font-weight: 500;");
        textLayout->addWidget(nameLabel);

        QString lastMsg = chat.lastMessage;
        if (lastMsg.length() > 20) lastMsg = lastMsg.left(20) + "...";
        auto* lastMsgLabel = new QLabel(lastMsg, contactWidget);
        lastMsgLabel->setStyleSheet("color: #999; font-size: 11px;");
        textLayout->addWidget(lastMsgLabel);

        contactLayout->addLayout(textLayout, 1);

        // Unread count badge
        if (chat.unreadCount > 0) {
            auto* badge = new QLabel(QString::number(chat.unreadCount), contactWidget);
            badge->setFixedSize(20, 20);
            badge->setStyleSheet("background-color: #FA5151; color: white; border-radius: 10px; font-size: 11px; font-weight: bold;");
            badge->setAlignment(Qt::AlignCenter);
            contactLayout->addWidget(badge);
        }

        m_contactListLayout->addWidget(contactWidget);

        // Click and right-click detection
        struct ContactEventFilter : public QObject {
            IPMsgWidget* w;
            QString ip;
            QString deviceId;
            ContactEventFilter(IPMsgWidget* w, const QString& ip, const QString& deviceId, QObject* parent) 
                : QObject(parent), w(w), ip(ip), deviceId(deviceId) {}
            bool eventFilter(QObject* obj, QEvent* event) override {
                if (event->type() == QEvent::MouseButtonRelease) {
                    QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
                    if (mouseEvent->button() == Qt::LeftButton) {
                        w->onContactClicked(ip);
                        return true;
                    } else if (mouseEvent->button() == Qt::RightButton) {
                        QMenu* menu = new QMenu(w);
                        menu->setStyleSheet("QMenu { background-color: #3C3C3C; color: white; border: 1px solid #555; }"
                                           "QMenu::item:selected { background-color: #07C160; }");
                        QAction* detailsAction = menu->addAction(w->tr("查看详情"));
                        QObject::connect(detailsAction, &QAction::triggered, w, [this]() {
                            w->showContactDetails(ip);
                        });
                        
                        // Add friend request option if not already friends
                        if (!w->m_manager->isFriend(deviceId)) {
                            QAction* friendAction = menu->addAction(w->tr("添加好友"));
                            QObject::connect(friendAction, &QAction::triggered, w, [this]() {
                                w->sendFriendRequestToContact(ip);
                            });
                        }
                        
                        // Add DND toggle
                        QAction* dndAction = menu->addAction(w->m_contacts[ip].dnd ? w->tr("取消免打扰") : w->tr("设为免打扰"));
                        QObject::connect(dndAction, &QAction::triggered, w, [this]() {
                            w->m_contacts[ip].dnd = !w->m_contacts[ip].dnd;
                            w->updateContactList();
                        });
                        
                        menu->exec(mouseEvent->globalPosition().toPoint());
                        return true;
                    }
                }
                return false;
            }
        };
        auto* filter = new ContactEventFilter(this, info.ip, info.deviceId, contactWidget);
        contactWidget->installEventFilter(filter);
    }

    // Then show other online contacts not in recent chats
    for (auto it = m_contacts.constBegin(); it != m_contacts.constEnd(); ++it) {
        const ContactInfo& info = it.value();
        if (!info.online) continue;
        
        // Skip if already shown in recent chats
        bool isRecent = false;
        for (const RecentChat& chat : m_recentChats) {
            if (chat.ip == info.ip) {
                isRecent = true;
                break;
            }
        }
        if (isRecent) continue;
        
        // Apply search filter
        if (!m_searchFilter.isEmpty()) {
            if (!info.name.toLower().contains(m_searchFilter) && 
                !info.ip.toLower().contains(m_searchFilter)) {
                continue;
            }
        }

        auto* contactWidget = new QWidget();
        contactWidget->setFixedHeight(72);
        contactWidget->setProperty("contact_ip", info.ip);

        bool isSelected = (info.ip == m_targetIp);
        contactWidget->setStyleSheet(isSelected ?
            "QWidget { background-color: #3C3C3C; border-left: 3px solid #07C160; }" :
            "QWidget { background-color: #2E2E2E; border-left: 3px solid transparent; }"
            "QWidget:hover { background-color: #363636; }");

        auto* contactLayout = new QHBoxLayout(contactWidget);
        contactLayout->setContentsMargins(12, 8, 12, 8);
        contactLayout->setSpacing(12);

        // Avatar with online indicator
        auto* avatarContainer = new QWidget(contactWidget);
        avatarContainer->setFixedSize(48, 48);
        auto* avatarLayout = new QVBoxLayout(avatarContainer);
        avatarLayout->setContentsMargins(0, 0, 0, 0);
        avatarLayout->setAlignment(Qt::AlignCenter);

        auto* avatar = new QLabel(avatarContainer);
        avatar->setFixedSize(42, 42);
        avatar->setStyleSheet(QString(
            "background-color: %1; color: white; border-radius: 21px; font-weight: bold; font-size: 18px;")
            .arg(getAvatarColor(info.name)));
        avatar->setAlignment(Qt::AlignCenter);
        avatar->setText(getAvatarLetter(info.name));
        avatarLayout->addWidget(avatar);

        // Online indicator dot
        auto* onlineDot = new QLabel(avatarContainer);
        onlineDot->setFixedSize(12, 12);
        onlineDot->setStyleSheet("background-color: #07C160; border-radius: 6px; border: 2px solid #2E2E2E;");
        onlineDot->move(33, 33);
        onlineDot->raise();
        contactLayout->addWidget(avatarContainer);

        // Name and status
        auto* textLayout = new QVBoxLayout();
        textLayout->setSpacing(2);

        auto* nameLabel = new QLabel(info.name, contactWidget);
        nameLabel->setStyleSheet("color: #E0E0E0; font-size: 14px; font-weight: 500;");
        textLayout->addWidget(nameLabel);

        auto* statusLabel = new QLabel(tr("在线"), contactWidget);
        statusLabel->setStyleSheet("color: #07C160; font-size: 11px;");
        textLayout->addWidget(statusLabel);

        contactLayout->addLayout(textLayout, 1);

        m_contactListLayout->addWidget(contactWidget);

        // Click and right-click detection
        struct ContactEventFilter : public QObject {
            IPMsgWidget* w;
            QString ip;
            QString deviceId;
            ContactEventFilter(IPMsgWidget* w, const QString& ip, const QString& deviceId, QObject* parent) 
                : QObject(parent), w(w), ip(ip), deviceId(deviceId) {}
            bool eventFilter(QObject* obj, QEvent* event) override {
                if (event->type() == QEvent::MouseButtonRelease) {
                    QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
                    if (mouseEvent->button() == Qt::LeftButton) {
                        w->onContactClicked(ip);
                        return true;
                    } else if (mouseEvent->button() == Qt::RightButton) {
                        QMenu* menu = new QMenu(w);
                        menu->setStyleSheet("QMenu { background-color: #3C3C3C; color: white; border: 1px solid #555; }"
                                           "QMenu::item:selected { background-color: #07C160; }");
                        QAction* detailsAction = menu->addAction(w->tr("查看详情"));
                        QObject::connect(detailsAction, &QAction::triggered, w, [this]() {
                            w->showContactDetails(ip);
                        });
                        
                        // Add friend request option if not already friends
                        if (!w->m_manager->isFriend(deviceId)) {
                            QAction* friendAction = menu->addAction(w->tr("添加好友"));
                            QObject::connect(friendAction, &QAction::triggered, w, [this]() {
                                w->sendFriendRequestToContact(ip);
                            });
                        }
                        
                        // Add DND toggle
                        QAction* dndAction = menu->addAction(w->m_contacts[ip].dnd ? w->tr("取消免打扰") : w->tr("设为免打扰"));
                        QObject::connect(dndAction, &QAction::triggered, w, [this]() {
                            w->m_contacts[ip].dnd = !w->m_contacts[ip].dnd;
                            w->updateContactList();
                        });
                        
                        menu->exec(mouseEvent->globalPosition().toPoint());
                        return true;
                    }
                }
                return false;
            }
        };
        auto* filter = new ContactEventFilter(this, info.ip, info.deviceId, contactWidget);
        contactWidget->installEventFilter(filter);
    }

    m_contactListLayout->addStretch();
}

void IPMsgWidget::onContactClicked(const QString& ip) {
    selectContact(ip);
    onNotificationClicked();
}

void IPMsgWidget::onNotificationClicked() {
    // Clear notification from window title
    if (QWidget* parent = parentWidget()) {
        if (QDialog* dialog = qobject_cast<QDialog*>(parent)) {
            QString currentTitle = dialog->windowTitle();
            if (currentTitle.contains("● ")) {
                dialog->setWindowTitle(currentTitle.mid(2));
            }
        }
    }
}

void IPMsgWidget::onCreateGroup() {
    if (m_contacts.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("没有在线联系人"));
        return;
    }
    
    // Create group dialog
    QDialog dialog(this);
    dialog.setWindowTitle(tr("创建群聊"));
    dialog.setMinimumSize(300, 400);
    dialog.setStyleSheet("QDialog { background-color: #2E2E2E; }");
    
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    
    QLabel* nameLabel = new QLabel(tr("群聊名称:"), &dialog);
    nameLabel->setStyleSheet("color: white;");
    layout->addWidget(nameLabel);
    
    QLineEdit* nameEdit = new QLineEdit(&dialog);
    nameEdit->setStyleSheet("QLineEdit { background-color: #3C3C3C; color: white; border: none; border-radius: 4px; padding: 8px; }");
    nameEdit->setPlaceholderText(tr("输入群聊名称"));
    layout->addWidget(nameEdit);
    
    QLabel* memberLabel = new QLabel(tr("选择成员:"), &dialog);
    memberLabel->setStyleSheet("color: white;");
    layout->addWidget(memberLabel);
    
    // Member selection
    QList<QCheckBox*> memberCheckBoxes;
    for (auto it = m_contacts.constBegin(); it != m_contacts.constEnd(); ++it) {
        if (!it.value().online) continue;
        
        QCheckBox* checkBox = new QCheckBox(it.value().name, &dialog);
        checkBox->setStyleSheet("QCheckBox { color: white; }");
        checkBox->setProperty("memberId", it.value().deviceId);
        checkBox->setProperty("memberIp", it.key());
        memberCheckBoxes.append(checkBox);
        layout->addWidget(checkBox);
    }
    
    layout->addStretch();
    
    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    QPushButton* cancelBtn = new QPushButton(tr("取消"), &dialog);
    cancelBtn->setStyleSheet("QPushButton { background-color: #3C3C3C; color: white; border: none; border-radius: 4px; padding: 8px 16px; }");
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    buttonLayout->addWidget(cancelBtn);
    
    QPushButton* createBtn = new QPushButton(tr("创建"), &dialog);
    createBtn->setStyleSheet("QPushButton { background-color: #07C160; color: white; border: none; border-radius: 4px; padding: 8px 16px; }");
    connect(createBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttonLayout->addWidget(createBtn);
    
    layout->addLayout(buttonLayout);
    
    if (dialog.exec() == QDialog::Accepted) {
        QString groupName = nameEdit->text().trimmed();
        if (groupName.isEmpty()) {
            QMessageBox::warning(this, tr("提示"), tr("请输入群聊名称"));
            return;
        }
        
        QList<QString> memberIds;
        for (QCheckBox* checkBox : memberCheckBoxes) {
            if (checkBox->isChecked()) {
                memberIds.append(checkBox->property("memberId").toString());
            }
        }
        
        if (memberIds.isEmpty()) {
            QMessageBox::warning(this, tr("提示"), tr("请选择至少一个成员"));
            return;
        }
        
        QString groupId = m_manager->createGroup(groupName, memberIds);
        
        // Add group to list
        IPMsgGroup group;
        group.id = groupId;
        group.name = groupName;
        group.memberIds = memberIds;
        group.createdAt = QDateTime::currentMSecsSinceEpoch();
        m_groups.append(group);
        
        // Select the new group
        onGroupClicked(groupId);
        
        QMessageBox::information(this, tr("成功"), tr("群聊 '%1' 已创建").arg(groupName));
    }
}

void IPMsgWidget::onGroupClicked(const QString& groupId) {
    // Find group
    for (const IPMsgGroup& group : m_groups) {
        if (group.id == groupId) {
            m_targetGroupId = groupId;
            m_targetIp.clear(); // Clear direct IP when in group mode
            m_targetName = group.name;
            m_chatTitleLabel->setText(QString("👥 %1").arg(group.name));
            m_groupSettingsBtn->setVisible(true);
            m_statsBtn->setVisible(true);
            m_memberManagementBtn->setVisible(true);
            
            // Update statistics widget
            if (m_groupStatsWidget) {
                m_groupStatsWidget->setGroup(group);
                m_groupStatsWidget->startAnimation();
            }
            
            // Update member management widget
            if (m_memberManagementWidget) {
                m_memberManagementWidget->setGroup(group);
            }
            
            // Clear chat and show group info
            m_chatDisplay->clear();
            m_chatMessages.clear();
            m_searchMatchIndices.clear();
            m_currentMatchIndex = -1;
            if (m_chatSearchWidget) {
                m_chatSearchWidget->deactivate();
            }
            addChatMessage("", tr("群聊 '%1' 已创建，成员: %2")
                .arg(group.name, group.memberNames.join(", ")), false);

            // Show pinned announcement at top
            DatabaseManager* db = &DatabaseManager::instance();
            QString announcement = db->getSetting("group_announcement_" + groupId).toString();
            if (!announcement.isEmpty()) {
                QString announceHtml = QString(
                    "<div style='margin: 8px 16px; padding: 10px 14px; background: linear-gradient(135deg, #1a3a5c, #1a2a3c); "
                    "border-radius: 8px; border-left: 4px solid #1E88E5;'>"
                    "<span style='color: #1E88E5; font-weight: bold; font-size: 12px;'>📢 群公告</span><br>"
                    "<span style='color: #E0E0E0; font-size: 13px;'>%1</span>"
                    "</div>").arg(announcement.toHtmlEscaped());
                m_chatDisplay->append(announceHtml);
            }

            m_messageInput->setFocus();
            break;
        }
    }
}

void IPMsgWidget::exportChatHistory() {
    if (m_targetIp.isEmpty() && m_targetGroupId.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择联系人或群聊"));
        return;
    }
    
    QString fileName = QFileDialog::getSaveFileName(this, tr("导出聊天记录"), 
        QString("%1_chat_history.html").arg(m_targetName), tr("HTML Files (*.html);;Text Files (*.txt)"));
    
    if (fileName.isEmpty()) return;
    
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("错误"), tr("无法创建文件"));
        return;
    }
    
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    
    if (fileName.endsWith(".html", Qt::CaseInsensitive)) {
        // HTML format
        out << "<!DOCTYPE html>\n<html>\n<head>\n";
        out << "<meta charset=\"UTF-8\">\n";
        out << "<title>" << m_targetName << " - 聊天记录</title>\n";
        out << "<style>\n";
        out << "body { font-family: 'Microsoft YaHei', sans-serif; background: #f5f5f5; padding: 20px; }\n";
        out << ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }\n";
        out << ".header { border-bottom: 1px solid #eee; padding-bottom: 15px; margin-bottom: 20px; }\n";
        out << ".title { font-size: 24px; color: #333; margin: 0; }\n";
        out << ".meta { color: #999; font-size: 14px; margin-top: 5px; }\n";
        out << ".message { margin: 15px 0; padding: 10px; border-radius: 8px; }\n";
        out << ".message.self { background: #07C160; color: white; text-align: right; }\n";
        out << ".message.other { background: #f0f0f0; color: #333; }\n";
        out << ".sender { font-weight: bold; margin-bottom: 5px; }\n";
        out << ".content { white-space: pre-wrap; }\n";
        out << ".time { font-size: 12px; color: #999; margin-top: 5px; }\n";
        out << "img { max-width: 300px; border-radius: 4px; }\n";
        out << "</style>\n</head>\n<body>\n<div class=\"container\">\n";
        out << "<div class=\"header\"><h1 class=\"title\">" << m_targetName << "</h1>";
        out << "<div class=\"meta\">导出时间: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "</div></div>\n";
        
        // Get chat display HTML
        QString html = m_chatDisplay->toHtml();
        out << html;
        
        out << "</div></body></html>";
    } else {
        // Plain text format
        out << "聊天记录: " << m_targetName << "\n";
        out << "导出时间: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";
        out << QString(50, '=') << "\n\n";
        
        QString text = m_chatDisplay->toPlainText();
        out << text;
    }
    
    file.close();
    QMessageBox::information(this, tr("成功"), tr("聊天记录已导出到:\n%1").arg(fileName));
}

void IPMsgWidget::onMultiSelectToggled(bool checked) {
    m_multiSelectMode = checked;
    m_selectedMessageIndices.clear();
    
    if (checked) {
        m_multiSelectBtn->setText(tr("✕"));
        m_multiSelectBtn->setToolTip(tr("退出多选"));
        
        // Show batch action bar
        showBatchActionBar();
    } else {
        m_multiSelectBtn->setText(tr("☑"));
        m_multiSelectBtn->setToolTip(tr("多选模式"));
        hideBatchActionBar();
    }
    
    // Refresh chat display to show checkboxes
    refreshChatDisplay();
}

void IPMsgWidget::showBatchActionBar() {
    if (m_batchActionBar) return;
    
    m_batchActionBar = new QWidget(this);
    m_batchActionBar->setFixedHeight(50);
    m_batchActionBar->setStyleSheet("QWidget { background-color: #2E2E2E; border-top: 1px solid #404040; }");
    auto* layout = new QHBoxLayout(m_batchActionBar);
    layout->setContentsMargins(10, 5, 10, 5);
    
    m_batchCountLabel = new QLabel(tr("已选择 0 条"), m_batchActionBar);
    m_batchCountLabel->setStyleSheet("color: #CCCCCC; font-size: 13px;");
    layout->addWidget(m_batchCountLabel);
    
    layout->addStretch();
    
    auto addBatchBtn = [&](const QString& text, std::function<void()> slot) {
        QPushButton* btn = new QPushButton(text, m_batchActionBar);
        btn->setStyleSheet("QPushButton { background-color: #404040; color: white; border: none; border-radius: 4px; padding: 6px 12px; }"
                          "QPushButton:hover { background-color: #505050; }");
        connect(btn, &QPushButton::clicked, this, slot);
        layout->addWidget(btn);
    };
    
    addBatchBtn(tr("删除"), [this]() { onBatchDelete(); });
    addBatchBtn(tr("转发"), [this]() { onBatchForward(); });
    addBatchBtn(tr("复制"), [this]() { onBatchCopy(); });
    addBatchBtn(tr("全选"), [this]() {
        m_selectedMessageIndices.clear();
        for (int i = 0; i < m_chatMessages.size(); ++i) {
            m_selectedMessageIndices.append(i);
        }
        m_batchCountLabel->setText(tr("已选择 %1 条").arg(m_selectedMessageIndices.size()));
        refreshChatDisplay();
    });
    
    QPushButton* cancelBtn = new QPushButton(tr("取消"), m_batchActionBar);
    cancelBtn->setStyleSheet("QPushButton { background-color: #FA5151; color: white; border: none; border-radius: 4px; padding: 6px 12px; }");
    connect(cancelBtn, &QPushButton::clicked, this, [this]() {
        m_multiSelectBtn->setChecked(false);
    });
    layout->addWidget(cancelBtn);
    
    // Find the right layout and insert batch action bar
    QWidget* centralWidget = nullptr;
    for (QWidget* w : findChildren<QWidget*>()) {
        if (w->layout() && w->layout()->count() > 1) {
            centralWidget = w;
            break;
        }
    }
    if (centralWidget) {
        QVBoxLayout* rightLayout = qobject_cast<QVBoxLayout*>(centralWidget->layout());
        if (rightLayout) {
            rightLayout->insertWidget(rightLayout->count() - 1, m_batchActionBar);
        }
    }
}

void IPMsgWidget::hideBatchActionBar() {
    if (m_batchActionBar) {
        m_batchActionBar->deleteLater();
        m_batchActionBar = nullptr;
        m_batchCountLabel = nullptr;
    }
}

void IPMsgWidget::refreshChatDisplay() {
    // Store scroll position
    QScrollBar* scrollBar = m_chatDisplay->verticalScrollBar();
    int scrollPos = scrollBar->value();

    // Rebuild chat display from m_chatMessages
    m_chatDisplay->clear();

    // Track which message indices are search matches
    QSet<int> matchSet;
    for (int idx : m_searchMatchIndices) {
        matchSet.insert(idx);
    }

    QString keyword;
    QLineEdit* searchInput = m_chatSearchWidget ? m_chatSearchWidget->findChild<QLineEdit*>() : nullptr;
    if (searchInput) keyword = searchInput->text();

    for (int i = 0; i < m_chatMessages.size(); ++i) {
        const ChatMessage& cm = m_chatMessages[i];

        if (cm.isImage) {
            // Re-add image message
            addImageMessageDirect(cm, i);
        } else if (cm.isFile) {
            // Re-add file message
            addFileMessageDirect(cm, i);
        } else {
            // Re-add text message
            addChatMessageDirect(cm, i);
        }
    }

    // Restore scroll position
    scrollBar->setValue(scrollPos);

    // Now use QTextBrowser::find to highlight matches
    if (!keyword.isEmpty() && !m_searchMatchIndices.isEmpty()) {
        // Highlight all matches using find()
        QTextCursor cursor = m_chatDisplay->textCursor();
        cursor.movePosition(QTextCursor::Start);
        m_chatDisplay->setTextCursor(cursor);

        QTextDocument::FindFlags flags;
        bool found = true;
        while (found) {
            found = m_chatDisplay->find(keyword, flags);
            if (!found) break;
        }

        // Move to first match
        cursor.movePosition(QTextCursor::Start);
        m_chatDisplay->setTextCursor(cursor);
        if (!m_searchMatchIndices.isEmpty()) {
            navigateSearchMatch(true);
        }
    }
}

void IPMsgWidget::onBatchDelete() {
    if (m_selectedMessageIndices.isEmpty()) return;

    QMessageBox::StandardButton reply = QMessageBox::question(this,
        tr("批量删除"), tr("确定要删除选中的 %1 条消息吗？").arg(m_selectedMessageIndices.size()),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        // Sort indices descending to remove from end first
        QList<int> sortedIndices = m_selectedMessageIndices;
        std::sort(sortedIndices.begin(), sortedIndices.end(), std::greater<int>());
        for (int idx : sortedIndices) {
            if (idx >= 0 && idx < m_chatMessages.size()) {
                m_chatMessages.removeAt(idx);
            }
        }
        m_selectedMessageIndices.clear();
        m_multiSelectBtn->setChecked(false);
        refreshChatDisplay();
    }
}

void IPMsgWidget::onBatchForward() {
    if (m_selectedMessageIndices.isEmpty()) return;

    // Get selected messages from memory
    QStringList messages;
    for (int idx : m_selectedMessageIndices) {
        if (idx >= 0 && idx < m_chatMessages.size()) {
            const ChatMessage& cm = m_chatMessages[idx];
            messages.append(QString("[%1] %2: %3").arg(cm.timestamp, cm.sender, cm.content));
        }
    }

    if (messages.isEmpty()) return;

    QString forwardText = messages.join("\n");

    // Show contact selection dialog
    QStringList contacts;
    for (auto it = m_contacts.constBegin(); it != m_contacts.constEnd(); ++it) {
        if (it.value().online) {
            contacts.append(it.value().name);
        }
    }

    for (const IPMsgGroup& group : m_groups) {
        contacts.append("👥 " + group.name);
    }
    
    bool ok;
    QString selected = QInputDialog::getItem(this, tr("转发消息"), tr("选择转发对象"), contacts, 0, false, &ok);
    
    if (ok && !selected.isEmpty()) {
        if (selected.startsWith("👥 ")) {
            QString groupId = selected.mid(3);
            for (const IPMsgGroup& group : m_groups) {
                if (group.name == groupId) {
                    m_manager->sendGroupMessage(group.id, "[转发] " + forwardText);
                    break;
                }
            }
        } else {
            for (auto it = m_contacts.constBegin(); it != m_contacts.constEnd(); ++it) {
                if (it.value().name == selected) {
                    m_manager->sendMessage(it.key(), "[转发] " + forwardText);
                    break;
                }
            }
        }
        QMessageBox::information(this, tr("成功"), tr("已转发给 %1").arg(selected));
        m_multiSelectBtn->setChecked(false);
    }
}

void IPMsgWidget::onBatchCopy() {
    if (m_selectedMessageIndices.isEmpty()) return;

    QStringList selectedMessages;
    for (int idx : m_selectedMessageIndices) {
        if (idx >= 0 && idx < m_chatMessages.size()) {
            const ChatMessage& cm = m_chatMessages[idx];
            selectedMessages.append(QString("[%1] %2: %3").arg(cm.timestamp, cm.sender, cm.content));
        }
    }

    if (!selectedMessages.isEmpty()) {
        QApplication::clipboard()->setText(selectedMessages.join("\n"));
        QMessageBox::information(this, tr("成功"), tr("已复制 %1 条消息到剪贴板").arg(selectedMessages.size()));
        m_multiSelectBtn->setChecked(false);
    }
}

void IPMsgWidget::cleanupOldChatHistory() {
    qint64 cutoffTime = QDateTime::currentMSecsSinceEpoch() - (static_cast<qint64>(m_maxChatHistoryDays) * 24 * 60 * 60 * 1000);

    DatabaseManager& db = DatabaseManager::instance();
    if (db.initialize()) {
        QSqlQuery query(db.database());  // Access the database via public method
        query.prepare("DELETE FROM messages WHERE timestamp < ?");
        query.addBindValue(cutoffTime);
        if (query.exec()) {
            int deletedCount = query.numRowsAffected();
            if (m_statusLabel) {
                m_statusLabel->setText(tr("聊天记录清理完成，删除了 %1 条超过 %2 天的记录").arg(deletedCount).arg(m_maxChatHistoryDays));
                QTimer::singleShot(3000, [this]() {
                    if (m_statusLabel) m_statusLabel->clear();
                });
            }
        } else {
            if (m_statusLabel) {
                m_statusLabel->setText(tr("聊天记录清理失败: %1").arg(query.lastError().text()));
                QTimer::singleShot(3000, [this]() {
                    if (m_statusLabel) m_statusLabel->clear();
                });
            }
        }
    } else {
        if (m_statusLabel) {
            m_statusLabel->setText(tr("数据库未初始化，无法清理聊天记录"));
            QTimer::singleShot(3000, [this]() {
                if (m_statusLabel) m_statusLabel->clear();
            });
        }
    }
}

void IPMsgWidget::showCleanupSettings() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("聊天记录清理设置"));
    dialog.setMinimumSize(350, 250);
    dialog.setStyleSheet("QDialog { background-color: #2E2E2E; }");
    
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    
    QLabel* infoLabel = new QLabel(tr("自动清理超过指定天数的聊天记录"), &dialog);
    infoLabel->setStyleSheet("color: #CCCCCC; font-size: 13px; margin-bottom: 10px;");
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);
    
    QLabel* daysLabel = new QLabel(tr("保留天数:"), &dialog);
    daysLabel->setStyleSheet("color: white;");
    layout->addWidget(daysLabel);
    
    QSpinBox* daysSpinBox = new QSpinBox(&dialog);
    daysSpinBox->setRange(1, 365);
    daysSpinBox->setValue(m_maxChatHistoryDays);
    daysSpinBox->setStyleSheet("QSpinBox { background-color: #3C3C3C; color: white; border: none; border-radius: 4px; padding: 8px; }");
    layout->addWidget(daysSpinBox);
    
    QCheckBox* enableCheckBox = new QCheckBox(tr("启用自动清理"), &dialog);
    enableCheckBox->setChecked(m_cleanupTimer && m_cleanupTimer->isActive());
    enableCheckBox->setStyleSheet("QCheckBox { color: white; }"
                                  "QCheckBox::indicator { width: 18px; height: 18px; }");
    layout->addWidget(enableCheckBox);
    
    QPushButton* cleanupNowBtn = new QPushButton(tr("立即清理"), &dialog);
    cleanupNowBtn->setStyleSheet("QPushButton { background-color: #FA5151; color: white; border: none; border-radius: 4px; padding: 8px 16px; }");
    connect(cleanupNowBtn, &QPushButton::clicked, &dialog, [this, daysSpinBox, enableCheckBox, &dialog]() {
        m_maxChatHistoryDays = daysSpinBox->value();
        cleanupOldChatHistory();
        if (enableCheckBox->isChecked()) {
            m_cleanupTimer->start(24 * 60 * 60 * 1000);
        } else {
            m_cleanupTimer->stop();
        }
        dialog.accept();
    });
    layout->addWidget(cleanupNowBtn);
    
    layout->addStretch();
    
    QPushButton* closeBtn = new QPushButton(tr("关闭"), &dialog);
    closeBtn->setStyleSheet("QPushButton { background-color: #404040; color: white; border: none; border-radius: 4px; padding: 8px 16px; }");
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(closeBtn, 0, Qt::AlignRight);
    
    dialog.exec();
}

// ────────── Voice/Video/Location/Card Messages ──────────

void IPMsgWidget::onVoiceMessageReceived(const IPMsgMessage& message) {
    // Rich voice message card
    QString voiceHtml = QString(
        "<div style='margin: 6px 60px 6px 0; padding: 12px 16px; background: linear-gradient(135deg, #1a3a2c, #1a2a1c); "
        "border-radius: 12px; border-left: 4px solid #07C160; cursor: pointer;'>"
        "<span style='color: #07C160; font-weight: bold; font-size: 13px;'>🎤 语音消息</span><br>"
        "<span style='color: #E0E0E0; font-size: 12px;'>时长: %1 秒</span><br>"
        "<span style='color: #888; font-size: 11px;'>点击播放</span>"
        "</div>")
        .arg(message.voiceDuration);
    m_chatDisplay->append(voiceHtml);
    updateRecentChats(message.senderId, message.senderName, "[语音消息]");

    if (!m_contacts[message.senderId].dnd) {
        QApplication::beep();
    }
}

void IPMsgWidget::onVideoMessageReceived(const IPMsgMessage& message) {
    // Rich video message card
    QString videoHtml = QString(
        "<div style='margin: 6px 60px 6px 0; padding: 12px 16px; background: linear-gradient(135deg, #2a1a3c, #1a1a2c); "
        "border-radius: 12px; border-left: 4px solid #8E24AA; cursor: pointer;'>"
        "<span style='color: #8E24AA; font-weight: bold; font-size: 13px;'>🎬 视频消息</span><br>"
        "<span style='color: #E0E0E0; font-size: 12px;'>时长: %1 秒 | 分辨率: %2x%3</span><br>"
        "<span style='color: #888; font-size: 11px;'>点击播放</span>"
        "</div>")
        .arg(message.videoDuration).arg(message.videoWidth).arg(message.videoHeight);
    m_chatDisplay->append(videoHtml);
    updateRecentChats(message.senderId, message.senderName, "[视频消息]");

    if (!m_contacts[message.senderId].dnd) {
        QApplication::beep();
    }
}

void IPMsgWidget::onLocationMessageReceived(const IPMsgMessage& message) {
    // Rich location message card with clickable map link
    QString mapUrl = QString("https://maps.google.com/?q=%1,%2")
        .arg(message.locationLatitude, message.locationLongitude);
    QString locationHtml = QString(
        "<div style='margin: 6px 60px 6px 0; padding: 12px 16px; background: linear-gradient(135deg, #1a2a3c, #1a1a2c); "
        "border-radius: 12px; border-left: 4px solid #1E88E5;'>"
        "<span style='color: #1E88E5; font-weight: bold; font-size: 13px;'>📍 位置分享</span><br>"
        "<span style='color: #E0E0E0; font-size: 13px;'>%1</span><br>"
        "<a href='%2' style='color: #64B5F6; font-size: 12px; text-decoration: underline;'>在地图中打开</a>"
        "</div>")
        .arg(message.locationName.isEmpty() ? tr("未知位置") : message.locationName.toHtmlEscaped(), mapUrl);
    m_chatDisplay->append(locationHtml);
    updateRecentChats(message.senderId, message.senderName, "[位置消息]");

    if (!m_contacts[message.senderId].dnd) {
        QApplication::beep();
    }
}

void IPMsgWidget::onCardMessageReceived(const IPMsgMessage& message) {
    // Rich card message with contact info
    QString cardHtml = QString(
        "<div style='margin: 6px 60px 6px 0; padding: 12px 16px; background: linear-gradient(135deg, #2a2a1c, #1a1a1c); "
        "border-radius: 12px; border-left: 4px solid #FB8C00;'>"
        "<span style='color: #FB8C00; font-weight: bold; font-size: 13px;'>👤 名片消息</span><br>"
        "<span style='color: #E0E0E0; font-size: 13px;'>%1</span><br>"
        "<span style='color: #888; font-size: 11px;'>点击添加好友</span>"
        "</div>")
        .arg(message.content.isEmpty() ? tr("联系人") : message.content.toHtmlEscaped());
    m_chatDisplay->append(cardHtml);
    updateRecentChats(message.senderId, message.senderName, "[名片消息]");

    if (!m_contacts[message.senderId].dnd) {
        QApplication::beep();
    }
}

// ────────── VoIP Call Handling ──────────

void IPMsgWidget::onIncomingCall(const QString& callerId, const QString& callerName) {
    if (m_callDialog) return;

    m_callDialog = new QDialog(this, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_callDialog->setWindowTitle(tr("来电"));
    m_callDialog->setFixedSize(320, 200);
    m_callDialog->setStyleSheet("QDialog { background-color: #2E2E2E; border-radius: 10px; }");
    
    auto* layout = new QVBoxLayout(m_callDialog);
    layout->setContentsMargins(20, 20, 20, 20);
    
    QLabel* nameLabel = new QLabel(tr("%1 正在呼叫你...").arg(callerName), m_callDialog);
    nameLabel->setStyleSheet("color: white; font-size: 16px; font-weight: bold;");
    layout->addWidget(nameLabel);
    
    auto* buttonLayout = new QHBoxLayout();
    QPushButton* acceptBtn = new QPushButton(tr("接听"), m_callDialog);
    acceptBtn->setStyleSheet("QPushButton { background-color: #07C160; color: white; border: none; border-radius: 6px; padding: 10px 24px; font-size: 14px; }");
    connect(acceptBtn, &QPushButton::clicked, m_callDialog, [this, callerId]() {
        m_manager->acceptCall(m_targetIp);
        showVoiceCallWindow(callerId);
        m_callDialog->accept();
        m_callDialog = nullptr;
    });
    buttonLayout->addWidget(acceptBtn);
    
    QPushButton* rejectBtn = new QPushButton(tr("拒绝"), m_callDialog);
    rejectBtn->setStyleSheet("QPushButton { background-color: #FA5151; color: white; border: none; border-radius: 6px; padding: 10px 24px; font-size: 14px; }");
    connect(rejectBtn, &QPushButton::clicked, m_callDialog, [this, callerId]() {
        m_manager->rejectCall(callerId);
        m_callDialog->reject();
        m_callDialog = nullptr;
    });
    buttonLayout->addWidget(rejectBtn);
    
    layout->addLayout(buttonLayout);
    m_callDialog->show();
}

void IPMsgWidget::onCallAccepted(const QString& calleeId) {
    if (m_callDialog) {
        m_callDialog->close();
        m_callDialog = nullptr;
    }
    showVoiceCallWindow(m_targetIp);
}

void IPMsgWidget::onCallRejected(const QString& calleeId) {
    if (m_callDialog) {
        QMessageBox::information(this, tr("通话"), tr("对方拒绝了您的来电"));
        m_callDialog->close();
        m_callDialog = nullptr;
    }
}

void IPMsgWidget::onCallEnded(const QString& peerId) {
    endCallInternal();
}

void IPMsgWidget::onIceCandidateReceived(const QString& peerId, const QByteArray& sdp) {
    // Store ICE candidate for WebRTC peer connection setup
    // In a full implementation, this would be forwarded to QPeerConnection::addIceCandidate()
    qDebug() << "ICE candidate received from" << peerId << ", size:" << sdp.size();
    m_iceCandidates[peerId].append(sdp);
}

void IPMsgWidget::showVoiceCallWindow(const QString& peerId) {
    m_callPeerId = peerId;
    m_callDuration = 0;
    m_isMuted = false;
    m_isCameraOn = true;

    m_callWidget = new QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint);
    m_callWidget->setWindowTitle(tr("语音通话"));
    m_callWidget->resize(320, 180);
    m_callWidget->setStyleSheet("QWidget { background-color: #2E2E2E; border-radius: 10px; }");
    
    auto* layout = new QVBoxLayout(m_callWidget);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);
    
    QLabel* peerLabel = new QLabel(tr("通话中: %1").arg(peerId), m_callWidget);
    peerLabel->setStyleSheet("color: white; font-size: 16px; font-weight: bold;");
    peerLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(peerLabel);
    
    QLabel* timerLabel = new QLabel("00:00", m_callWidget);
    timerLabel->setStyleSheet("color: #CCCCCC; font-size: 28px; font-family: monospace;");
    timerLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(timerLabel);
    
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(16);
    
    QPushButton* muteBtn = new QPushButton(QIcon(":/icons/mic.svg"), tr("静音"), m_callWidget);
    muteBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 6px; padding: 10px 16px; font-size: 13px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:checked { background-color: #FA5151; color: white; }");
    muteBtn->setCheckable(true);
    muteBtn->setFixedWidth(100);
    connect(muteBtn, &QPushButton::toggled, this, [this, muteBtn](bool checked) {
        m_isMuted = checked;
        muteBtn->setText(checked ? tr("取消静音") : tr("静音"));
        if (m_callAudioCapture) {
            m_callAudioCapture->setMuted(checked);
        }
    });
    buttonLayout->addWidget(muteBtn);
    
    QPushButton* cameraBtn = new QPushButton(QIcon(":/icons/video.svg"), tr("摄像头"), m_callWidget);
    cameraBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 6px; padding: 10px 16px; font-size: 13px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:checked { background-color: #07C160; color: white; }");
    cameraBtn->setCheckable(true);
    cameraBtn->setChecked(true);
    cameraBtn->setFixedWidth(100);
    connect(cameraBtn, &QPushButton::toggled, this, [this, cameraBtn](bool checked) {
        m_isCameraOn = checked;
        cameraBtn->setText(checked ? tr("关闭摄像头") : tr("开启摄像头"));
        if (checked) {
            if (!m_callCameraCapture) {
                m_callCameraCapture = new CameraCapture(this);
            }
            if (!m_callCameraCapture->isInitialized() && !m_callCameraCapture->initialize()) {
                qWarning() << "IPMsgWidget: failed to start call camera";
                m_isCameraOn = false;
                cameraBtn->setChecked(false);
            }
        } else if (m_callCameraCapture) {
            m_callCameraCapture->shutdown();
        }
    });
    buttonLayout->addWidget(cameraBtn);
    
    QPushButton* endBtn = new QPushButton(QIcon(":/icons/phone-hangup.svg"), tr("挂断"), m_callWidget);
    endBtn->setStyleSheet(
        "QPushButton { background-color: #FA5151; color: white; border: none; border-radius: 6px; padding: 10px 20px; font-size: 13px; }"
        "QPushButton:hover { background-color: #E04040; }");
    endBtn->setFixedWidth(100);
    connect(endBtn, &QPushButton::clicked, this, [this]() { endCallInternal(); });
    buttonLayout->addWidget(endBtn);
    
    layout->addLayout(buttonLayout);
    layout->addStretch();

    // Create the call's microphone capture so the mute button can actually gate audio.
    if (m_callAudioCapture) {
        m_callAudioCapture->shutdown();
        m_callAudioCapture->deleteLater();
        m_callAudioCapture = nullptr;
    }
    m_callAudioCapture = new AudioCapture(this, AudioCapture::Microphone);
    if (!m_callAudioCapture->initialize()) {
        qWarning() << "IPMsgWidget: failed to initialize call microphone capture";
    }
    m_callAudioCapture->setMuted(m_isMuted);

    // Create the call's camera capture so the video toggle can actually gate the stream.
    m_callCameraCapture = new CameraCapture(this);
    if (!m_callCameraCapture->initialize()) {
        qWarning() << "IPMsgWidget: failed to initialize call camera";
        m_isCameraOn = false;
        cameraBtn->setChecked(false);
    }

    m_callWidget->show();
    
    // Start call duration timer
    m_callTimer = new QTimer(this);
    connect(m_callTimer, &QTimer::timeout, this, [this, timerLabel]() {
        m_callDuration++;
        int mins = m_callDuration / 60;
        int secs = m_callDuration % 60;
        timerLabel->setText(QString("%1:%2").arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0')));
    });
    m_callTimer->start(1000);
}

    void IPMsgWidget::endCallInternal() {
    if (m_callTimer) {
        m_callTimer->stop();
        m_callTimer->deleteLater();
        m_callTimer = nullptr;
    }
    m_callDuration = 0;
    m_isMuted = false;
    m_isCameraOn = true;
    m_callPeerId.clear();
    
    if (m_callWidget) {
        m_callWidget->close();
        m_callWidget = nullptr;
    }
    if (m_callAudioCapture) {
        m_callAudioCapture->shutdown();
        m_callAudioCapture->deleteLater();
        m_callAudioCapture = nullptr;
    }
    if (m_callCameraCapture) {
        m_callCameraCapture->shutdown();
        m_callCameraCapture->deleteLater();
        m_callCameraCapture = nullptr;
    }
    if (m_callDialog) {
        m_callDialog->close();
        m_callDialog = nullptr;
    }
}

void IPMsgWidget::onGroupAnnouncementReceived(const QString& groupId, const QString& announcement, const QString& announcer) {
    addChatMessage("", QString("[群公告] %1: %2").arg(announcer, announcement), false);
    
    // Show notification
    QMessageBox::information(this, tr("群公告"), 
        tr("群公告: %1\n发布者: %2").arg(announcement, announcer));
}

void IPMsgWidget::onGroupMentionReceived(const QString& groupId, const QStringList& mentionedMembers, const QString& message, const QString& senderName) {
    // Only show for mentions that include this user
    addChatMessage(senderName, QString("@%1 %2").arg(senderName, message), false);
    
    // Show notification
    QMessageBox::warning(this, tr("@消息"), 
        tr("%1 在群里 @ 了你\n消息: %2").arg(senderName, message));
    
    // Play notification sound
    QApplication::beep();
}

void IPMsgWidget::onGroupVoteReceived(const QString& groupId, const QString& voteTitle, const QStringList& options, int durationSeconds, const QString& creator) {
    // Find the group to get the creator's device ID
    QString creatorId;
    for (const auto& group : m_groups) {
        if (group.id == groupId) {
            // Find creator in group members
            for (int i = 0; i < group.memberNames.size(); ++i) {
                if (group.memberNames[i] == creator) {
                    creatorId = group.memberIds[i];
                    break;
                }
            }
            break;
        }
    }

    // Create a custom dialog for voting with the actual options
    QDialog dialog(this);
    dialog.setWindowTitle(tr("群投票"));
    dialog.setMinimumSize(400, 300);
    dialog.setStyleSheet("QDialog { background-color: #2E2E2E; }");

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* titleLabel = new QLabel(tr("%1 发起投票:").arg(creator), &dialog);
    titleLabel->setStyleSheet("color: white; font-size: 14px; font-weight: bold; margin-bottom: 5px;");
    layout->addWidget(titleLabel);

    QLabel* voteTitleLabel = new QLabel(voteTitle, &dialog);
    voteTitleLabel->setStyleSheet("color: #CCCCCC; font-size: 13px; margin-bottom: 10px;");
    voteTitleLabel->setWordWrap(true);
    layout->addWidget(voteTitleLabel);

    QLabel* optionsLabel = new QLabel(tr("请选择一项:"), &dialog);
    optionsLabel->setStyleSheet("color: white; margin-top: 5px;");
    layout->addWidget(optionsLabel);

    QButtonGroup* buttonGroup = new QButtonGroup(&dialog);
    for (int i = 0; i < options.size(); ++i) {
        QRadioButton* radio = new QRadioButton(options[i], &dialog);
        radio->setStyleSheet("QRadioButton { color: white; padding: 8px; }"
                            "QRadioButton::indicator { width: 18px; height: 18px; }");
        if (i == 0) radio->setChecked(true);
        buttonGroup->addButton(radio, i);
        layout->addWidget(radio);
    }

    layout->addStretch();

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* voteBtn = new QPushButton(tr("提交投票"), &dialog);
    voteBtn->setStyleSheet("QPushButton { background-color: #07C160; color: white; border: none; border-radius: 4px; padding: 10px 20px; font-size: 13px; }");
    QPushButton* cancelBtn = new QPushButton(tr("取消"), &dialog);
    cancelBtn->setStyleSheet("QPushButton { background-color: #666666; color: white; border: none; border-radius: 4px; padding: 10px 20px; font-size: 13px; }");
    btnLayout->addStretch();
    btnLayout->addWidget(voteBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    connect(voteBtn, &QPushButton::clicked, &dialog, [&dialog, this, groupId, voteTitle, creatorId, buttonGroup]() {
        int selectedOption = buttonGroup->checkedId();
        if (selectedOption >= 0 && m_manager) {
            m_manager->sendGroupVoteResponse(groupId, voteTitle, selectedOption, creatorId);
        }
        dialog.accept();
    });

    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    dialog.exec();
}

void IPMsgWidget::onGroupVoteResponseReceived(const QString& groupId, const QString& voteTitle, int selectedOption, const QString& voterId, const QString& voterName) {
    // Show a toast notification when someone votes
    QString optionText = QString::number(selectedOption + 1); // 1-based for display
    ChatMessage cm;
    cm.sender = tr("系统");
    cm.content = tr("[%1 投票了: 选项 %2]").arg(voterName, optionText);
    cm.isSelf = false;
    cm.timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    cm.msgTimestamp = QDateTime::currentMSecsSinceEpoch();
    addChatMessageDirect(cm);
}

void IPMsgWidget::onMergeForwardMessageReceived(const IPMsgMessage& message) {
    // Render merge forward message as a collapsible card
    QString content = message.content;
    // Parse the content - it may contain JSON with forwarded messages
    QString displayContent;
    if (content.startsWith("[")) {
        // Try to parse as JSON array of forwarded messages
        QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            QStringList items;
            for (const QJsonValue& val : arr) {
                QJsonObject obj = val.toObject();
                QString sender = obj["sender"].toString();
                QString text = obj["content"].toString();
                if (text.length() > 60) text = text.left(60) + "...";
                items.append(QString("<b>%1</b>: %2").arg(sender.toHtmlEscaped(), text.toHtmlEscaped()));
            }
            displayContent = items.join("<br>");
        }
    }
    if (displayContent.isEmpty()) {
        displayContent = content.toHtmlEscaped();
        if (displayContent.length() > 200) displayContent = displayContent.left(200) + "...";
    }

    QString forwardHtml = QString(
        "<div style='margin: 6px 60px 6px 0; padding: 12px 16px; background: linear-gradient(135deg, #2a2a3c, #1a1a2c); "
        "border-radius: 12px; border-left: 4px solid #5C6BC0;'>"
        "<span style='color: #5C6BC0; font-weight: bold; font-size: 13px;'>📨 合并转发</span><br>"
        "<div style='margin-top: 6px; padding: 8px; background: rgba(0,0,0,0.2); border-radius: 6px; "
        "color: #E0E0E0; font-size: 12px; line-height: 1.5;'>%1</div>"
        "</div>")
        .arg(displayContent);
    m_chatDisplay->append(forwardHtml);
    updateRecentChats(message.senderId, message.senderName, "[合并转发]");
}

void IPMsgWidget::onGroupSettings() {
    if (m_targetGroupId.isEmpty()) return;
    
    // Find group
    IPMsgGroup* group = nullptr;
    for (int i = 0; i < m_groups.size(); ++i) {
        if (m_groups[i].id == m_targetGroupId) {
            group = &m_groups[i];
            break;
        }
    }
    if (!group) return;
    
    QDialog dialog(this);
    dialog.setWindowTitle(tr("群聊设置 - %1").arg(group->name));
    dialog.setMinimumSize(350, 400);
    dialog.setStyleSheet("QDialog { background-color: #2E2E2E; }");
    
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    
    // Group name
    QLabel* nameLabel = new QLabel(tr("群聊名称:"), &dialog);
    nameLabel->setStyleSheet("color: white;");
    layout->addWidget(nameLabel);
    
    QLineEdit* nameEdit = new QLineEdit(group->name, &dialog);
    nameEdit->setStyleSheet("QLineEdit { background-color: #3C3C3C; color: white; border: none; border-radius: 4px; padding: 8px; }");
    layout->addWidget(nameEdit);
    
    // Members list
    QLabel* memberLabel = new QLabel(tr("群成员 (%1人):").arg(group->memberIds.size()), &dialog);
    memberLabel->setStyleSheet("color: white; margin-top: 10px;");
    layout->addWidget(memberLabel);
    
    QListWidget* memberList = new QListWidget(&dialog);
    memberList->setStyleSheet("QListWidget { background-color: #3C3C3C; color: white; border: none; }"
                             "QListWidget::item { padding: 8px; border-bottom: 1px solid #404040; }");
    
    for (int i = 0; i < group->memberIds.size(); ++i) {
        QString memberName = (i < group->memberNames.size()) ? group->memberNames[i] : "Unknown";
        QListWidgetItem* item = new QListWidgetItem(memberName, memberList);
        item->setData(Qt::UserRole, group->memberIds[i]);
    }
    layout->addWidget(memberList);
    
    // Invite button
    QPushButton* inviteBtn = new QPushButton(tr("邀请成员"), &dialog);
    inviteBtn->setStyleSheet("QPushButton { background-color: #07C160; color: white; border: none; border-radius: 4px; padding: 8px; }");
    connect(inviteBtn, &QPushButton::clicked, &dialog, [this, &dialog, group]() {
        QList<QString> availableContacts;
        for (auto it = m_contacts.constBegin(); it != m_contacts.constEnd(); ++it) {
            if (it.value().online && !group->memberIds.contains(it.value().deviceId)) {
                availableContacts.append(it.value().deviceId);
            }
        }
        
        if (availableContacts.isEmpty()) {
            QMessageBox::information(&dialog, tr("提示"), tr("没有可邀请的联系人"));
            return;
        }
        
        // Simple selection dialog
        QStringList items;
        QMap<QString, QString> idToName;
        for (const QString& id : availableContacts) {
            for (auto it = m_contacts.constBegin(); it != m_contacts.constEnd(); ++it) {
                if (it.value().deviceId == id) {
                    items.append(it.value().name);
                    idToName[it.value().name] = id;
                    break;
                }
            }
        }
        
        bool ok;
        QString selected = QInputDialog::getItem(&dialog, tr("邀请成员"), tr("选择要邀请的联系人"), items, 0, false, &ok);
        if (ok && !selected.isEmpty()) {
            QString memberId = idToName[selected];
            m_manager->inviteToGroup(m_targetGroupId, memberId);
            QMessageBox::information(&dialog, tr("成功"), tr("已邀请 %1").arg(selected));
            dialog.accept();
        }
    });
    layout->addWidget(inviteBtn);
    
    // Remove button
    QPushButton* removeBtn = new QPushButton(tr("移除成员"), &dialog);
    removeBtn->setStyleSheet("QPushButton { background-color: #FA5151; color: white; border: none; border-radius: 4px; padding: 8px; }");
    connect(removeBtn, &QPushButton::clicked, &dialog, [this, &dialog, group, memberList]() {
        QListWidgetItem* item = memberList->currentItem();
        if (!item) {
            QMessageBox::warning(&dialog, tr("提示"), tr("请选择要移除的成员"));
            return;
        }
        
        QString memberId = item->data(Qt::UserRole).toString();
        if (memberId == m_manager->userId()) {
            QMessageBox::warning(&dialog, tr("提示"), tr("不能移除自己"));
            return;
        }
        
        m_manager->removeFromGroup(m_targetGroupId, memberId);
        QMessageBox::information(&dialog, tr("成功"), tr("已移除成员"));
        dialog.accept();
    });
    layout->addWidget(removeBtn);

    // --- Notification Settings ---
    auto* notifLabel = new QLabel(tr("通知设置"), &dialog);
    notifLabel->setStyleSheet("color: #07C160; font-weight: bold; font-size: 13px; margin-top: 8px;");
    layout->addWidget(notifLabel);

    auto* muteCheck = new QCheckBox(tr("消息免打扰"), &dialog);
    DatabaseManager* db = &DatabaseManager::instance();
    if (db) {
        bool isMuted = db->getSetting("group_mute_" + m_targetGroupId, false).toBool();
        muteCheck->setChecked(isMuted);
    }
    connect(muteCheck, &QCheckBox::toggled, this, [this](bool checked) {
        DatabaseManager* db = &DatabaseManager::instance();
        if (db) db->setSetting("group_mute_" + m_targetGroupId, checked);
    });
    layout->addWidget(muteCheck);

    layout->addStretch();
    
    // Announcement button
    QPushButton* announceBtn = new QPushButton(tr("发布公告"), &dialog);
    announceBtn->setStyleSheet("QPushButton { background-color: #1E88E5; color: white; border: none; border-radius: 4px; padding: 8px 16px; }");
    connect(announceBtn, &QPushButton::clicked, &dialog, [this, &dialog, group]() {
        bool ok;
        QString announcement = QInputDialog::getText(&dialog, tr("群公告"),
            tr("请输入群公告内容:"), QLineEdit::Normal, "", &ok);
        if (ok && !announcement.isEmpty()) {
            m_manager->sendGroupAnnouncement(group->id, announcement);
            QMessageBox::information(&dialog, tr("成功"), tr("群公告已发布"));
        }
    });
    layout->addWidget(announceBtn);

    // View announcement history
    QPushButton* viewAnnounceBtn = new QPushButton(tr("查看公告"), &dialog);
    viewAnnounceBtn->setStyleSheet("QPushButton { background-color: #43A047; color: white; border: none; border-radius: 4px; padding: 8px 16px; }");
    connect(viewAnnounceBtn, &QPushButton::clicked, &dialog, [this, &dialog, group]() {
        DatabaseManager* db = &DatabaseManager::instance();
        QString announcement;
        if (db) {
            announcement = db->getSetting("group_announcement_" + group->id).toString();
        }
        if (announcement.isEmpty()) {
            QMessageBox::information(&dialog, tr("群公告"), tr("暂无群公告"));
        } else {
            QMessageBox::information(&dialog, tr("群公告"), announcement);
        }
    });
    layout->addWidget(viewAnnounceBtn);
    
    // @提醒按钮
    QPushButton* mentionBtn = new QPushButton(tr("@提醒"), &dialog);
    mentionBtn->setStyleSheet("QPushButton { background-color: #07C160; color: white; border: none; border-radius: 4px; padding: 8px 16px; }");
    connect(mentionBtn, &QPushButton::clicked, &dialog, [this, &dialog, group]() {
        bool ok;
        QString message = QInputDialog::getText(&dialog, tr("@提醒"),
            tr("请输入@消息内容:"), QLineEdit::Normal, "", &ok);
        if (ok && !message.isEmpty()) {
            QStringList members;
            for (const QString& name : group->memberNames) {
                members.append(name);
            }
            m_manager->sendGroupMention(group->id, members, message);
            QMessageBox::information(&dialog, tr("成功"), tr("@消息已发送"));
        }
    });
    layout->addWidget(mentionBtn);
    
    // 投票按钮
    QPushButton* voteBtn = new QPushButton(tr("发起投票"), &dialog);
    voteBtn->setStyleSheet("QPushButton { background-color: #FB8C00; color: white; border: none; border-radius: 4px; padding: 8px 16px; }");
    connect(voteBtn, &QPushButton::clicked, &dialog, [this, &dialog, group]() {
        // Simple vote dialog
        QDialog voteDialog(&dialog);
        voteDialog.setWindowTitle(tr("发起群投票"));
        voteDialog.setMinimumSize(400, 200);
        voteDialog.setStyleSheet("QDialog { background-color: #2E2E2E; }");
        
        auto* vlayout = new QVBoxLayout(&voteDialog);
        
        QLineEdit* titleEdit = new QLineEdit(&voteDialog);
        titleEdit->setPlaceholderText(tr("投票标题"));
        titleEdit->setStyleSheet("QLineEdit { background-color: #3C3C3C; color: white; border: none; border-radius: 4px; padding: 8px; }");
        vlayout->addWidget(titleEdit);

        QList<QLineEdit*> optEdits;
        for (int i = 0; i < 3; ++i) {
            QLineEdit* optEdit = new QLineEdit(&voteDialog);
            optEdit->setPlaceholderText(tr("选项 %1 (可选)").arg(i + 1));
            optEdit->setStyleSheet("QLineEdit { background-color: #3C3C3C; color: white; border: none; border-radius: 4px; padding: 8px; }");
            vlayout->addWidget(optEdit);
            optEdits.append(optEdit);
        }

        QPushButton* sendVoteBtn = new QPushButton(tr("发送投票"), &voteDialog);
        sendVoteBtn->setStyleSheet("QPushButton { background-color: #07C160; color: white; border: none; border-radius: 4px; padding: 8px 16px; }");
        connect(sendVoteBtn, &QPushButton::clicked, &voteDialog, [this, group, titleEdit, optEdits, &voteDialog]() {
            if (!titleEdit->text().isEmpty()) {
                QStringList options;
                for (QLineEdit* edit : optEdits) {
                    if (!edit->text().trimmed().isEmpty()) {
                        options.append(edit->text().trimmed());
                    }
                }
                if (options.isEmpty()) {
                    options = QStringList() << "同意" << "不同意";
                }
                m_manager->sendGroupVote(group->id, titleEdit->text(), options, 86400);
                voteDialog.accept();
            }
        });
        vlayout->addWidget(sendVoteBtn);
        
        voteDialog.exec();
    });
    layout->addWidget(voteBtn);
    
    // Button row
    auto* buttonLayout = new QHBoxLayout();
    
    QPushButton* dissolveBtn = new QPushButton(tr("解散群聊"), &dialog);
    dissolveBtn->setStyleSheet("QPushButton { background-color: #FA5151; color: white; border: none; border-radius: 4px; padding: 8px 16px; }");
    connect(dissolveBtn, &QPushButton::clicked, &dialog, [this, &dialog, group]() {
        QMessageBox::StandardButton reply = QMessageBox::question(&dialog,
            tr("解散群聊"), tr("确定要解散群聊 '%1' 吗？").arg(group->name),
            QMessageBox::Yes | QMessageBox::No);
        
        if (reply == QMessageBox::Yes) {
            m_manager->dissolveGroup(m_targetGroupId);
            for (int i = 0; i < m_groups.size(); ++i) {
                if (m_groups[i].id == m_targetGroupId) {
                    m_groups.removeAt(i);
                    break;
                }
            }
            m_targetGroupId.clear();
            m_chatTitleLabel->setText(tr("选择联系人开始聊天"));
            m_groupSettingsBtn->setVisible(false);
            m_chatDisplay->clear();
            dialog.accept();
        }
    });
    buttonLayout->addWidget(dissolveBtn);
    
    buttonLayout->addStretch();
    
    QPushButton* saveBtn = new QPushButton(tr("保存"), &dialog);
    saveBtn->setStyleSheet("QPushButton { background-color: #07C160; color: white; border: none; border-radius: 4px; padding: 8px 16px; }");
    connect(saveBtn, &QPushButton::clicked, &dialog, [this, &dialog, group, nameEdit]() {
        QString newName = nameEdit->text().trimmed();
        if (!newName.isEmpty()) {
            m_manager->updateGroupName(m_targetGroupId, newName);
            group->name = newName;
            m_targetName = newName;
            m_chatTitleLabel->setText(QString("👥 %1").arg(newName));
        }
        dialog.accept();
    });
    buttonLayout->addWidget(saveBtn);
    
    layout->addLayout(buttonLayout);
    
    dialog.exec();
}

void IPMsgWidget::onChangeBackground() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("设置聊天背景"));
    dialog.setFixedSize(300, 200);
    dialog.setStyleSheet("QDialog { background-color: #2E2E2E; }");
    
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    
    // Background color option
    QLabel* colorLabel = new QLabel(tr("背景颜色:"), &dialog);
    colorLabel->setStyleSheet("color: white;");
    layout->addWidget(colorLabel);
    
    QHBoxLayout* colorLayout = new QHBoxLayout();
    QStringList colors = {"#3C3C3C", "#2E2E2E", "#1E1E1E", "#FFFFFF", "#F5F5F5", "#E8E8E8"};
    for (const QString& color : colors) {
        QPushButton* colorBtn = new QPushButton(&dialog);
        colorBtn->setFixedSize(30, 30);
        colorBtn->setStyleSheet(QString("QPushButton { background-color: %1; border: 2px solid #555; border-radius: 4px; }"
                                        "QPushButton:hover { border-color: #07C160; }").arg(color));
        connect(colorBtn, &QPushButton::clicked, this, [this, color]() {
            m_backgroundColor = QColor(color);
            m_chatDisplay->setStyleSheet(QString("QTextEdit { background-color: %1; border: none; padding: 10px; }"
                "QScrollBar:vertical { width: 6px; background: %1; }"
                "QScrollBar::handle:vertical { background: #666; border-radius: 3px; min-height: 30px; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }").arg(color));
        });
        colorLayout->addWidget(colorBtn);
    }
    layout->addLayout(colorLayout);
    
    layout->addStretch();
    
    // Background image option
    QPushButton* imageBtn = new QPushButton(tr("选择背景图片"), &dialog);
    imageBtn->setStyleSheet("QPushButton { background-color: #3C3C3C; color: white; border: none; border-radius: 4px; padding: 8px; }");
    connect(imageBtn, &QPushButton::clicked, this, [this]() {
        QString file = QFileDialog::getOpenFileName(this, tr("选择背景图片"), "",
            tr("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (!file.isEmpty()) {
            m_backgroundPath = file;
            m_chatDisplay->setStyleSheet(QString("QTextEdit { background-image: url(%1); background-color: #3C3C3C; "
                "border: none; padding: 10px; }"
                "QScrollBar:vertical { width: 6px; background: #3C3C3C; }"
                "QScrollBar::handle:vertical { background: #666; border-radius: 3px; min-height: 30px; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }").arg(file));
        }
    });
    layout->addWidget(imageBtn);
    
    // Reset button
    QPushButton* resetBtn = new QPushButton(tr("恢复默认"), &dialog);
    resetBtn->setStyleSheet("QPushButton { background-color: #3C3C3C; color: white; border: none; border-radius: 4px; padding: 8px; }");
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        m_backgroundPath.clear();
        m_backgroundColor = QColor(60, 60, 60);
        m_chatDisplay->setStyleSheet(
            "QTextEdit { background-color: #3C3C3C; border: none; padding: 10px; }"
            "QScrollBar:vertical { width: 6px; background: #3C3C3C; }"
            "QScrollBar::handle:vertical { background: #666; border-radius: 3px; min-height: 30px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");
    });
    layout->addWidget(resetBtn);
    
    dialog.exec();
}

void IPMsgWidget::selectContact(const QString& ip) {
    if (!m_contacts.contains(ip)) return;

    m_targetIp = ip;
    m_targetName = m_contacts[ip].name;
    m_chatTitleLabel->setText(m_targetName);

    // Hide group-specific buttons when selecting a contact
    m_groupSettingsBtn->setVisible(false);
    m_statsBtn->setVisible(false);
    m_memberManagementBtn->setVisible(false);
    if (m_statsPanel) {
        m_statsPanel->setVisible(false);
    }
    if (m_groupStatsWidget) {
        m_groupStatsWidget->stopAnimation();
    }
    m_statsBtn->setChecked(false);

    if (m_memberManagementPanel) {
        m_memberManagementPanel->setVisible(false);
    }
    m_memberManagementBtn->setChecked(false);

    // Update contact list highlighting
    for (int i = 0; i < m_contactListLayout->count(); ++i) {
        QLayoutItem* layoutItem = m_contactListLayout->itemAt(i);
        if (!layoutItem || !layoutItem->widget()) continue;
        QWidget* w = layoutItem->widget();
        QString itemIp = w->property("contact_ip").toString();
        bool isSelected = (itemIp == ip);
        w->setStyleSheet(isSelected ?
            "QWidget { background-color: #3C3C3C; border-left: 3px solid #07C160; }" :
            "QWidget { background-color: #2E2E2E; border-left: 3px solid transparent; }"
            "QWidget:hover { background-color: #363636; }");
    }

    // Clear chat and show history
    m_chatDisplay->clear();
    m_chatMessages.clear();
    m_searchMatchIndices.clear();
    m_currentMatchIndex = -1;
    if (m_chatSearchWidget) {
        m_chatSearchWidget->deactivate();
    }
    addChatMessage("", tr("开始与 %1 的对话").arg(m_targetName), false, QDateTime::currentDateTime().toString("HH:mm"));

    m_messageInput->setFocus();
}

void IPMsgWidget::onForwardMessage(const QString& message) {
    if (message.isEmpty()) return;
    
    // Create forward dialog
    QDialog dialog(this);
    dialog.setWindowTitle(tr("转发消息"));
    dialog.setMinimumSize(300, 400);
    dialog.setStyleSheet("QDialog { background-color: #2E2E2E; }");
    
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    
    QLabel* titleLabel = new QLabel(tr("选择转发对象:"), &dialog);
    titleLabel->setStyleSheet("color: white; font-weight: bold;");
    layout->addWidget(titleLabel);
    
    // Message preview
    QString preview = message;
    if (preview.length() > 100) preview = preview.left(100) + "...";
    QLabel* previewLabel = new QLabel(tr("消息: %1").arg(preview), &dialog);
    previewLabel->setStyleSheet("color: #999; font-size: 12px;");
    previewLabel->setWordWrap(true);
    layout->addWidget(previewLabel);
    
    layout->addSpacing(10);
    
    // Contact list
    QList<QCheckBox*> contactCheckBoxes;
    for (auto it = m_contacts.constBegin(); it != m_contacts.constEnd(); ++it) {
        if (!it.value().online) continue;
        
        QCheckBox* checkBox = new QCheckBox(it.value().name, &dialog);
        checkBox->setStyleSheet("QCheckBox { color: white; }");
        checkBox->setProperty("contactIp", it.key());
        contactCheckBoxes.append(checkBox);
        layout->addWidget(checkBox);
    }
    
    layout->addStretch();
    
    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    QPushButton* cancelBtn = new QPushButton(tr("取消"), &dialog);
    cancelBtn->setStyleSheet("QPushButton { background-color: #3C3C3C; color: white; border: none; border-radius: 4px; padding: 8px 16px; }");
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    buttonLayout->addWidget(cancelBtn);
    
    QPushButton* forwardBtn = new QPushButton(tr("转发"), &dialog);
    forwardBtn->setStyleSheet("QPushButton { background-color: #07C160; color: white; border: none; border-radius: 4px; padding: 8px 16px; }");
    connect(forwardBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttonLayout->addWidget(forwardBtn);
    
    layout->addLayout(buttonLayout);
    
    if (dialog.exec() == QDialog::Accepted) {
        QList<QString> selectedIps;
        for (QCheckBox* checkBox : contactCheckBoxes) {
            if (checkBox->isChecked()) {
                selectedIps.append(checkBox->property("contactIp").toString());
            }
        }
        
        if (selectedIps.isEmpty()) {
            QMessageBox::warning(this, tr("提示"), tr("请选择至少一个转发对象"));
            return;
        }
        
        // Forward message to selected contacts
        for (const QString& ip : selectedIps) {
            sendMessage(ip, tr("[转发] %1").arg(message));
        }
        
        QMessageBox::information(this, tr("成功"), tr("消息已转发给 %1 人").arg(selectedIps.size()));
    }
}

void IPMsgWidget::scrollToBottom() {
    QScrollBar* scrollbar = m_chatDisplay->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}

void IPMsgWidget::showEmojiPanel() {
    m_emojiPanelVisible = !m_emojiPanelVisible;
    m_emojiPanel->setVisible(m_emojiPanelVisible);
}

void IPMsgWidget::onSendClicked() {
    QString message = m_messageInput->text().trimmed();
    if (message.isEmpty()) return;

    // Check if in group mode
    if (!m_targetGroupId.isEmpty()) {
        addChatMessage(m_manager->userName(), message, true);
        emit sendGroupMessage(m_targetGroupId, message);
        m_messageInput->clear();
        m_messageInput->setFocus();
        return;
    }

    if (m_targetIp.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择联系人"));
        return;
    }

    QString recallId = QString("%1_%2").arg(m_manager->userId()).arg(QDateTime::currentMSecsSinceEpoch());
    if (!m_replyTo.isEmpty()) {
        addChatMessage(m_manager->userName(), message, true, "", m_replyTo, m_replyContent);
        emit sendReply(m_targetIp, message, m_replyTo, m_replyContent);
        clearReplyPreview();
    } else {
        addChatMessage(m_manager->userName(), message, true);
        emit sendMessage(m_targetIp, message);
    }
    
    // Update recent chats
    updateRecentChats(m_targetIp, m_targetName, message);
    
    m_recallableMessages[recallId] = QDateTime::currentMSecsSinceEpoch();
    m_messageInput->clear();
    m_messageInput->setFocus();
}

void IPMsgWidget::onSendFileClicked() {
    if (m_targetIp.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择联系人"));
        return;
    }

    QStringList files = QFileDialog::getOpenFileNames(this, tr("选择文件"));
    if (files.isEmpty()) return;

    QList<SendPreviewItem> items;
    for (const QString& file : files) {
        QFileInfo fi(file);
        if (!fi.exists()) continue;
        items.append({file, fi.fileName(), fi.size(), false});
    }
    if (items.isEmpty()) return;

    SendPreviewDialog dlg(m_targetName.isEmpty() ? m_targetIp : m_targetName,
                          items, e2eeActiveForTarget(), this);
    if (dlg.exec() == QDialog::Accepted) sendItems(dlg.items());
}

void IPMsgWidget::onSendFolderClicked() {
    if (m_targetIp.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择联系人"));
        return;
    }

    QString dir = QFileDialog::getExistingDirectory(this, tr("选择文件夹"));
    if (dir.isEmpty()) return;

    QFileInfo fi(dir);
    qint64 dirSize = 0;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        dirSize += it.fileInfo().size();
    }
    QList<SendPreviewItem> items;
    items.append({dir, fi.fileName(), dirSize, true});

    SendPreviewDialog dlg(m_targetName.isEmpty() ? m_targetIp : m_targetName,
                          items, e2eeActiveForTarget(), this);
    if (dlg.exec() == QDialog::Accepted) sendItems(dlg.items());
}

bool IPMsgWidget::e2eeActiveForTarget() const {
    if (!m_manager || m_targetIp.isEmpty()) return false;
    QString devId = m_contacts.value(m_targetIp).deviceId;
    if (devId.isEmpty()) return false;
    return m_manager->hasEstablishedSession(devId);
}

void IPMsgWidget::sendItems(const QList<SendPreviewItem>& items) {
    for (const SendPreviewItem& it : items) {
        if (it.isDir) {
            emit sendFolder(m_targetIp, it.path);
            addFileMessage(m_manager->userName(), it.name + "/", 0, true);
        } else {
            emit sendFile(m_targetIp, it.path);
            addFileMessage(m_manager->userName(), it.name, it.size, true);
        }
    }
}

void IPMsgWidget::onSendImageClicked() {
    if (m_targetIp.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择联系人"));
        return;
    }

    QStringList files = QFileDialog::getOpenFileNames(this, tr("选择图片"), "",
        tr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp)"));
    for (const QString& file : files) {
        QFile imageFile(file);
        if (imageFile.open(QIODevice::ReadOnly)) {
            QByteArray imageData = imageFile.readAll();
            emit sendImage(m_targetIp, imageData, QFileInfo(file).fileName());
            addImageMessage(m_manager->userName(), imageData, QFileInfo(file).fileName(), true);
        }
    }
}

void IPMsgWidget::onSendEmoji() {
    showEmojiPanel();
}

void IPMsgWidget::onMessageReceived(const QString& sender, const QString& message) {
    // Always show received messages
    addChatMessage(sender, message, false);
    
    // Update recent chats - find sender's IP
    for (auto it = m_contacts.constBegin(); it != m_contacts.constEnd(); ++it) {
        if (it.value().name == sender) {
            updateRecentChats(it.key(), sender, message);
            
            // Send read receipt
            QString messageId = QString("%1_%2").arg(sender).arg(QDateTime::currentMSecsSinceEpoch());
            m_manager->sendReadReceipt(it.key(), messageId);
            
            // Play notification sound (unless DND)
            if (!it.value().dnd) {
                QApplication::beep();
            }
            break;
        }
    }
    
    // Update window title if parent is a dialog
    if (QWidget* parent = parentWidget()) {
        if (QDialog* dialog = qobject_cast<QDialog*>(parent)) {
            QString currentTitle = dialog->windowTitle();
            if (!currentTitle.contains("●")) {
                dialog->setWindowTitle("● " + currentTitle);
            }
        }
    }
}

void IPMsgWidget::onReplyToMessage(const QString& sender, const QString& message, const QString& timestamp) {
    m_replyTo = message;
    m_replyContent = QString("[%1] %2: %3").arg(timestamp, sender, message);
    if (m_replyContent.length() > 60) m_replyContent = m_replyContent.left(60) + "...";
    m_replyPreviewLabel->setText(m_replyContent);
    m_replyPreview->setVisible(true);
    m_messageInput->setFocus();
}

void IPMsgWidget::onClearReply() {
    clearReplyPreview();
}

void IPMsgWidget::updateRecentChats(const QString& ip, const QString& name, const QString& message) {
    // Find existing recent chat or add new one
    int index = -1;
    for (int i = 0; i < m_recentChats.size(); ++i) {
        if (m_recentChats[i].ip == ip) {
            index = i;
            break;
        }
    }
    
    if (index >= 0) {
        // Update existing
        m_recentChats[index].lastMessage = message;
        m_recentChats[index].timestamp = QDateTime::currentMSecsSinceEpoch();
        // Move to front
        RecentChat chat = m_recentChats.takeAt(index);
        m_recentChats.prepend(chat);
    } else {
        // Add new
        RecentChat chat;
        chat.ip = ip;
        chat.name = name;
        chat.lastMessage = message;
        chat.timestamp = QDateTime::currentMSecsSinceEpoch();
        chat.unreadCount = 0;
        m_recentChats.prepend(chat);
    }
    
    // Keep only last 10 recent chats
    while (m_recentChats.size() > 10) {
        m_recentChats.removeLast();
    }
    
    updateContactList();
}

void IPMsgWidget::showContactDetails(const QString& ip) {
    if (!m_contacts.contains(ip)) return;
    
    const ContactInfo& info = m_contacts[ip];
    
    QDialog* dialog = new QDialog(this);
    dialog->setWindowTitle(tr("联系人详情"));
    dialog->setFixedSize(300, 250);
    dialog->setStyleSheet("QDialog { background-color: #2E2E2E; }");
    
    QVBoxLayout* layout = new QVBoxLayout(dialog);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 20, 20, 20);
    
    // Avatar
    QLabel* avatarLabel = new QLabel(dialog);
    avatarLabel->setFixedSize(80, 80);
    avatarLabel->setStyleSheet(QString(
        "background-color: %1; color: white; border-radius: 40px; font-weight: bold; font-size: 32px;")
        .arg(getAvatarColor(info.name)));
    avatarLabel->setAlignment(Qt::AlignCenter);
    avatarLabel->setText(getAvatarLetter(info.name));
    layout->addWidget(avatarLabel, 0, Qt::AlignCenter);
    
    // Name
    QLabel* nameLabel = new QLabel(info.name, dialog);
    nameLabel->setStyleSheet("color: white; font-size: 18px; font-weight: bold;");
    nameLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(nameLabel);
    
    // IP
    QLabel* ipLabel = new QLabel(tr("IP: %1").arg(info.ip), dialog);
    ipLabel->setStyleSheet("color: #999; font-size: 13px;");
    ipLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(ipLabel);
    
    // Status
    QLabel* statusLabel = new QLabel(info.online ? tr("在线") : tr("离线"), dialog);
    statusLabel->setStyleSheet(info.online ? 
        "color: #07C160; font-size: 13px;" : 
        "color: #999; font-size: 13px;");
    statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(statusLabel);
    
    layout->addStretch();
    
    // Close button
    QPushButton* closeBtn = new QPushButton(tr("关闭"), dialog);
    closeBtn->setStyleSheet(
        "QPushButton { background-color: #3C3C3C; color: white; border: none; border-radius: 4px; "
        "padding: 8px 16px; font-size: 13px; }"
        "QPushButton:hover { background-color: #404040; }");
    connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::accept);
    layout->addWidget(closeBtn, 0, Qt::AlignCenter);
    
    dialog->exec();
    dialog->deleteLater();
}

void IPMsgWidget::clearReplyPreview() {
    m_replyTo.clear();
    m_replyContent.clear();
    m_replyPreview->setVisible(false);
}

void IPMsgWidget::onRecallMessage(const QString& recallId) {
    if (m_targetIp.isEmpty()) return;
    
    // Find the sender name for the recall message
    QString senderName = m_manager->userName();
    m_manager->recallMessage(m_targetIp, recallId, senderName);
    m_recallableMessages.remove(recallId);
    
    // Add recall notification to chat
    addChatMessage("", tr("你撤回了一条消息"), false);
}

void IPMsgWidget::onMessageRecalled(const QString& recallId, const QString& senderName) {
    // Add recall notification to chat
    addChatMessage("", tr("%1 撤回了一条消息").arg(senderName), false);
}

void IPMsgWidget::onMessageRead(const QString& messageId, const QString& readerName) {
    Q_UNUSED(messageId);
    QString time = QDateTime::currentDateTime().toString("HH:mm");
    QString receiptHtml = QString(
        "<div style='margin: 2px 0 2px 60px; text-align: right;'>"
        "<span style='color: #07C160; font-size: 10px;'>✓✓ %1 已读</span>"
        "</div>").arg(readerName);
    m_chatDisplay->append(receiptHtml);
    scrollToBottom();
}

void IPMsgWidget::onTypingIndicator(const QString& senderName) {
    // Show typing indicator with animated dots
    m_typingLabel->setText(tr("%1 正在输入...").arg(senderName));
    m_typingLabel->setVisible(true);
    m_typingLabel->setStyleSheet(
        "color: #07C160; font-size: 12px; font-style: italic; "
        "background-color: rgba(7, 193, 96, 0.1); border-radius: 10px; padding: 2px 8px;");

    // Hide after 4 seconds
    if (!m_typingTimer) {
        m_typingTimer = new QTimer(this);
        connect(m_typingTimer, &QTimer::timeout, this, [this]() {
            m_typingLabel->setVisible(false);
            m_typingLabel->setStyleSheet("color: #07C160; font-size: 12px; font-style: italic;");
        });
    }
    m_typingTimer->start(4000);
}

void IPMsgWidget::showImageViewer(const QByteArray& imageData, const QString& fileName) {
    QDialog* viewer = new QDialog(this);
    viewer->setWindowTitle(fileName);
    viewer->setMinimumSize(800, 600);
    viewer->setStyleSheet("QDialog { background-color: #1E1E1E; }");
    
    QVBoxLayout* layout = new QVBoxLayout(viewer);
    layout->setContentsMargins(0, 0, 0, 0);
    
    // Image display
    QLabel* imageLabel = new QLabel(viewer);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setStyleSheet("background-color: #1E1E1E;");
    
    QImage image;
    image.loadFromData(imageData);
    if (!image.isNull()) {
        // Scale image to fit while maintaining aspect ratio
        QPixmap pixmap = QPixmap::fromImage(image);
        pixmap = pixmap.scaled(viewer->size() - QSize(20, 60), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        imageLabel->setPixmap(pixmap);
    }
    
    layout->addWidget(imageLabel);
    
    // Button bar
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(10, 5, 10, 5);
    
    // Save button
    QPushButton* saveBtn = new QPushButton(tr("保存"), viewer);
    saveBtn->setStyleSheet(
        "QPushButton { background-color: #3C3C3C; color: white; border: none; border-radius: 4px; "
        "padding: 8px 16px; }"
        "QPushButton:hover { background-color: #404040; }");
    connect(saveBtn, &QPushButton::clicked, viewer, [viewer, imageData, fileName]() {
        QString savePath = QFileDialog::getSaveFileName(viewer, tr("保存图片"), fileName,
            tr("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (!savePath.isEmpty()) {
            QFile file(savePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(imageData);
                file.close();
            }
        }
    });
    buttonLayout->addWidget(saveBtn);
    
    buttonLayout->addStretch();
    
    // Close button
    QPushButton* closeBtn = new QPushButton(tr("关闭"), viewer);
    closeBtn->setStyleSheet(
        "QPushButton { background-color: #3C3C3C; color: white; border: none; border-radius: 4px; "
        "padding: 8px 16px; }"
        "QPushButton:hover { background-color: #404040; }");
    connect(closeBtn, &QPushButton::clicked, viewer, &QDialog::accept);
    buttonLayout->addWidget(closeBtn);
    
    layout->addLayout(buttonLayout);
    
    viewer->exec();
    viewer->deleteLater();
}

void IPMsgWidget::onFriendRequest(const QString& senderId, const QString& senderName, const QString& message) {
    QMessageBox::StandardButton reply = QMessageBox::question(this,
        tr("好友申请"),
        tr("%1 请求添加你为好友\n留言: %2").arg(senderName, message.isEmpty() ? tr("无") : message),
        QMessageBox::Yes | QMessageBox::No);
    
    // Find sender's IP
    QString senderIp;
    for (auto it = m_contacts.constBegin(); it != m_contacts.constEnd(); ++it) {
        if (it.value().deviceId == senderId) {
            senderIp = it.key();
            break;
        }
    }
    
    if (!senderIp.isEmpty()) {
        if (reply == QMessageBox::Yes) {
            m_manager->acceptFriendRequest(senderIp);
            addChatMessage("", tr("已接受 %1 的好友申请").arg(senderName), false);
        } else {
            m_manager->rejectFriendRequest(senderIp);
            addChatMessage("", tr("已拒绝 %1 的好友申请").arg(senderName), false);
        }
    }
}

void IPMsgWidget::onFriendRequestAccepted(const QString& senderId, const QString& senderName) {
    addChatMessage("", tr("%1 已接受你的好友申请").arg(senderName), false);
    QMessageBox::information(this, tr("好友申请"), tr("%1 已接受你的好友申请").arg(senderName));
}

void IPMsgWidget::onFriendRequestRejected(const QString& senderId, const QString& senderName) {
    addChatMessage("", tr("%1 已拒绝你的好友申请").arg(senderName), false);
}

void IPMsgWidget::sendFriendRequestToContact(const QString& ip) {
    if (ip.isEmpty() || !m_contacts.contains(ip)) return;
    
    bool ok;
    QString message = QInputDialog::getText(this, tr("好友申请"),
        tr("请输入验证消息:"), QLineEdit::Normal, "", &ok);
    
    if (ok) {
        m_manager->sendFriendRequest(ip, message);
        QMessageBox::information(this, tr("好友申请"), tr("好友申请已发送"));
    }
}

void IPMsgWidget::searchChatHistory(const QString& keyword) {
    // Delegate to the new search widget
    m_chatSearchWidget->activate();
    if (!keyword.isEmpty()) {
        highlightSearchMatches(keyword);
    }
}

void IPMsgWidget::highlightSearchMatches(const QString& keyword) {
    m_searchMatchIndices.clear();
    m_currentMatchIndex = -1;

    if (keyword.isEmpty() || m_chatMessages.isEmpty()) {
        m_chatSearchWidget->setStyleSheet("");
        // Update count label via child
        return;
    }

    // Find matching messages
    for (int i = 0; i < m_chatMessages.size(); ++i) {
        const ChatMessage& cm = m_chatMessages[i];
        if (cm.content.contains(keyword, Qt::CaseInsensitive) ||
            cm.sender.contains(keyword, Qt::CaseInsensitive)) {
            m_searchMatchIndices.append(i);
        }
    }

    // Update count label
    QLabel* countLabel = m_chatSearchWidget->findChild<QLabel*>();
    // We'll set it via the widget's property or find by name - simpler: just set text on the label
    // The label is the third widget in the layout
    if (m_searchMatchIndices.isEmpty()) {
        // Show "0 results" - we'll handle this via the widget
        // For now, just reset
        m_currentMatchIndex = -1;
    } else {
        m_currentMatchIndex = 0;
    }

    // Rebuild display with highlighted matches
    refreshChatDisplay();

    // Scroll to first match
    if (!m_searchMatchIndices.isEmpty()) {
        scrollToMatch(0);
    }
}

void IPMsgWidget::navigateSearchMatch(bool forward) {
    if (m_searchMatchIndices.isEmpty()) return;

    if (forward) {
        m_currentMatchIndex = (m_currentMatchIndex + 1) % m_searchMatchIndices.size();
    } else {
        m_currentMatchIndex = (m_currentMatchIndex - 1 + m_searchMatchIndices.size()) % m_searchMatchIndices.size();
    }

    scrollToMatch(m_currentMatchIndex);
}

void IPMsgWidget::scrollToMatch(int matchIndex) {
    if (matchIndex < 0 || matchIndex >= m_searchMatchIndices.size()) return;

    int msgIndex = m_searchMatchIndices[matchIndex];

    // Use QTextBrowser::find to highlight and scroll to the match
    // First, we need to search through the text
    QTextCursor cursor = m_chatDisplay->textCursor();
    cursor.movePosition(QTextCursor::Start);

    // Find the nth occurrence of the keyword
    QString keyword = m_chatSearchWidget->findChild<QLineEdit*>()->text();
    int found = 0;
    QTextCursor matchCursor;
    while (true) {
        cursor = m_chatDisplay->document()->find(keyword, cursor,
            QTextDocument::FindCaseSensitively);
        if (cursor.isNull()) break;
        if (found == matchIndex) {
            matchCursor = cursor;
            break;
        }
        found++;
    }

    if (!matchCursor.isNull()) {
        m_chatDisplay->setTextCursor(matchCursor);
        m_chatDisplay->ensureCursorVisible();
    }

    // Update count label
    QLabel* countLabel = m_chatSearchWidget->findChild<QLabel*>();
    if (countLabel) {
        countLabel->setText(tr("%1 / %2").arg(m_currentMatchIndex + 1).arg(m_searchMatchIndices.size()));
    }
}

void IPMsgWidget::onSearchTextChanged(const QString& text) {
    m_searchFilter = text.trimmed().toLower();
    updateContactList();
}

void IPMsgWidget::onFileProgress(const QString& fileName, qint64 bytesTransferred, qint64 totalBytes) {
    m_progressBar->setVisible(true);
    m_progressBar->setMaximum(totalBytes);
    m_progressBar->setValue(bytesTransferred);
}

void IPMsgWidget::onFileCompleted(const QString& fileName, bool integrityOk) {
    m_progressBar->setVisible(false);
    if (integrityOk) {
        addChatMessage("", tr("文件 %1 传输完成").arg(fileName), false);
    } else {
        addChatMessage("", tr("文件 %1 传输失败").arg(fileName), false);
    }
}

void IPMsgWidget::onFileResuming(const QString& fileName, qint64 offset) {
    addChatMessage("", tr("正在续传 %1").arg(fileName), false);
}

void IPMsgWidget::onRefreshDevices() {
    if (m_manager) {
        m_manager->broadcastPresence();
    }
    updateContactList();
}

void IPMsgWidget::onNameEditFinished() {
    if (!m_nameEdit || !m_manager) return;

    QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty()) return;

    if (newName != m_manager->userName()) {
        m_manager->setUserName(newName);
        emit userNameChanged(newName);
        m_manager->broadcastPresence();
    }
}

void IPMsgWidget::onSyncClicked() {
    if (!m_manager) return;

    if (!m_manager->hasAccountConfigured()) {
        bool ok = false;
        QString accountId = QInputDialog::getText(this, tr("多设备同步"), tr("同步账号ID:"), QLineEdit::Normal, "", &ok);
        if (!ok || accountId.trimmed().isEmpty()) return;

        QString syncKey = QInputDialog::getText(this, tr("多设备同步"), tr("同步密钥:"), QLineEdit::Password, "", &ok);
        if (!ok || syncKey.isEmpty()) return;

        m_manager->setAccount(accountId.trimmed(), syncKey);
        m_statusLabel->setText(tr("同步账号已配置"));
        return;
    }

    QList<QString> peers = m_manager->sameAccountDevices();
    if (peers.isEmpty()) {
        QMessageBox::information(this, tr("多设备同步"), tr("未发现同一账号的在线设备。"));
        return;
    }

    int count = 0;
    const QSet<QString> peerSet(peers.begin(), peers.end());
    const auto devices = m_manager->getOnlineDevices();
    for (const IPMsgDevice& device : devices) {
        if (peerSet.contains(device.id)) {
            m_manager->syncWith(device.ip);
            ++count;
        }
    }
    if (count > 0) {
        m_statusLabel->setText(tr("正在同步 %1 台设备...").arg(count));
    }
}

void IPMsgWidget::onSyncCompleted(const QString& deviceId, int appliedCount) {
    m_statusLabel->setText(tr("同步完成: 设备 %1 (应用 %2 项)").arg(deviceId).arg(appliedCount));
    updateContactList();
}

void IPMsgWidget::onSyncFailed(const QString& deviceId, const QString& reason) {
    m_statusLabel->setText(tr("同步失败: %1 - %2").arg(deviceId, reason));
}

void IPMsgWidget::onDataSynced() {
    m_statusLabel->setText(tr("多端数据同步完成"));
    updateContactList();
}

void IPMsgWidget::onSameAccountDeviceFound(const QString& deviceId, const QString& deviceName) {
    m_statusLabel->setText(tr("发现同一账号设备: %1").arg(deviceName));
    updateContactList();
}

void IPMsgWidget::onStatsToggled(bool checked) {
    if (checked && m_transferBtn && m_transferBtn->isChecked()) m_transferBtn->setChecked(false);
    if (m_statsPanel) {
        m_statsPanel->setVisible(checked);
        if (checked && m_groupStatsWidget && !m_targetGroupId.isEmpty()) {
            // Find the group and update statistics
            for (const IPMsgGroup& group : m_groups) {
                if (group.id == m_targetGroupId) {
                    m_groupStatsWidget->setGroup(group);
                    m_groupStatsWidget->startAnimation();
                    break;
                }
            }
        } else if (m_groupStatsWidget) {
            m_groupStatsWidget->stopAnimation();
        }
    }
}

void IPMsgWidget::onMemberManagementToggled(bool checked) {
    if (checked && m_transferBtn && m_transferBtn->isChecked()) m_transferBtn->setChecked(false);
    if (m_memberManagementPanel) {
        m_memberManagementPanel->setVisible(checked);
        if (checked && m_memberManagementWidget && !m_targetGroupId.isEmpty()) {
            // Find the group and update member management
            for (const IPMsgGroup& group : m_groups) {
                if (group.id == m_targetGroupId) {
                    m_memberManagementWidget->setGroup(group);
                    break;
                }
            }
        }
    }
}

void IPMsgWidget::onTransferToggled(bool checked) {
    if (m_transferPanel) {
        m_transferPanel->setVisible(checked);
        if (checked) {
            // Close the other (group-only) panels to avoid stacking.
            if (m_statsBtn && m_statsBtn->isChecked()) m_statsBtn->setChecked(false);
            if (m_memberManagementBtn && m_memberManagementBtn->isChecked()) m_memberManagementBtn->setChecked(false);
            m_transferPanel->refresh();
        }
    }
}

void IPMsgWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void IPMsgWidget::dropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    if (!mimeData->hasUrls()) return;

    if (m_targetIp.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择联系人"));
        return;
    }

    QStringList imageExtensions = {"png", "jpg", "jpeg", "gif", "bmp", "webp"};

    // Collect non-image files / folders for a single batch preview, send
    // images inline as before.
    QList<SendPreviewItem> batch;
    for (const QUrl& url : mimeData->urls()) {
        QString filePath = url.toLocalFile();
        QFileInfo fileInfo(filePath);
        if (!fileInfo.exists()) continue;

        if (fileInfo.isDir()) {
            batch.append({filePath, fileInfo.fileName(), 0, true});
        } else {
            QString ext = fileInfo.suffix().toLower();
            if (imageExtensions.contains(ext)) {
                QFile imageFile(filePath);
                if (imageFile.open(QIODevice::ReadOnly)) {
                    QByteArray imageData = imageFile.readAll();
                    emit sendImage(m_targetIp, imageData, fileInfo.fileName());
                    addImageMessage(m_manager->userName(), imageData, fileInfo.fileName(), true);
                }
            } else {
                batch.append({filePath, fileInfo.fileName(), fileInfo.size(), false});
            }
        }
    }

    if (!batch.isEmpty()) {
        SendPreviewDialog dlg(m_targetName.isEmpty() ? m_targetIp : m_targetName,
                              batch, e2eeActiveForTarget(), this);
        if (dlg.exec() == QDialog::Accepted) sendItems(dlg.items());
    }
}

void IPMsgWidget::keyPressEvent(QKeyEvent* event) {
    // Ctrl+F: Search
    if (event->key() == Qt::Key_F && event->modifiers() == Qt::ControlModifier) {
        if (!m_targetIp.isEmpty() || !m_targetGroupId.isEmpty()) {
            m_chatSearchWidget->activate();
        }
        return;
    }
    if (event->key() == Qt::Key_V && event->modifiers() == Qt::ControlModifier) {
        if (m_targetIp.isEmpty()) return;

        // Check clipboard for images
        QClipboard* clipboard = QApplication::clipboard();
        const QMimeData* mimeData = clipboard->mimeData();
        if (mimeData->hasImage()) {
            QImage image = qvariant_cast<QImage>(mimeData->imageData());
            if (!image.isNull()) {
                QByteArray imageData;
                QBuffer buffer(&imageData);
                buffer.open(QIODevice::WriteOnly);
                image.save(&buffer, "PNG");
                emit sendImage(m_targetIp, imageData, "clipboard_image.png");
                addImageMessage(m_manager->userName(), imageData, "clipboard_image.png", true);
                return;
            }
        }
    }
    QWidget::keyPressEvent(event);
}

} // namespace xrk
