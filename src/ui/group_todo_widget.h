#pragma once

#include <QWidget>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QVBoxLayout>
#include <QMap>
#include <QDateTime>
#include "core/types.h"

namespace xrk {

class GroupTodoWidget : public QWidget {
    Q_OBJECT
public:
    explicit GroupTodoWidget(QWidget* parent = nullptr);
    ~GroupTodoWidget();

    void setGroupId(const QString& groupId);
    void setGroupName(const QString& groupName);
    void setCurrentUserId(const QString& userId);
    void addTodo(const GroupTodo& todo);
    void updateTodo(const GroupTodo& todo);
    void setTodos(const QList<GroupTodo>& todos);

signals:
    void todoCreated(const QString& groupId, const GroupTodo& todo);
    void todoStatusChanged(const QString& groupId, const QString& todoId, int newStatus);
    void todoDeleted(const QString& groupId, const QString& todoId);
    void closed();

private slots:
    void onCreateClicked();
    void onDeleteClicked();
    void onItemClicked(QListWidgetItem* item);

private:
    void setupUI();
    void refreshList();
    QString statusText(int status) const;
    QString priorityText(int priority) const;
    QColor statusColor(int status) const;

    QString m_groupId;
    QString m_groupName;
    QString m_currentUserId;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_groupNameLabel = nullptr;
    QListWidget* m_todoList = nullptr;
    QLineEdit* m_titleEdit = nullptr;
    QLineEdit* m_descEdit = nullptr;
    QComboBox* m_priorityCombo = nullptr;
    QPushButton* m_createBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;

    QList<GroupTodo> m_todos;
};

} // namespace xrk
