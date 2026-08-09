#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QTimer>
#include <QDateTime>
#include <QScrollArea>

namespace xrk {

class NotificationCenterWidget : public QWidget {
    Q_OBJECT
public:
    struct Notification {
        QString title;
        QString message;
        QString type;       // "info", "warning", "error", "success"
        QDateTime timestamp;
        bool read = false;
    };

    explicit NotificationCenterWidget(QWidget* parent = nullptr);
    ~NotificationCenterWidget();

    void addNotification(const QString& title, const QString& message, const QString& type = "info");
    int unreadCount() const { return m_unreadCount; }
    void clearAll();

signals:
    void notificationClicked(int index);
    void cleared();

private slots:
    void onItemClicked(QListWidgetItem* item);
    void onClearClicked();
    void onMarkAllRead();

private:
    void setupUI();
    void updateUnreadBadge();
    QString getNotificationIcon(const QString& type) const;
    QColor getNotificationColor(const QString& type) const;

    QListWidget* m_listWidget = nullptr;
    QPushButton* m_clearButton = nullptr;
    QPushButton* m_markReadButton = nullptr;
    QLabel* m_unreadBadge = nullptr;
    QList<Notification> m_notifications;
    int m_unreadCount = 0;
};

} // namespace xrk
