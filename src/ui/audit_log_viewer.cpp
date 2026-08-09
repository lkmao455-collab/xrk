#include "audit_log_viewer.h"
#include "core/audit_logger.h"
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QGroupBox>

namespace xrk {

AuditLogViewer::AuditLogViewer(AuditLogger* logger, QWidget* parent)
    : QDialog(parent)
    , m_logger(logger)
{
    setWindowTitle(tr("审计日志"));
    setMinimumSize(700, 450);
    setupUI();
    if (m_logger) {
        connect(m_logger, &AuditLogger::entryAdded, this, &AuditLogViewer::onEntryAdded);
    }
    refreshLogs();
}

AuditLogViewer::~AuditLogViewer() {}

void AuditLogViewer::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);

    // Filter bar
    auto* filterLayout = new QHBoxLayout();
    m_typeFilter = new QComboBox(this);
    m_typeFilter->addItems({tr("全部"), tr("连接"), tr("认证"), tr("会话"), tr("操作"), tr("错误")});
    connect(m_typeFilter, &QComboBox::currentIndexChanged, this, &AuditLogViewer::onFilterChanged);

    m_fromDate = new QDateTimeEdit(QDateTime::currentDateTime().addDays(-7), this);
    m_fromDate->setDisplayFormat("yyyy-MM-dd HH:mm");
    m_fromDate->setCalendarPopup(true);
    connect(m_fromDate, &QDateTimeEdit::dateTimeChanged, this, &AuditLogViewer::onFilterChanged);

    m_toDate = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    m_toDate->setDisplayFormat("yyyy-MM-dd HH:mm");
    m_toDate->setCalendarPopup(true);
    connect(m_toDate, &QDateTimeEdit::dateTimeChanged, this, &AuditLogViewer::onFilterChanged);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("搜索..."));
    connect(m_searchEdit, &QLineEdit::textChanged, this, &AuditLogViewer::onFilterChanged);

    filterLayout->addWidget(new QLabel(tr("类型:"), this));
    filterLayout->addWidget(m_typeFilter);
    filterLayout->addWidget(new QLabel(tr("从:"), this));
    filterLayout->addWidget(m_fromDate);
    filterLayout->addWidget(new QLabel(tr("到:"), this));
    filterLayout->addWidget(m_toDate);
    filterLayout->addWidget(m_searchEdit);
    mainLayout->addLayout(filterLayout);

    // Table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({tr("时间"), tr("类型"), tr("客户端"), tr("事件"), tr("详情")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    mainLayout->addWidget(m_table, 1);

    // Bottom bar
    auto* bottomLayout = new QHBoxLayout();
    m_countLabel = new QLabel(this);
    m_refreshBtn = new QPushButton(tr("刷新"), this);
    m_exportBtn = new QPushButton(tr("导出"), this);
    m_clearBtn = new QPushButton(tr("清除旧日志"), this);
    connect(m_refreshBtn, &QPushButton::clicked, this, &AuditLogViewer::refreshLogs);
    connect(m_exportBtn, &QPushButton::clicked, this, &AuditLogViewer::onExportClicked);
    connect(m_clearBtn, &QPushButton::clicked, this, &AuditLogViewer::onClearClicked);
    bottomLayout->addWidget(m_countLabel);
    bottomLayout->addStretch();
    bottomLayout->addWidget(m_refreshBtn);
    bottomLayout->addWidget(m_exportBtn);
    bottomLayout->addWidget(m_clearBtn);
    mainLayout->addLayout(bottomLayout);

    applyDarkTheme();
}

void AuditLogViewer::refreshLogs() {
    if (!m_logger) return;
    QDateTime from = m_fromDate->dateTime();
    QDateTime to = m_toDate->dateTime();
    QJsonArray entries = m_logger->entriesSince(from, 5000);

    // Filter by date range
    QJsonArray filtered;
    qint64 fromMs = from.toMSecsSinceEpoch();
    qint64 toMs = to.toMSecsSinceEpoch();
    for (const auto& val : entries) {
        QJsonObject obj = val.toObject();
        qint64 ts = obj["timestamp"].toVariant().toLongLong();
        if (ts >= fromMs && ts <= toMs) {
            filtered.append(obj);
        }
    }

    // Apply type and search filters
    QString typeFilter = m_typeFilter->currentText();
    QString searchText = m_searchEdit->text().toLower();
    QJsonArray result;
    for (const auto& val : filtered) {
        QJsonObject obj = val.toObject();
        if (typeFilter != tr("全部")) {
            QString type = obj["type"].toString();
            if (typeFilter == tr("连接") && type != "connection") continue;
            if (typeFilter == tr("认证") && type != "auth") continue;
            if (typeFilter == tr("会话") && type != "session") continue;
            if (typeFilter == tr("操作") && type != "operation") continue;
            if (typeFilter == tr("错误") && type != "error") continue;
        }
        if (!searchText.isEmpty()) {
            QString json = QJsonDocument(obj).toJson().toLower();
            if (!json.contains(searchText)) continue;
        }
        result.append(obj);
    }

    populateTable(result);
}

void AuditLogViewer::populateTable(const QJsonArray& entries) {
    m_table->setRowCount(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        QJsonObject obj = entries[i].toObject();
        QDateTime ts = QDateTime::fromMSecsSinceEpoch(obj["timestamp"].toVariant().toLongLong());
        QString type = obj["type"].toString();
        QString client = obj["client_id"].toString();
        QString event = obj["event"].toString();
        QString details = obj["details"].toString();

        m_table->setItem(i, 0, new QTableWidgetItem(ts.toString("yyyy-MM-dd HH:mm:ss")));
        m_table->setItem(i, 1, new QTableWidgetItem(type));
        m_table->setItem(i, 2, new QTableWidgetItem(client));
        m_table->setItem(i, 3, new QTableWidgetItem(event));
        m_table->setItem(i, 4, new QTableWidgetItem(details));
    }
    m_countLabel->setText(tr("共 %1 条记录").arg(entries.size()));
}

void AuditLogViewer::onFilterChanged() {
    refreshLogs();
}

void AuditLogViewer::onExportClicked() {
    QString path = QFileDialog::getSaveFileName(this, tr("导出审计日志"), "audit_log.json", "JSON (*.json)");
    if (path.isEmpty()) return;

    QDateTime from = m_fromDate->dateTime();
    QJsonArray entries = m_logger->entriesSince(from, 10000);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(entries).toJson());
        QMessageBox::information(this, tr("导出成功"), tr("已导出 %1 条记录").arg(entries.size()));
    }
}

void AuditLogViewer::onClearClicked() {
    if (m_logger) {
        m_logger->clearOldLogs(30);
        refreshLogs();
    }
}

void AuditLogViewer::onEntryAdded(const QJsonObject& entry) {
    Q_UNUSED(entry);
    refreshLogs();
}

void AuditLogViewer::applyDarkTheme() {
    setStyleSheet(R"(
        AuditLogViewer { background: #2b2b2b; color: #ddd; }
        QTableWidget { background: #1e1e1e; color: #ddd; gridline-color: #444; border: 1px solid #444; }
        QHeaderView::section { background: #3a3a3a; color: #ddd; border: 1px solid #444; padding: 4px; }
        QComboBox, QLineEdit, QDateTimeEdit { background: #3a3a3a; color: #ddd; border: 1px solid #555; border-radius: 3px; padding: 3px; }
        QPushButton { background: #3a3a3a; color: #ddd; border: 1px solid #555; border-radius: 4px; padding: 5px 12px; }
        QPushButton:hover { background: #4a4a4a; }
    )");
}

} // namespace xrk
