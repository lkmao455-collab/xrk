#include "system_info_widget.h"
#include "app/remote_controller.h"
#include <QGroupBox>
#include <QFrame>

namespace xrk {

SystemInfoWidget::SystemInfoWidget(QWidget* parent) : QWidget(parent) {
    setupUI();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        if (m_controller) m_controller->requestSystemInfo();
    });
}

SystemInfoWidget::~SystemInfoWidget() {
    m_refreshTimer->stop();
}

void SystemInfoWidget::setRemoteController(RemoteController* controller) {
    m_controller = controller;
}

void SystemInfoWidget::updateInfo(const SysInfo& info) {
    // CPU
    m_cpuValue->setText(QString::number(info.cpuUsage, 'f', 1) + " %");
    m_cpuBar->setValue(static_cast<int>(info.cpuUsage));
    m_cpuNameLabel->setText("CPU: " + info.cpuName);

    // Memory
    m_memValue->setText(QString::number(info.memoryUsage, 'f', 1) + " %");
    m_memBar->setValue(static_cast<int>(info.memoryUsage));
    QString memStr = QString("使用 %1 GB / %2 GB")
        .arg((info.memoryTotal - info.memoryAvailable) / (1024.0 * 1024 * 1024), 0, 'f', 1)
        .arg(info.memoryTotal / (1024.0 * 1024 * 1024), 0, 'f', 1);
    m_memDetail->setText(memStr);

    // Disk
    m_diskValue->setText(QString::number(info.diskUsage, 'f', 1) + " %");
    m_diskBar->setValue(static_cast<int>(info.diskUsage));
    QString diskStr = QString("可用 %1 GB / %2 GB")
        .arg(info.diskFree / (1024.0 * 1024 * 1024), 0, 'f', 1)
        .arg(info.diskTotal / (1024.0 * 1024 * 1024), 0, 'f', 1);
    m_diskDetail->setText(diskStr);

    // System
    m_osLabel->setText(info.osName + " " + info.osVersion);
    uint64_t days = info.uptime / 86400;
    uint64_t hours = (info.uptime % 86400) / 3600;
    uint64_t mins = (info.uptime % 3600) / 60;
    m_uptimeLabel->setText(QString("%1天 %2时 %3分").arg(days).arg(hours).arg(mins));
    m_processLabel->setText(QString::number(info.processCount) + " 个进程");
    m_networkLabel->setText(QString("RX: %1 MB | TX: %2 MB")
        .arg(info.networkRx / (1024 * 1024))
        .arg(info.networkTx / (1024 * 1024)));
}

void SystemInfoWidget::setConnected(bool connected) {
    if (connected) {
        m_refreshTimer->start(3000);
        if (m_controller) m_controller->requestSystemInfo();
    } else {
        m_refreshTimer->stop();
    }
}

void SystemInfoWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    // CPU group
    auto* cpuGroup = new QGroupBox("CPU", this);
    auto* cpuLayout = new QVBoxLayout(cpuGroup);
    m_cpuNameLabel = new QLabel("CPU: --", this);
    cpuLayout->addWidget(m_cpuNameLabel);
    cpuLayout->addLayout(createMetricLayout(m_cpuValue, m_cpuBar));
    mainLayout->addWidget(cpuGroup);

    // Memory group
    auto* memGroup = new QGroupBox("内存", this);
    auto* memLayout = new QVBoxLayout(memGroup);
    memLayout->addLayout(createMetricLayout(m_memValue, m_memBar));
    m_memDetail = new QLabel("--", this);
    memLayout->addWidget(m_memDetail);
    mainLayout->addWidget(memGroup);

    // Disk group
    auto* diskGroup = new QGroupBox("磁盘 (系统盘)", this);
    auto* diskLayout = new QVBoxLayout(diskGroup);
    diskLayout->addLayout(createMetricLayout(m_diskValue, m_diskBar));
    m_diskDetail = new QLabel("--", this);
    diskLayout->addWidget(m_diskDetail);
    mainLayout->addWidget(diskGroup);

    // System info group
    auto* sysGroup = new QGroupBox("系统信息", this);
    auto* sysLayout = new QGridLayout(sysGroup);
    sysLayout->addWidget(new QLabel("操作系统:", this), 0, 0);
    m_osLabel = new QLabel("--", this);
    sysLayout->addWidget(m_osLabel, 0, 1);
    sysLayout->addWidget(new QLabel("运行时间:", this), 1, 0);
    m_uptimeLabel = new QLabel("--", this);
    sysLayout->addWidget(m_uptimeLabel, 1, 1);
    sysLayout->addWidget(new QLabel("进程数:", this), 2, 0);
    m_processLabel = new QLabel("--", this);
    sysLayout->addWidget(m_processLabel, 2, 1);
    sysLayout->addWidget(new QLabel("网络流量:", this), 3, 0);
    m_networkLabel = new QLabel("--", this);
    sysLayout->addWidget(m_networkLabel, 3, 1);
    mainLayout->addWidget(sysGroup);

    mainLayout->addStretch();
}

QHBoxLayout* SystemInfoWidget::createMetricLayout(
    QLabel*& valueLabel, QProgressBar*& progressBar) {
    auto* layout = new QHBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);

    valueLabel = new QLabel("--", this);
    valueLabel->setFixedWidth(80);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #e94560; background: transparent;");

    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setTextVisible(false);
    progressBar->setMinimumHeight(20);
    progressBar->setObjectName("metric-bar");

    layout->addWidget(valueLabel);
    layout->addWidget(progressBar, 1);

    return layout;
}

} // namespace xrk
