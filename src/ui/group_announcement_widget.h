#pragma once

#include <QWidget>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QTimer>
#include <QMap>
#include "core/types.h"

namespace xrk {

class GroupAnnouncementWidget : public QWidget {
    Q_OBJECT
public:
    explicit GroupAnnouncementWidget(QWidget* parent = nullptr);
    ~GroupAnnouncementWidget();

    void setGroupId(const QString& groupId);
    void setGroupName(const QString& groupName);
    void addAnnouncement(const GroupAnnouncement& announcement);
    void setAnnouncements(const QList<GroupAnnouncement>& announcements);
    void setCurrentUserId(const QString& userId);
    void setCanPost(bool canPost);

signals:
    void announcementPosted(const QString& groupId, const QString& text);
    void announcementDismissed(const QString& groupId);
    void closed();

private slots:
    void onPostClicked();
    void onDismissClicked();
    void onAnnouncementDoubleClicked(QListWidgetItem* item);

private:
    void setupUI();
    void refreshList();

    QString m_groupId;
    QString m_groupName;
    QString m_currentUserId;
    bool m_canPost = false;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_groupNameLabel = nullptr;
    QListWidget* m_announcementList = nullptr;
    QLineEdit* m_inputEdit = nullptr;
    QPushButton* m_postBtn = nullptr;
    QPushButton* m_dismissBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;

    QList<GroupAnnouncement> m_announcements;
};

} // namespace xrk
