#include "device_trust_widget.h"
#include "app/host.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QHBoxLayout>

namespace xrk {

DeviceTrustWidget::DeviceTrustWidget(Host* host, QWidget* parent)
    : QDialog(parent)
    , m_host(host)
{
    setWindowTitle(tr("受信任设备管理"));
    setMinimumSize(500, 350);
    setupUI();
    refreshTrustedList();
}

DeviceTrustWidget::~DeviceTrustWidget() {}

void DeviceTrustWidget::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);

    // Description
    auto* descLabel = new QLabel(tr("受信任的IP地址将跳过连接确认对话框，自动授权远程访问。"), this);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet("color: #aaa; font-size: 12px; margin-bottom: 8px;");
    mainLayout->addWidget(descLabel);

    // Add IP row
    auto* addLayout = new QHBoxLayout();
    m_ipInput = new QLineEdit(this);
    m_ipInput->setPlaceholderText(tr("输入IP地址 (例如: 192.168.1.100)"));
    addLayout->addWidget(m_ipInput, 1);
    m_addBtn = new QPushButton(tr("添加"), this);
    m_addBtn->setStyleSheet("background: #27ae60; color: white; border: none; border-radius: 4px; padding: 6px 16px;");
    connect(m_addBtn, &QPushButton::clicked, this, &DeviceTrustWidget::onAddTrustedIp);
    addLayout->addWidget(m_addBtn);
    mainLayout->addLayout(addLayout);

    // Table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({tr("IP 地址"), tr("状态")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    mainLayout->addWidget(m_table, 1);

    // Bottom bar
    auto* bottomLayout = new QHBoxLayout();
    m_countLabel = new QLabel(this);
    m_removeBtn = new QPushButton(tr("移除选中"), this);
    m_removeBtn->setStyleSheet("background: #e67e22; color: white; border: none; border-radius: 4px; padding: 5px 12px;");
    connect(m_removeBtn, &QPushButton::clicked, this, &DeviceTrustWidget::onRemoveTrustedIp);
    m_clearBtn = new QPushButton(tr("清除全部"), this);
    m_clearBtn->setStyleSheet("background: #e74c3c; color: white; border: none; border-radius: 4px; padding: 5px 12px;");
    connect(m_clearBtn, &QPushButton::clicked, this, &DeviceTrustWidget::onClearAll);
    bottomLayout->addWidget(m_countLabel);
    bottomLayout->addStretch();
    bottomLayout->addWidget(m_removeBtn);
    bottomLayout->addWidget(m_clearBtn);
    mainLayout->addLayout(bottomLayout);

    applyDarkTheme();
}

void DeviceTrustWidget::refreshTrustedList() {
    if (!m_host) return;
    QStringList ips = m_host->trustedIps();
    m_table->setRowCount(ips.size());
    for (int i = 0; i < ips.size(); ++i) {
        m_table->setItem(i, 0, new QTableWidgetItem(ips[i]));
        auto* statusItem = new QTableWidgetItem(tr("受信任"));
        statusItem->setForeground(QColor("#27ae60"));
        m_table->setItem(i, 1, statusItem);
    }
    m_countLabel->setText(tr("共 %1 个受信任IP").arg(ips.size()));
}

void DeviceTrustWidget::onAddTrustedIp() {
    if (!m_host) return;
    QString ip = m_ipInput->text().trimmed();
    if (ip.isEmpty()) return;

    // Validate IP
    QHostAddress addr(ip);
    if (addr.protocol() != QAbstractSocket::IPv4Protocol && addr.protocol() != QAbstractSocket::IPv6Protocol) {
        QMessageBox::warning(this, tr("无效IP"), tr("请输入有效的IP地址"));
        return;
    }

    m_host->addTrustedIp(ip);
    m_ipInput->clear();
    refreshTrustedList();
}

void DeviceTrustWidget::onRemoveTrustedIp() {
    if (!m_host) return;
    int row = m_table->currentRow();
    if (row < 0) return;
    QString ip = m_table->item(row, 0)->text();
    m_host->removeTrustedIp(ip);
    refreshTrustedList();
}

void DeviceTrustWidget::onClearAll() {
    if (!m_host) return;
    auto result = QMessageBox::question(this, tr("确认"), tr("确定要清除所有受信任IP吗?"));
    if (result == QMessageBox::Yes) {
        m_host->clearTrustedIps();
        refreshTrustedList();
    }
}

void DeviceTrustWidget::applyDarkTheme() {
    setStyleSheet(R"(
        DeviceTrustWidget { background: #2b2b2b; color: #ddd; }
        QTableWidget { background: #1e1e1e; color: #ddd; gridline-color: #444; border: 1px solid #444; }
        QHeaderView::section { background: #3a3a3a; color: #ddd; border: 1px solid #444; padding: 4px; }
        QLineEdit { background: #3a3a3a; color: #ddd; border: 1px solid #555; border-radius: 4px; padding: 6px; }
        QLabel { color: #ddd; background: transparent; }
        QPushButton { background: #3a3a3a; color: #ddd; border: 1px solid #555; border-radius: 4px; padding: 5px 12px; }
        QPushButton:hover { background: #4a4a4a; }
    )");
}

} // namespace xrk
