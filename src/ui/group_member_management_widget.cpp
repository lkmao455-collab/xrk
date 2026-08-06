#include "group_member_management_widget.h"
#include <QDebug>
#include <QRandomGenerator>
#include <QEasingCurve>
#include <QScrollBar>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QAction>

namespace xrk {

GroupMemberManagementWidget::GroupMemberManagementWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

GroupMemberManagementWidget::~GroupMemberManagementWidget() {
}

void GroupMemberManagementWidget::setupUI() {
    setStyleSheet(
        "QWidget {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "                  stop:0 #1a1a2e, stop:1 #16213e);"
        "    border-radius: 12px;"
        "    color: #e0e0e0;"
        "    font-family: 'Segoe UI', 'Microsoft YaHei', sans-serif;"
        "}"
        "QLabel {"
        "    color: #ffffff;"
        "    background: transparent;"
        "}"
        "QLineEdit {"
        "    background-color: #2E2E2E;"
        "    color: #e0e0e0;"
        "    border: 1px solid #444;"
        "    border-radius: 8px;"
        "    padding: 8px 12px;"
        "    font-size: 13px;"
        "}"
        "QLineEdit:focus {"
        "    border-color: #07C160;"
        "}"
        "QPushButton {"
        "    background-color: #07C160;"
        "    color: white;"
        "    border: none;"
        "    border-radius: 6px;"
        "    padding: 8px 16px;"
        "    font-weight: 600;"
        "}"
        "QPushButton:hover {"
        "    background-color: #06AD56;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #059A4C;"
        "}"
        "QPushButton:disabled {"
        "    background-color: #444;"
        "    color: #888;"
        "}"
        "QListWidget {"
        "    background-color: transparent;"
        "    border: none;"
        "    outline: none;"
        "}"
        "QListWidget::item {"
        "    background-color: transparent;"
        "    border-bottom: 1px solid rgba(255, 255, 255, 0.05);"
        "    padding: 0;"
        "}"
        "QListWidget::item:selected {"
        "    background-color: rgba(7, 193, 96, 0.2);"
        "}"
        "QListWidget::item:hover {"
        "    background-color: rgba(255, 255, 255, 0.03);"
        "}"
        "QScrollBar:vertical {"
        "    background: transparent;"
        "    width: 6px;"
        "    margin: 0;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background: rgba(255, 255, 255, 0.2);"
        "    border-radius: 3px;"
        "    min-height: 30px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "    background: rgba(7, 193, 96, 0.8);"
        "}"
        "QMenu {"
        "    background-color: #2E2E2E;"
        "    color: #E0E0E0;"
        "    border: 1px solid #444;"
        "    border-radius: 8px;"
        "    padding: 4px;"
        "}"
        "QMenu::item {"
        "    padding: 10px 24px;"
        "    border-radius: 4px;"
        "}"
        "QMenu::item:selected {"
        "    background-color: #07C160;"
        "}"
        "QMenu::separator {"
        "    height: 1px;"
        "    background: #444;"
        "    margin: 4px 8px;"
        "}"
    );

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // Header
    m_headerWidget = new QWidget(this);
    m_headerWidget->setFixedHeight(72);
    m_headerWidget->setStyleSheet("QWidget { background-color: #252525; border-bottom: 1px solid #333; border-radius: 12px 12px 0 0; }");
    
    auto* headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(16, 8, 16, 8);
    headerLayout->setSpacing(12);

    m_groupNameLabel = new QLabel(tr("群成员管理"), m_headerWidget);
    m_groupNameLabel->setStyleSheet("font-size: 16px; font-weight: 600; color: #ffffff;");
    headerLayout->addWidget(m_groupNameLabel);

    headerLayout->addStretch();

    m_memberCountLabel = new QLabel("0 / 0", m_headerWidget);
    m_memberCountLabel->setStyleSheet("font-size: 13px; color: #888888;");
    headerLayout->addWidget(m_memberCountLabel);

    m_onlineCountLabel = new QLabel(tr("在线: 0"), m_headerWidget);
    m_onlineCountLabel->setStyleSheet("font-size: 13px; color: #07C160;");
    headerLayout->addWidget(m_onlineCountLabel);

    m_mainLayout->addWidget(m_headerWidget);

    // Search and actions bar
    auto* barWidget = new QWidget(this);
    barWidget->setFixedHeight(56);
    barWidget->setStyleSheet("QWidget { background-color: #252525; }");
    
    auto* barLayout = new QHBoxLayout(barWidget);
    barLayout->setContentsMargins(16, 8, 16, 8);
    barLayout->setSpacing(12);

    m_searchInput = new QLineEdit(barWidget);
    m_searchInput->setPlaceholderText(tr("搜索成员..."));
    m_searchInput->setFixedWidth(200);
    connect(m_searchInput, &QLineEdit::textChanged, this, &GroupMemberManagementWidget::applySearchFilter);
    barLayout->addWidget(m_searchInput);

    barLayout->addStretch();

    m_inviteBtn = new QPushButton(tr("邀请成员"), barWidget);
    m_inviteBtn->setFixedHeight(36);
    m_inviteBtn->setCursor(Qt::PointingHandCursor);
    connect(m_inviteBtn, &QPushButton::clicked, this, &GroupMemberManagementWidget::onInviteMembersTriggered);
    barLayout->addWidget(m_inviteBtn);

    m_mainLayout->addWidget(barWidget);

    // Member list
    m_memberList = new QListWidget(this);
    m_memberList->setStyleSheet("QListWidget { background-color: #1E1E1E; }");
    m_memberList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_memberList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_memberList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_memberList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    
    connect(m_memberList, &QListWidget::customContextMenuRequested, 
            this, &GroupMemberManagementWidget::onContextMenuRequested);
    connect(m_memberList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem* item) {
                if (item) {
                    QString memberId = item->data(Qt::UserRole).toString();
                    emit messageToMember(memberId, item->data(Qt::UserRole + 1).toString());
                }
            });

    m_mainLayout->addWidget(m_memberList, 1);

    // Empty state
    m_emptyStateWidget = new QWidget(this);
    m_emptyStateWidget->setStyleSheet("QWidget { background-color: #1E1E1E; }");
    m_emptyStateWidget->setVisible(false);
    
    auto* emptyLayout = new QVBoxLayout(m_emptyStateWidget);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(12);

    QLabel* emptyIcon = new QLabel("👥", m_emptyStateWidget);
    emptyIcon->setStyleSheet("font-size: 48px; background: transparent;");
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyIcon);

    QLabel* emptyText = new QLabel(tr("暂无成员"), m_emptyStateWidget);
    emptyText->setStyleSheet("font-size: 16px; color: #888888; background: transparent;");
    emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyText);

    QLabel* emptyHint = new QLabel(tr("点击「邀请成员」添加第一位成员"), m_emptyStateWidget);
    emptyHint->setStyleSheet("font-size: 13px; color: #666666; background: transparent;");
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyHint);

    m_mainLayout->addWidget(m_emptyStateWidget, 1);

    // Setup context menu
    setupContextMenu();
}

void GroupMemberManagementWidget::setGroup(const IPMsgGroup& group) {
    m_currentGroup = group;
    m_groupNameLabel->setText(tr("%1 - 成员管理").arg(group.name));
    m_memberCountLabel->setText(QString("%1 / %2").arg(group.memberIds.size()).arg(group.memberIds.size()));
    refreshMemberList();
}

void GroupMemberManagementWidget::setManager(IPMsgManager* manager) {
    m_manager = manager;
    refreshMemberList();
}

void GroupMemberManagementWidget::refreshMemberList() {
    if (!m_manager) return;

    m_memberList->clear();
    m_memberMap.clear();

    // Get online devices
    QList<IPMsgDevice> devices = m_manager->getOnlineDevices();
    QMap<QString, IPMsgDevice> onlineDevices;
    for (const auto& device : devices) {
        onlineDevices[device.id] = device;
    }

    int onlineCount = 0;
    
    // Add members from group
    for (int i = 0; i < m_currentGroup.memberIds.size(); ++i) {
        const QString& memberId = m_currentGroup.memberIds[i];
        QString memberName = (i < m_currentGroup.memberNames.size()) ? m_currentGroup.memberNames[i] : tr("未知成员");
        
        bool isOnline = onlineDevices.contains(memberId);
        QString memberIp = "";
        if (isOnline) {
            memberIp = onlineDevices[memberId].ip;
            onlineCount++;
        }

        // Determine role (first member is owner, could be extended with actual role data)
        int role = 0;
        if (i == 0) role = 2; // owner
        
        MemberInfo info;
        info.id = memberId;
        info.name = memberName;
        info.ip = memberIp;
        info.online = isOnline;
        info.role = role;
        info.lastSeen = isOnline ? QDateTime::currentMSecsSinceEpoch() : 0;
        info.avatarColor = getAvatarColor(memberName);
        info.selected = false;

        m_memberMap[memberId] = info;
        createMemberItem(info, m_memberList->count());
    }

    // Update counts
    int totalCount = m_currentGroup.memberIds.size();
    m_memberCountLabel->setText(QString("%1 / %2").arg(onlineCount).arg(totalCount));
    m_onlineCountLabel->setText(tr("在线: %1").arg(onlineCount));

    // Show/hide empty state
    bool hasMembers = !m_currentGroup.memberIds.isEmpty();
    m_memberList->setVisible(hasMembers);
    m_emptyStateWidget->setVisible(!hasMembers);
}

void GroupMemberManagementWidget::createMemberItem(const MemberInfo& info, int index) {
    QListWidgetItem* item = new QListWidgetItem(m_memberList);
    item->setData(Qt::UserRole, info.id);
    item->setData(Qt::UserRole + 1, info.name);
    item->setSizeHint(QSize(0, 72));

    QWidget* itemWidget = new QWidget();
    itemWidget->setStyleSheet(
        "QWidget {"
        "    background: transparent;"
        "    border-radius: 8px;"
        "    padding: 8px 12px;"
        "}"
        "QWidget:hover {"
        "    background: rgba(255, 255, 255, 0.05);"
        "}"
    );

    auto* layout = new QHBoxLayout(itemWidget);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(12);

    // Avatar
    QLabel* avatar = new QLabel(itemWidget);
    avatar->setFixedSize(44, 44);
    avatar->setStyleSheet(QString(
        "background-color: %1; color: white; border-radius: 22px; font-weight: 600; font-size: 16px;")
        .arg(info.avatarColor));
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setText(getAvatarLetter(info.name));
    layout->addWidget(avatar);

    // Online indicator
    QLabel* onlineDot = new QLabel(itemWidget);
    onlineDot->setFixedSize(12, 12);
    onlineDot->setStyleSheet(QString(
        "background-color: %1; border-radius: 6px; border: 2px solid #252525;")
        .arg(info.online ? "#07C160" : "#757575"));
    onlineDot->move(34, 34);
    onlineDot->raise();
    layout->addWidget(onlineDot);

    // Name and status
    auto* textLayout = new QVBoxLayout();
    textLayout->setSpacing(2);

    QLabel* nameLabel = new QLabel(info.name, itemWidget);
    nameLabel->setStyleSheet("color: #E0E0E0; font-size: 14px; font-weight: 500;");
    textLayout->addWidget(nameLabel);

    QString statusText;
    QString statusColor;
    if (info.role == 2) {
        statusText = tr("群主");
        statusColor = "#FB8C00";
    } else if (info.role == 1) {
        statusText = info.online ? tr("管理员 · 在线") : tr("管理员 · 离线");
        statusColor = info.online ? "#07C160" : "#757575";
    } else {
        statusText = info.online ? tr("成员 · 在线") : tr("成员 · 离线");
        statusColor = info.online ? "#07C160" : "#757575";
    }
    
    QLabel* statusLabel = new QLabel(statusText, itemWidget);
    statusLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(statusColor));
    textLayout->addWidget(statusLabel);

    layout->addLayout(textLayout, 1);

    // Role badge
    if (info.role > 0) {
        QLabel* roleBadge = new QLabel(info.role == 2 ? tr("群主") : tr("管理员"), itemWidget);
        roleBadge->setStyleSheet(QString(
            "background-color: %1; color: white; border-radius: 10px; padding: 2px 8px; font-size: 10px; font-weight: 600;")
            .arg(info.role == 2 ? "#FB8C00" : "#07C160"));
        roleBadge->setAlignment(Qt::AlignCenter);
        layout->addWidget(roleBadge);
    }

    m_memberList->setItemWidget(item, itemWidget);
    item->setData(Qt::UserRole, info.id);
    item->setData(Qt::UserRole + 1, info.name);
}

void GroupMemberManagementWidget::setupContextMenu() {
    m_memberContextMenu = new QMenu(this);
    m_memberContextMenu->setStyleSheet(
        "QMenu {"
        "    background-color: #2E2E2E;"
        "    color: #E0E0E0;"
        "    border: 1px solid #444;"
        "    border-radius: 8px;"
        "    padding: 4px;"
        "}"
        "QMenu::item {"
        "    padding: 10px 24px;"
        "    border-radius: 4px;"
        "}"
        "QMenu::item:selected {"
        "    background-color: #07C160;"
        "}"
        "QMenu::separator {"
        "    height: 1px;"
        "    background: #444;"
        "    margin: 4px 8px;"
        "}"
    );

    m_makeAdminAction = m_memberContextMenu->addAction(tr("设为管理员"));
    connect(m_makeAdminAction, &QAction::triggered, this, &GroupMemberManagementWidget::onMakeAdminTriggered);

    m_removeAdminAction = m_memberContextMenu->addAction(tr("取消管理员"));
    connect(m_removeAdminAction, &QAction::triggered, this, &GroupMemberManagementWidget::onRemoveAdminTriggered);

    m_memberContextMenu->addSeparator();

    m_kickAction = m_memberContextMenu->addAction(tr("移出群聊"));
    m_kickAction->setIcon(QIcon(":/icons/close.svg"));
    connect(m_kickAction, &QAction::triggered, this, &GroupMemberManagementWidget::onKickMemberTriggered);

    m_memberContextMenu->addSeparator();

    m_sendMessageAction = m_memberContextMenu->addAction(tr("发送消息"));
    connect(m_sendMessageAction, &QAction::triggered, this, &GroupMemberManagementWidget::onSendMessageTriggered);

    m_viewProfileAction = m_memberContextMenu->addAction(tr("查看资料"));
    connect(m_viewProfileAction, &QAction::triggered, this, &GroupMemberManagementWidget::onViewProfileTriggered);
}

void GroupMemberManagementWidget::onContextMenuRequested(const QPoint& pos) {
    QListWidgetItem* item = m_memberList->itemAt(pos);
    if (!item) return;

    QString memberId = item->data(Qt::UserRole).toString();
    if (!m_memberMap.contains(memberId)) return;

    MemberInfo& info = m_memberMap[memberId];

    // Update action visibility based on role
    m_makeAdminAction->setVisible(info.role == 0);
    m_removeAdminAction->setVisible(info.role == 1);
    m_kickAction->setEnabled(info.role != 2); // Can't kick owner

    m_memberContextMenu->exec(m_memberList->mapToGlobal(pos));
}

void GroupMemberManagementWidget::onMakeAdminTriggered() {
    QList<QListWidgetItem*> selectedItems = m_memberList->selectedItems();
    if (selectedItems.isEmpty()) return;

    for (QListWidgetItem* item : selectedItems) {
        QString memberId = item->data(Qt::UserRole).toString();
        if (m_memberMap.contains(memberId)) {
            MemberInfo& info = m_memberMap[memberId];
            if (info.role == 0) {
                info.role = 1;
                if (m_manager) {
                    // In real implementation, would call manager to update role
                }
                emit memberRoleChanged(memberId, 1);
            }
        }
    }
    refreshMemberList();
}

void GroupMemberManagementWidget::onRemoveAdminTriggered() {
    QList<QListWidgetItem*> selectedItems = m_memberList->selectedItems();
    if (selectedItems.isEmpty()) return;

    for (QListWidgetItem* item : selectedItems) {
        QString memberId = item->data(Qt::UserRole).toString();
        if (m_memberMap.contains(memberId)) {
            MemberInfo& info = m_memberMap[memberId];
            if (info.role == 1) {
                info.role = 0;
                if (m_manager) {
                    // In real implementation, would call manager to update role
                }
                emit memberRoleChanged(memberId, 0);
            }
        }
    }
    refreshMemberList();
}

void GroupMemberManagementWidget::onKickMemberTriggered() {
    QList<QListWidgetItem*> selectedItems = m_memberList->selectedItems();
    if (selectedItems.isEmpty()) return;

    QMessageBox::StandardButton reply = QMessageBox::question(this,
        tr("移出成员"), 
        tr("确定要将选中的 %1 位成员移出群聊吗？").arg(selectedItems.size()),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        for (QListWidgetItem* item : selectedItems) {
            QString memberId = item->data(Qt::UserRole).toString();
            if (m_memberMap.contains(memberId)) {
                MemberInfo& info = m_memberMap[memberId];
                if (info.role != 2) { // Can't kick owner
                    if (m_manager) {
                        // In real implementation, would call manager to remove member
                    }
                    emit memberKicked(memberId);
                }
            }
        }
        refreshMemberList();
    }
}

void GroupMemberManagementWidget::onSendMessageTriggered() {
    QListWidgetItem* item = m_memberList->currentItem();
    if (!item) return;

    QString memberId = item->data(Qt::UserRole).toString();
    QString memberName = item->data(Qt::UserRole + 1).toString();
    emit messageToMember(memberId, memberName);
}

void GroupMemberManagementWidget::onViewProfileTriggered() {
    QListWidgetItem* item = m_memberList->currentItem();
    if (!item) return;

    QString memberId = item->data(Qt::UserRole).toString();
    if (!m_memberMap.contains(memberId)) return;

    const MemberInfo& info = m_memberMap[memberId];

    QDialog dialog(this);
    dialog.setWindowTitle(tr("成员资料 - %1").arg(info.name));
    dialog.setFixedSize(320, 300);
    dialog.setStyleSheet("QDialog { background-color: #2E2E2E; border-radius: 12px; }");

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    // Avatar
    QLabel* avatar = new QLabel(&dialog);
    avatar->setFixedSize(80, 80);
    avatar->setStyleSheet(QString(
        "background-color: %1; color: white; border-radius: 40px; font-weight: bold; font-size: 28px;")
        .arg(info.avatarColor));
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setText(getAvatarLetter(info.name));
    layout->addWidget(avatar, 0, Qt::AlignCenter);

    // Name
    QLabel* nameLabel = new QLabel(info.name, &dialog);
    nameLabel->setStyleSheet("color: white; font-size: 18px; font-weight: bold;");
    nameLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(nameLabel);

    // Role
    QString roleText = info.role == 2 ? tr("群主") : (info.role == 1 ? tr("管理员") : tr("成员"));
    QString roleColor = info.role == 2 ? "#FB8C00" : (info.role == 1 ? "#07C160" : "#1E88E5");
    QLabel* roleLabel = new QLabel(roleText, &dialog);
    roleLabel->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: 600;").arg(roleColor));
    roleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(roleLabel);

    // Status
    QLabel* statusLabel = new QLabel(info.online ? tr("● 在线") : tr("○ 离线"), &dialog);
    statusLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(info.online ? "#07C160" : "#757575"));
    statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(statusLabel);

    // IP
    if (!info.ip.isEmpty()) {
        QLabel* ipLabel = new QLabel(tr("IP: %1").arg(info.ip), &dialog);
        ipLabel->setStyleSheet("color: #888888; font-size: 12px;");
        ipLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(ipLabel);
    }

    layout->addStretch();

    // Close button
    QPushButton* closeBtn = new QPushButton(tr("关闭"), &dialog);
    closeBtn->setStyleSheet("QPushButton { background-color: #3A3A3A; color: white; border: none; border-radius: 6px; padding: 10px 24px; font-size: 13px; }"
                            "QPushButton:hover { background-color: #4A4A4A; }");
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(closeBtn, 0, Qt::AlignCenter);

    dialog.exec();
}

void GroupMemberManagementWidget::onInviteMembersTriggered() {
    if (!m_manager) return;

    // Get available contacts not in group
    QList<IPMsgDevice> devices = m_manager->getOnlineDevices();
    QList<QString> availableContacts;
    QMap<QString, QString> idToName;
    for (const IPMsgDevice& device : devices) {
        if (!m_currentGroup.memberIds.contains(device.id)) {
            availableContacts.append(device.id);
            idToName[device.id] = device.name;
        }
    }

    if (availableContacts.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("没有可邀请的在线联系人"));
        return;
    }

    QStringList items;
    for (const QString& id : availableContacts) {
        items.append(idToName[id]);
    }

    bool ok;
    QString selected = QInputDialog::getItem(this, tr("邀请成员"), tr("选择要邀请的联系人"), items, 0, false, &ok);
    if (ok && !selected.isEmpty()) {
        QString memberId = idToName[selected];
        // In real implementation, would call manager to invite
        emit memberInvited(memberId);
        QMessageBox::information(this, tr("成功"), tr("已邀请 %1").arg(selected));
    }
}

void GroupMemberManagementWidget::applySearchFilter(const QString& filter) {
    m_searchFilter = filter.trimmed().toLower();
    
    for (int i = 0; i < m_memberList->count(); ++i) {
        QListWidgetItem* item = m_memberList->item(i);
        if (!item) continue;

        QString memberName = item->data(Qt::UserRole + 1).toString().toLower();
        bool match = filter.isEmpty() || memberName.contains(m_searchFilter);
        item->setHidden(!match);
    }
}

void GroupMemberManagementWidget::setSearchFilter(const QString& filter) {
    if (m_searchInput) {
        m_searchInput->setText(filter);
    }
    applySearchFilter(filter);
}

QString GroupMemberManagementWidget::getAvatarColor(const QString& name) const {
    QStringList colors = {"#1E88E5", "#43A047", "#E53935", "#FB8C00", "#8E24AA", "#00ACC1", "#F4511E", "#3949AB"};
    int hash = qHash(name) % colors.size();
    return colors[hash];
}

QString GroupMemberManagementWidget::getAvatarLetter(const QString& name) const {
    if (name.isEmpty()) return "?";
    return QString(name.at(0)).toUpper();
}

} // namespace xrk