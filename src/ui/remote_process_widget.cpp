#include "remote_process_widget.h"
#include "app/remote_controller.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>

namespace xrk {

RemoteProcessWidget::RemoteProcessWidget(QWidget* parent) : QWidget(parent) {
    setupUI();
    applyDarkTheme();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        if (m_controller) m_controller->requestProcessList();
    });

    connect(m_refreshButton, &QPushButton::clicked, this, [this]() {
        if (m_controller) m_controller->requestProcessList();
    });
    connect(m_killButton, &QPushButton::clicked, this, &RemoteProcessWidget::onKillClicked);
    connect(m_startButton, &QPushButton::clicked, this, &RemoteProcessWidget::onStartClicked);
}

RemoteProcessWidget::~RemoteProcessWidget() {
    m_refreshTimer->stop();
}

void RemoteProcessWidget::setRemoteController(RemoteController* controller) {
    m_controller = controller;
}

void RemoteProcessWidget::setConnected(bool connected) {
    if (connected) {
        m_refreshTimer->start(3000);
        if (m_controller) m_controller->requestProcessList();
    } else {
        m_refreshTimer->stop();
        m_table->setRowCount(0);
        m_statusLabel->setText("未连接");
    }
}

void RemoteProcessWidget::refreshList(const ProcessListResponse& response) {
    if (!response.success) {
        m_statusLabel->setText("错误: " + response.errorMessage);
        return;
    }
    m_table->setRowCount(response.entries.size());
    for (int i = 0; i < response.entries.size(); ++i) {
        const ProcessEntry& e = response.entries.at(i);
        m_table->setItem(i, 0, new QTableWidgetItem(QString::number(e.pid)));
        m_table->setItem(i, 1, new QTableWidgetItem(e.name));
        m_table->setItem(i, 2, new QTableWidgetItem(formatBytes(e.memoryBytes)));
    }
    m_statusLabel->setText(QString("共 %1 个进程").arg(response.entries.size()));
}

void RemoteProcessWidget::onKillClicked() {
    int row = m_table->currentRow();
    if (row < 0) {
        QMessageBox::information(this, tr("结束进程"), tr("请先选中一个进程。"));
        return;
    }
    QTableWidgetItem* pidItem = m_table->item(row, 0);
    if (!pidItem) return;
    bool ok = false;
    qint64 pid = pidItem->text().toLongLong(&ok);
    if (!ok) return;

    QTableWidgetItem* nameItem = m_table->item(row, 1);
    QString name = nameItem ? nameItem->text() : QString::number(pid);
    if (QMessageBox::question(this, tr("结束进程"),
            tr("确定要结束进程 %1 (PID %2) 吗？").arg(name).arg(pid),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    if (m_controller) m_controller->requestKillProcess(pid);
}

void RemoteProcessWidget::onStartClicked() {
    QString cmd = m_startEdit->text().trimmed();
    if (cmd.isEmpty()) {
        QMessageBox::information(this, tr("启动进程"), tr("请输入要启动的命令。"));
        return;
    }
    if (m_controller) m_controller->requestStartProcess(cmd, QString());
    m_startEdit->clear();
}

void RemoteProcessWidget::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);

    // Toolbar
    auto* toolbar = new QHBoxLayout();
    m_refreshButton = new QPushButton(tr("刷新"), this);
    m_killButton = new QPushButton(tr("结束进程"), this);
    m_startEdit = new QLineEdit(this);
    m_startEdit->setPlaceholderText(tr("输入命令以启动新进程，如 notepad"));
    m_startButton = new QPushButton(tr("启动"), this);
    toolbar->addWidget(m_refreshButton);
    toolbar->addWidget(m_killButton);
    toolbar->addWidget(m_startEdit, 1);
    toolbar->addWidget(m_startButton);
    mainLayout->addLayout(toolbar);

    // Table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("PID"), tr("名称"), tr("内存")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setAlternatingRowColors(true);
    mainLayout->addWidget(m_table, 1);

    m_statusLabel = new QLabel(tr("未连接"), this);
    mainLayout->addWidget(m_statusLabel);
}

void RemoteProcessWidget::applyDarkTheme() {
    setStyleSheet(R"(
        QWidget { background-color: #1e1e2e; color: #cdd6f4; }
        QTableWidget { background-color: #181825; gridline-color: #313244;
            selection-background-color: #45475a; }
        QHeaderView::section { background-color: #313244; color: #cdd6f4;
            padding: 4px; border: none; }
        QPushButton { background-color: #89b4fa; color: #11111b; border-radius: 4px;
            padding: 5px 12px; font-weight: bold; }
        QPushButton:hover { background-color: #74c7ec; }
        QLineEdit { background-color: #181825; border: 1px solid #313244;
            border-radius: 4px; padding: 5px; }
    )");
}

QString RemoteProcessWidget::formatBytes(qint64 bytes) {
    if (bytes <= 0) return "--";
    const qint64 kb = 1024;
    const qint64 mb = kb * 1024;
    const qint64 gb = mb * 1024;
    if (bytes >= gb) return QString::number(bytes / double(gb), 'f', 2) + " GB";
    if (bytes >= mb) return QString::number(bytes / double(mb), 'f', 1) + " MB";
    return QString::number(bytes / double(kb), 'f', 0) + " KB";
}

} // namespace xrk
