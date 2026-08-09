#include "notification_center_widget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QFont>

namespace xrk {

NotificationCenterWidget::NotificationCenterWidget(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

NotificationCenterWidget::~NotificationCenterWidget() {
}

void NotificationCenterWidget::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // Header
    auto* headerLayout = new QHBoxLayout();
    auto* titleLabel = new QLabel(tr("通知中心"));
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #E0E0E0;");
    headerLayout->addWidget(titleLabel);

    m_unreadBadge = new QLabel("0");
    m_unreadBadge->setStyleSheet(
        "background-color: #f44336; color: white; border-radius: 10px; "
        "padding: 2px 6px; font-size: 11px; font-weight: bold;");
    m_unreadBadge->setAlignment(Qt::AlignCenter);
    m_unreadBadge->setMinimumSize(20, 20);
    m_unreadBadge->hide();
    headerLayout->addWidget(m_unreadBadge);

    headerLayout->addStretch(1);

    m_markReadButton = new QPushButton(tr("全部已读"));
    m_markReadButton->setStyleSheet(
        "QPushButton { background: transparent; color: #4a9eff; border: none; font-size: 12px; }"
        "QPushButton:hover { color: #6ab4ff; }");
    connect(m_markReadButton, &QPushButton::clicked, this, &NotificationCenterWidget::onMarkAllRead);
    headerLayout->addWidget(m_markReadButton);

    m_clearButton = new QPushButton(tr("清空"));
    m_clearButton->setStyleSheet(
        "QPushButton { background: transparent; color: #ff6b6b; border: none; font-size: 12px; }"
        "QPushButton:hover { color: #ff8a8a; }");
    connect(m_clearButton, &QPushButton::clicked, this, &NotificationCenterWidget::onClearClicked);
    headerLayout->addWidget(m_clearButton);

    mainLayout->addLayout(headerLayout);

    // Notification list
    m_listWidget = new QListWidget();
    m_listWidget->setStyleSheet(
        "QListWidget { background: transparent; border: none; outline: none; }"
        "QListWidget::item { background: rgba(255,255,255,0.05); border-radius: 6px; "
        "padding: 8px; margin-bottom: 4px; }"
        "QListWidget::item:hover { background: rgba(255,255,255,0.1); }"
        "QListWidget::item:selected { background: rgba(74,158,255,0.2); }");
    m_listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(m_listWidget, &QListWidget::itemClicked, this, &NotificationCenterWidget::onItemClicked);

    mainLayout->addWidget(m_listWidget);

    setMinimumSize(300, 400);
    setStyleSheet("background-color: #2a2a2a; border-radius: 8px;");
}

void NotificationCenterWidget::addNotification(const QString& title, const QString& message, const QString& type) {
    Notification notif;
    notif.title = title;
    notif.message = message;
    notif.type = type;
    notif.timestamp = QDateTime::currentDateTime();
    notif.read = false;

    m_notifications.prepend(notif);
    m_unreadCount++;

    // Create list item
    QString icon = getNotificationIcon(type);
    QColor color = getNotificationColor(type);

    QString timeStr = notif.timestamp.toString("HH:mm");
    QString html = QString(
        "<div style='color: %1; font-size: 14px; font-weight: bold;'>%2 %3</div>"
        "<div style='color: #ccc; font-size: 12px; margin-top: 2px;'>%4</div>"
        "<div style='color: #888; font-size: 10px; margin-top: 2px;'>%5</div>")
        .arg(color.name(), icon, title.toHtmlEscaped(),
             message.toHtmlEscaped(), timeStr);

    auto* item = new QListWidgetItem();
    item->setData(Qt::UserRole, m_notifications.size() - 1);
    item->setSizeHint(QSize(0, 60));

    auto* label = new QLabel(html);
    label->setWordWrap(true);
    label->setContentsMargins(4, 4, 4, 4);

    m_listWidget->insertItem(0, item);
    m_listWidget->setItemWidget(item, label);

    updateUnreadBadge();
}

void NotificationCenterWidget::onItemClicked(QListWidgetItem* item) {
    int index = item->data(Qt::UserRole).toInt();
    if (index >= 0 && index < m_notifications.size()) {
        if (!m_notifications[index].read) {
            m_notifications[index].read = true;
            m_unreadCount = qMax(0, m_unreadCount - 1);
            updateUnreadBadge();
        }
        emit notificationClicked(index);
    }
}

void NotificationCenterWidget::onClearClicked() {
    m_listWidget->clear();
    m_notifications.clear();
    m_unreadCount = 0;
    updateUnreadBadge();
    emit cleared();
}

void NotificationCenterWidget::onMarkAllRead() {
    for (auto& notif : m_notifications) {
        notif.read = true;
    }
    m_unreadCount = 0;
    updateUnreadBadge();
}

void NotificationCenterWidget::clearAll() {
    onClearClicked();
}

void NotificationCenterWidget::updateUnreadBadge() {
    if (m_unreadCount > 0) {
        m_unreadBadge->setText(QString::number(m_unreadCount));
        m_unreadBadge->show();
    } else {
        m_unreadBadge->hide();
    }
}

QString NotificationCenterWidget::getNotificationIcon(const QString& type) const {
    if (type == "success") return QString::fromUtf8("\u2705");
    if (type == "warning") return QString::fromUtf8("\u26a0\ufe0f");
    if (type == "error") return QString::fromUtf8("\u274c");
    return QString::fromUtf8("\u2139\ufe0f");
}

QColor NotificationCenterWidget::getNotificationColor(const QString& type) const {
    if (type == "success") return QColor("#4caf50");
    if (type == "warning") return QColor("#ff9800");
    if (type == "error") return QColor("#f44336");
    return QColor("#2196f3");
}

} // namespace xrk
