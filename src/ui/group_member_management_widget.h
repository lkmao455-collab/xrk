#pragma once

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QTimer>
#include <QMap>
#include <QColor>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QDateTime>
#include <QProgressBar>

#include "app/ipmsg_manager.h"

namespace xrk {

class GroupMemberManagementWidget : public QWidget {
    Q_OBJECT

public:
    explicit GroupMemberManagementWidget(QWidget* parent = nullptr);
    ~GroupMemberManagementWidget();

    void setGroup(const IPMsgGroup& group);
    void setManager(IPMsgManager* manager);
    void refreshMemberList();
    void setSearchFilter(const QString& filter);

signals:
    void memberRoleChanged(const QString& memberId, int newRole);
    void memberKicked(const QString& memberId);
    void memberInvited(const QString& memberId);
    void messageToMember(const QString& memberId, const QString& memberName);

private:
    struct MemberInfo {
        QString id;
        QString name;
        QString ip;
        bool online = false;
        int role = 0; // 0: member, 1: admin, 2: owner
        qint64 lastSeen = 0;
        QString avatarColor;
        bool selected = false;
    };

    void setupUI();
    void createMemberList();
    void updateMemberList();
    void setupContextMenu();
    void createMemberItem(const MemberInfo& member, int index);
    void animateItemUpdate(QListWidgetItem* item);
    void applySearchFilter(const QString& filter);
    void onContextMenuRequested(const QPoint& pos);
    void onMakeAdminTriggered();
    void onRemoveAdminTriggered();
    void onKickMemberTriggered();
    void onSendMessageTriggered();
    void onViewProfileTriggered();
    void onInviteMembersTriggered();

    QString getAvatarColor(const QString& name) const;
    QString getAvatarLetter(const QString& name) const;

    QVBoxLayout* m_mainLayout = nullptr;
    QWidget* m_headerWidget = nullptr;
    QLabel* m_groupNameLabel = nullptr;
    QLabel* m_memberCountLabel = nullptr;
    QLabel* m_onlineCountLabel = nullptr;
    QProgressBar* m_onlineProgress = nullptr;
    QLineEdit* m_searchInput = nullptr;
    QPushButton* m_inviteBtn = nullptr;
    QListWidget* m_memberList = nullptr;
    QWidget* m_emptyStateWidget = nullptr;

    QMenu* m_memberContextMenu = nullptr;
    QAction* m_makeAdminAction = nullptr;
    QAction* m_removeAdminAction = nullptr;
    QAction* m_kickAction = nullptr;
    QAction* m_sendMessageAction = nullptr;
    QAction* m_viewProfileAction = nullptr;

    IPMsgManager* m_manager = nullptr;
    IPMsgGroup m_currentGroup;
    QMap<QString, MemberInfo> m_memberMap;
    QString m_searchFilter;

    bool m_animationsEnabled = true;
    int m_animationDuration = 200;
};

} // namespace xrk