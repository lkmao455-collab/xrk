#include "group_todo_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QMessageBox>

namespace xrk {

GroupTodoWidget::GroupTodoWidget(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setFixedSize(420, 520);
    setStyleSheet("background-color: #1e1e2e;");
    setupUI();
}

GroupTodoWidget::~GroupTodoWidget() {}

void GroupTodoWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(10);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    m_titleLabel = new QLabel(tr("Group Todo List"), this);
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

    QWidget* createSection = new QWidget(this);
    QVBoxLayout* createLayout = new QVBoxLayout(createSection);
    createLayout->setContentsMargins(0, 0, 0, 0);
    createLayout->setSpacing(6);

    m_titleEdit = new QLineEdit(createSection);
    m_titleEdit->setPlaceholderText(tr("Todo title..."));
    m_titleEdit->setStyleSheet("QLineEdit { background: #2a2a3e; border: 1px solid #3a3a5c; "
                               "border-radius: 4px; padding: 6px; color: #d4d4d4; font-size: 12px; }");
    createLayout->addWidget(m_titleEdit);

    m_descEdit = new QLineEdit(createSection);
    m_descEdit->setPlaceholderText(tr("Description (optional)..."));
    m_descEdit->setStyleSheet("QLineEdit { background: #2a2a3e; border: 1px solid #3a3a5c; "
                              "border-radius: 4px; padding: 6px; color: #d4d4d4; font-size: 12px; }");
    createLayout->addWidget(m_descEdit);

    QHBoxLayout* createBtnLayout = new QHBoxLayout();
    m_priorityCombo = new QComboBox(createSection);
    m_priorityCombo->addItems({tr("Low"), tr("Medium"), tr("High")});
    m_priorityCombo->setStyleSheet("QComboBox { background: #2a2a3e; color: #d4d4d4; "
                                   "border: 1px solid #3a3a5c; border-radius: 4px; padding: 4px 8px; }");
    m_createBtn = new QPushButton(tr("Add Todo"), createSection);
    m_createBtn->setStyleSheet("QPushButton { background: #0e639c; color: white; padding: 6px 12px; "
                               "border-radius: 4px; font-size: 12px; }"
                               "QPushButton:hover { background: #1177bb; }");
    createBtnLayout->addWidget(m_priorityCombo);
    createBtnLayout->addStretch();
    createBtnLayout->addWidget(m_createBtn);
    createLayout->addLayout(createBtnLayout);
    mainLayout->addWidget(createSection);

    m_todoList = new QListWidget(this);
    m_todoList->setStyleSheet(
        "QListWidget { background: #2a2a3e; border: none; border-radius: 6px; color: #d4d4d4; }"
        "QListWidget::item { padding: 8px; border-bottom: 1px solid #3a3a5c; }"
        "QListWidget::item:hover { background: #3a3a5c; }");
    connect(m_todoList, &QListWidget::itemClicked, this, &GroupTodoWidget::onItemClicked);
    mainLayout->addWidget(m_todoList);

    QHBoxLayout* bottomLayout = new QHBoxLayout();
    QLabel* hintLabel = new QLabel(tr("Click to cycle status: Pending -> In Progress -> Completed"), this);
    hintLabel->setStyleSheet("color: #666; font-size: 10px;");
    m_deleteBtn = new QPushButton(tr("Delete"), this);
    m_deleteBtn->setStyleSheet("QPushButton { background: #c53030; color: white; padding: 6px 12px; "
                               "border-radius: 4px; font-size: 12px; }"
                               "QPushButton:hover { background: #e04040; }");
    bottomLayout->addWidget(hintLabel);
    bottomLayout->addStretch();
    bottomLayout->addWidget(m_deleteBtn);
    mainLayout->addLayout(bottomLayout);

    connect(m_createBtn, &QPushButton::clicked, this, &GroupTodoWidget::onCreateClicked);
    connect(m_deleteBtn, &QPushButton::clicked, this, &GroupTodoWidget::onDeleteClicked);
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() { emit closed(); hide(); });
}

void GroupTodoWidget::setGroupId(const QString& groupId) { m_groupId = groupId; }
void GroupTodoWidget::setGroupName(const QString& groupName) {
    m_groupName = groupName;
    m_groupNameLabel->setText(tr("Group: %1").arg(groupName));
}
void GroupTodoWidget::setCurrentUserId(const QString& userId) { m_currentUserId = userId; }

void GroupTodoWidget::addTodo(const GroupTodo& todo) {
    m_todos.append(todo);
    refreshList();
}

void GroupTodoWidget::updateTodo(const GroupTodo& todo) {
    for (int i = 0; i < m_todos.size(); ++i) {
        if (m_todos[i].todoId == todo.todoId) {
            m_todos[i] = todo;
            break;
        }
    }
    refreshList();
}

void GroupTodoWidget::setTodos(const QList<GroupTodo>& todos) {
    m_todos = todos;
    refreshList();
}

void GroupTodoWidget::refreshList() {
    m_todoList->clear();
    for (const auto& t : m_todos) {
        QString statusIcon = t.status == 0 ? "\xe2\x9a\x94" : (t.status == 1 ? "\xe2\x9a\x99" : "\xe2\x9c\x85");
        QString priorityIcon = t.priority == 2 ? "\xf0\x9f\x94\xa5" : (t.priority == 1 ? "\xf0\x9f\x94\xb5" : "");
        QString text = QString("%1 <b>%2</b> %3<br><span style='color:#888;'>%4 | %5</span>")
            .arg(statusIcon)
            .arg(t.title.toHtmlEscaped())
            .arg(priorityIcon)
            .arg(statusText(t.status))
            .arg(t.assigneeName.isEmpty() ? t.creatorName.toHtmlEscaped() : t.assigneeName.toHtmlEscaped());

        QListWidgetItem* item = new QListWidgetItem(text, m_todoList);
        item->setData(Qt::UserRole, t.todoId);
        item->setData(Qt::UserRole + 1, t.status);
        item->setSizeHint(QSize(0, 48));

        QColor color = statusColor(t.status);
        item->setForeground(color);
    }
}

void GroupTodoWidget::onCreateClicked() {
    QString title = m_titleEdit->text().trimmed();
    if (title.isEmpty()) return;

    GroupTodo todo;
    todo.groupId = m_groupId;
    todo.todoId = QString("todo_%1_%2").arg(m_groupId).arg(QDateTime::currentMSecsSinceEpoch());
    todo.title = title;
    todo.description = m_descEdit->text();
    todo.priority = m_priorityCombo->currentIndex();
    todo.creatorId = m_currentUserId;
    todo.timestamp = QDateTime::currentMSecsSinceEpoch();

    emit todoCreated(m_groupId, todo);
    m_titleEdit->clear();
    m_descEdit->clear();
}

void GroupTodoWidget::onItemClicked(QListWidgetItem* item) {
    QString todoId = item->data(Qt::UserRole).toString();
    int currentStatus = item->data(Qt::UserRole + 1).toInt();
    int newStatus = (currentStatus + 1) % 3;
    emit todoStatusChanged(m_groupId, todoId, newStatus);
}

void GroupTodoWidget::onDeleteClicked() {
    QListWidgetItem* item = m_todoList->currentItem();
    if (!item) return;
    QString todoId = item->data(Qt::UserRole).toString();
    emit todoDeleted(m_groupId, todoId);
}

QString GroupTodoWidget::statusText(int status) const {
    switch (status) {
        case 0: return tr("Pending");
        case 1: return tr("In Progress");
        case 2: return tr("Completed");
        default: return tr("Unknown");
    }
}

QString GroupTodoWidget::priorityText(int priority) const {
    switch (priority) {
        case 0: return tr("Low");
        case 1: return tr("Medium");
        case 2: return tr("High");
        default: return tr("Low");
    }
}

QColor GroupTodoWidget::statusColor(int status) const {
    switch (status) {
        case 0: return QColor("#d4d4d4");
        case 1: return QColor("#4ec9b0");
        case 2: return QColor("#6a9955");
        default: return QColor("#d4d4d4");
    }
}

} // namespace xrk
