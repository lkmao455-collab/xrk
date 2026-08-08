#include "group_announcement_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QFont>
#include <QDateTime>

namespace xrk {

GroupAnnouncementWidget::GroupAnnouncementWidget(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setFixedSize(400, 480);
    setStyleSheet("background-color: #1e1e2e;");
    setupUI();
}

GroupAnnouncementWidget::~GroupAnnouncementWidget() {}

void GroupAnnouncementWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    m_titleLabel = new QLabel(tr("Group Announcements"), this);
    m_titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #e0e0e0;");
    m_closeBtn = new QPushButton(tr("X"), this);
    m_closeBtn->setFixedSize(28, 28);
    m_closeBtn->setStyleSheet("QPushButton { border: none; color: #888; font-size: 16px; }"
                              "QPushButton:hover { color: #d4d4d4; }");
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(headerLayout);

    m_groupNameLabel = new QLabel(this);
    m_groupNameLabel->setStyleSheet("color: #4ec9b0; font-size: 13px;");
    mainLayout->addWidget(m_groupNameLabel);

    QFrame* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #3a3a5c;");
    mainLayout->addWidget(sep);

    m_announcementList = new QListWidget(this);
    m_announcementList->setStyleSheet(
        "QListWidget { background: #2a2a3e; border: none; border-radius: 6px; color: #d4d4d4; }"
        "QListWidget::item { padding: 10px; border-bottom: 1px solid #3a3a5c; }"
        "QListWidget::item:hover { background: #3a3a5c; }");
    connect(m_announcementList, &QListWidget::itemDoubleClicked, this, &GroupAnnouncementWidget::onAnnouncementDoubleClicked);
    mainLayout->addWidget(m_announcementList);

    m_inputEdit = new QLineEdit(this);
    m_inputEdit->setPlaceholderText(tr("Post a new announcement..."));
    m_inputEdit->setStyleSheet("QLineEdit { background: #2a2a3e; border: 1px solid #3a3a5c; "
                               "border-radius: 4px; padding: 8px; color: #d4d4d4; font-size: 13px; }");
    mainLayout->addWidget(m_inputEdit);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_postBtn = new QPushButton(tr("Post"), this);
    m_dismissBtn = new QPushButton(tr("Dismiss All"), this);
    m_postBtn->setStyleSheet("QPushButton { background: #0e639c; color: white; padding: 6px 16px; "
                             "border-radius: 4px; font-size: 12px; }"
                             "QPushButton:hover { background: #1177bb; }");
    m_dismissBtn->setStyleSheet("QPushButton { background: #555; color: white; padding: 6px 16px; "
                                "border-radius: 4px; font-size: 12px; }"
                                "QPushButton:hover { background: #666; }");
    btnLayout->addWidget(m_postBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_dismissBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_postBtn, &QPushButton::clicked, this, &GroupAnnouncementWidget::onPostClicked);
    connect(m_dismissBtn, &QPushButton::clicked, this, &GroupAnnouncementWidget::onDismissClicked);
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() { emit closed(); hide(); });
}

void GroupAnnouncementWidget::setGroupId(const QString& groupId) { m_groupId = groupId; }
void GroupAnnouncementWidget::setGroupName(const QString& groupName) {
    m_groupName = groupName;
    m_groupNameLabel->setText(tr("Group: %1").arg(groupName));
}
void GroupAnnouncementWidget::setCurrentUserId(const QString& userId) { m_currentUserId = userId; }
void GroupAnnouncementWidget::setCanPost(bool canPost) {
    m_canPost = canPost;
    m_inputEdit->setVisible(canPost);
    m_postBtn->setVisible(canPost);
}

void GroupAnnouncementWidget::addAnnouncement(const GroupAnnouncement& announcement) {
    m_announcements.prepend(announcement);
    refreshList();
}

void GroupAnnouncementWidget::setAnnouncements(const QList<GroupAnnouncement>& announcements) {
    m_announcements = announcements;
    refreshList();
}

void GroupAnnouncementWidget::refreshList() {
    m_announcementList->clear();
    for (const auto& a : m_announcements) {
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(a.timestamp);
        QString text = QString("<b>%1</b><br><span style='color:#888;'>%2 - %3</span><br>%4")
            .arg(a.announcerName.toHtmlEscaped())
            .arg(dt.toString("MM-dd HH:mm"))
            .arg(a.groupName.toHtmlEscaped())
            .arg(a.announcement.toHtmlEscaped());
        QListWidgetItem* item = new QListWidgetItem(m_announcementList);
        item->setText(text);
        item->setData(Qt::UserRole, a.announcement);
    }
}

void GroupAnnouncementWidget::onPostClicked() {
    QString text = m_inputEdit->text().trimmed();
    if (text.isEmpty()) return;
    emit announcementPosted(m_groupId, text);
    m_inputEdit->clear();
}

void GroupAnnouncementWidget::onDismissClicked() {
    m_announcements.clear();
    m_announcementList->clear();
    emit announcementDismissed(m_groupId);
}

void GroupAnnouncementWidget::onAnnouncementDoubleClicked(QListWidgetItem*) {}

} // namespace xrk
