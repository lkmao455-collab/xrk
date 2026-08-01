#include "ipmsg_widget.h"
#include "app/ipmsg_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollBar>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

namespace xrk {

IPMsgWidget::IPMsgWidget(IPMsgManager* manager, QWidget* parent)
    : QWidget(parent), m_manager(manager) {
    setupUI();

    // Connect manager signals
    if (m_manager) {
        connect(m_manager, &IPMsgManager::messageReceived, this, [this](const IPMsgMessage& msg) {
            onMessageReceived(msg.senderName, msg.content);
        });

        connect(m_manager, &IPMsgManager::fileProgress, this, [this](const QString& fileId, qint64 bytes, qint64 total) {
            onFileProgress(fileId, bytes, total);
        });

        connect(m_manager, &IPMsgManager::fileCompleted, this, [this](const QString& fileId, const QString& filePath, bool integrityOk) {
            onFileCompleted(fileId, integrityOk);
        });

        connect(m_manager, &IPMsgManager::fileResuming, this, [this](const QString& fileId, qint64 offset) {
            onFileResuming(fileId, offset);
        });
    }
}

IPMsgWidget::~IPMsgWidget() {
}

void IPMsgWidget::setTargetDevice(const QString& deviceId, const QString& deviceName) {
    m_targetName = deviceName;
    m_targetLabel->setText(tr("目标: %1").arg(deviceName));
}

void IPMsgWidget::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(5, 5, 5, 5);

    // Top bar
    auto* topLayout = new QHBoxLayout();
    m_targetLabel = new QLabel(tr("请选择目标设备"), this);
    m_targetLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    topLayout->addWidget(m_targetLabel);
    topLayout->addStretch();

    m_refreshBtn = new QPushButton(tr("刷新"), this);
    connect(m_refreshBtn, &QPushButton::clicked, this, &IPMsgWidget::onRefreshDevices);
    topLayout->addWidget(m_refreshBtn);

    mainLayout->addLayout(topLayout);

    // Splitter
    m_splitter = new QSplitter(Qt::Horizontal, this);

    // Left panel - Device list
    auto* leftWidget = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    auto* deviceGroup = new QGroupBox(tr("在线设备"), leftWidget);
    auto* deviceLayout = new QVBoxLayout(deviceGroup);

    m_deviceTree = new QTreeWidget(leftWidget);
    m_deviceTree->setHeaderLabels({tr("设备名称"), tr("IP地址")});
    m_deviceTree->setColumnWidth(0, 150);
    m_deviceTree->setColumnWidth(1, 120);
    connect(m_deviceTree, &QTreeWidget::itemDoubleClicked, this, &IPMsgWidget::onDeviceDoubleClicked);
    deviceLayout->addWidget(m_deviceTree);

    leftLayout->addWidget(deviceGroup);
    m_splitter->addWidget(leftWidget);

    // Right panel - Chat
    auto* rightWidget = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    m_messageDisplay = new QTextEdit(rightWidget);
    m_messageDisplay->setReadOnly(true);
    m_messageDisplay->setStyleSheet("QTextEdit { background-color: #f5f5f5; border: 1px solid #ddd; border-radius: 5px; padding: 10px; }");
    rightLayout->addWidget(m_messageDisplay, 1);

    // Progress bar and integrity label
    auto* progressLayout = new QVBoxLayout();
    m_progressBar = new QProgressBar(rightWidget);
    m_progressBar->setVisible(false);
    progressLayout->addWidget(m_progressBar);

    m_integrityLabel = new QLabel(rightWidget);
    m_integrityLabel->setVisible(false);
    m_integrityLabel->setStyleSheet("font-weight: bold; padding: 5px;");
    progressLayout->addWidget(m_integrityLabel);

    // Resume button
    m_resumeBtn = new QPushButton(tr("断点续传"), rightWidget);
    m_resumeBtn->setStyleSheet("QPushButton { background-color: #FF9800; color: white; border: none; border-radius: 15px; padding: 8px 15px; }"
                             "QPushButton:hover { background-color: #F57C00; }");
    m_resumeBtn->setVisible(false);
    connect(m_resumeBtn, &QPushButton::clicked, this, [this]() {
        if (m_manager) {
            // Resume last failed transfer
            QString lastFileId;
            // In real implementation, track the last failed file ID
            if (!lastFileId.isEmpty()) {
                onResumeTransfer(lastFileId);
            }
        }
    });
    progressLayout->addWidget(m_resumeBtn);

    rightLayout->addLayout(progressLayout);

    // Input area
    auto* inputLayout = new QHBoxLayout();

    m_messageInput = new QLineEdit(rightWidget);
    m_messageInput->setPlaceholderText(tr("输入消息..."));
    m_messageInput->setStyleSheet("QLineEdit { border: 1px solid #ddd; border-radius: 15px; padding: 8px 15px; }");
    connect(m_messageInput, &QLineEdit::returnPressed, this, &IPMsgWidget::onSendClicked);
    inputLayout->addWidget(m_messageInput);

    m_sendBtn = new QPushButton(tr("发送"), rightWidget);
    m_sendBtn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; border: none; border-radius: 15px; padding: 8px 20px; }"
                           "QPushButton:hover { background-color: #45a049; }");
    connect(m_sendBtn, &QPushButton::clicked, this, &IPMsgWidget::onSendClicked);
    inputLayout->addWidget(m_sendBtn);

    rightLayout->addLayout(inputLayout);

    // File send buttons
    auto* fileLayout = new QHBoxLayout();

    m_sendFileBtn = new QPushButton(tr("发送文件"), rightWidget);
    m_sendFileBtn->setStyleSheet("QPushButton { background-color: #2196F3; color: white; border: none; border-radius: 15px; padding: 8px 15px; }"
                               "QPushButton:hover { background-color: #1976D2; }");
    connect(m_sendFileBtn, &QPushButton::clicked, this, &IPMsgWidget::onSendFileClicked);
    fileLayout->addWidget(m_sendFileBtn);

    m_sendFolderBtn = new QPushButton(tr("发送文件夹"), rightWidget);
    m_sendFolderBtn->setStyleSheet("QPushButton { background-color: #9C27B0; color: white; border: none; border-radius: 15px; padding: 8px 15px; }"
                                 "QPushButton:hover { background-color: #7B1FA2; }");
    connect(m_sendFolderBtn, &QPushButton::clicked, this, &IPMsgWidget::onSendFolderClicked);
    fileLayout->addWidget(m_sendFolderBtn);

    rightLayout->addLayout(fileLayout);

    m_splitter->addWidget(rightWidget);

    mainLayout->addWidget(m_splitter, 1);

    // Status bar
    auto* statusLayout = new QHBoxLayout();
    m_statusLabel = new QLabel(tr("就绪"), this);
    m_statusLabel->setStyleSheet("color: #666;");
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    mainLayout->addLayout(statusLayout);
}

void IPMsgWidget::addMessage(const QString& sender, const QString& message, bool isSelf) {
    QString color = isSelf ? "#4CAF50" : "#2196F3";
    QString align = isSelf ? "right" : "left";
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");

    QString html = QString(
        "<div style='text-align: %1; margin: 5px 0;'>"
        "<span style='background-color: %2; color: white; padding: 8px 12px; "
        "border-radius: 10px; display: inline-block; max-width: 70%%;'>"
        "<b>%3</b><br>%4"
        "<br><small style='color: #eee;'>%5</small>"
        "</span></div>")
        .arg(align)
        .arg(color)
        .arg(sender)
        .arg(message.toHtmlEscaped())
        .arg(timestamp);

    m_messageDisplay->append(html);

    // Scroll to bottom
    QScrollBar* scrollbar = m_messageDisplay->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}

void IPMsgWidget::updateDeviceList() {
    m_deviceTree->clear();

    if (!m_manager) return;

    auto devices = m_manager->getOnlineDevices();
    for (const auto& device : devices) {
        auto* item = new QTreeWidgetItem(m_deviceTree);
        item->setText(0, device.name);
        item->setText(1, device.ip);
        item->setData(0, Qt::UserRole, device.ip);
        item->setData(0, Qt::UserRole + 1, device.id);
    }
}

void IPMsgWidget::updateTransferStatus(const QString& fileName, const QString& status, const QString& color) {
    m_statusLabel->setText(status);
    m_statusLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(color));
}

void IPMsgWidget::onSendClicked() {
    QString message = m_messageInput->text().trimmed();
    if (message.isEmpty()) return;

    if (m_targetIp.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择目标设备"));
        return;
    }

    addMessage(m_manager->userName(), message, true);
    emit sendMessage(m_targetIp, message);
    m_messageInput->clear();
}

void IPMsgWidget::onSendFileClicked() {
    if (m_targetIp.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择目标设备"));
        return;
    }

    QStringList files = QFileDialog::getOpenFileNames(this, tr("选择文件"));
    for (const QString& file : files) {
        emit sendFile(m_targetIp, file);
        updateTransferStatus(QFileInfo(file).fileName(), tr("正在发送: %1").arg(QFileInfo(file).fileName()), "#2196F3");
    }
}

void IPMsgWidget::onSendFolderClicked() {
    if (m_targetIp.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择目标设备"));
        return;
    }

    QString folder = QFileDialog::getExistingDirectory(this, tr("选择文件夹"));
    if (!folder.isEmpty()) {
        emit sendFolder(m_targetIp, folder);
        updateTransferStatus(QFileInfo(folder).fileName(), tr("正在发送文件夹: %1").arg(QFileInfo(folder).fileName()), "#2196F3");
    }
}

void IPMsgWidget::onDeviceDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (!item) return;

    QString ip = item->data(0, Qt::UserRole).toString();
    QString name = item->text(0);

    m_targetIp = ip;
    m_targetName = name;
    m_targetLabel->setText(tr("目标: %1 (%2)").arg(name, ip));

    addMessage("", tr("已选择目标设备: %1").arg(name), false);
}

void IPMsgWidget::onMessageReceived(const QString& sender, const QString& message) {
    addMessage(sender, message, false);
}

void IPMsgWidget::onFileProgress(const QString& fileName, qint64 bytesTransferred, qint64 totalBytes) {
    m_progressBar->setVisible(true);
    m_progressBar->setMaximum(totalBytes);
    m_progressBar->setValue(bytesTransferred);
    
    double percentage = (totalBytes > 0) ? (bytesTransferred * 100.0 / totalBytes) : 0;
    updateTransferStatus(fileName, 
        tr("传输中: %1 (%2/%3 - %4%)")
            .arg(fileName)
            .arg(bytesTransferred / 1024)
            .arg(totalBytes / 1024)
            .arg(percentage, 0, 'f', 1), 
        "#2196F3");
}

void IPMsgWidget::onFileCompleted(const QString& fileName, bool integrityOk) {
    m_progressBar->setVisible(false);
    m_resumeBtn->setVisible(false);
    
    if (integrityOk) {
        m_integrityLabel->setVisible(true);
        m_integrityLabel->setText(tr("✓ 文件完整性验证通过 (MD5校验成功)"));
        m_integrityLabel->setStyleSheet("color: #4CAF50; font-weight: bold; padding: 5px; background-color: #E8F5E9; border-radius: 5px;");
        updateTransferStatus(fileName, tr("传输完成: %1 - 完整性验证通过").arg(fileName), "#4CAF50");
        addMessage("", tr("文件传输完成: %1 (完整性验证通过)").arg(fileName), false);
    } else {
        m_integrityLabel->setVisible(true);
        m_integrityLabel->setText(tr("✗ 文件完整性验证失败 (MD5校验不匹配)"));
        m_integrityLabel->setStyleSheet("color: #F44336; font-weight: bold; padding: 5px; background-color: #FFEBEE; border-radius: 5px;");
        m_resumeBtn->setVisible(true);
        updateTransferStatus(fileName, tr("传输完成: %1 - 完整性验证失败").arg(fileName), "#F44336");
        addMessage("", tr("文件传输完成: %1 (完整性验证失败，建议重新传输)").arg(fileName), false);
    }
}

void IPMsgWidget::onFileResuming(const QString& fileName, qint64 offset) {
    m_progressBar->setVisible(true);
    m_integrityLabel->setVisible(true);
    m_integrityLabel->setText(tr("正在断点续传: %1 (从 %2 KB 处继续)")
        .arg(fileName)
        .arg(offset / 1024));
    m_integrityLabel->setStyleSheet("color: #FF9800; font-weight: bold; padding: 5px; background-color: #FFF3E0; border-radius: 5px;");
    updateTransferStatus(fileName, tr("断点续传: %1").arg(fileName), "#FF9800");
    addMessage("", tr("开始断点续传: %1 (从 %2 KB 处继续)").arg(fileName).arg(offset / 1024), false);
}

void IPMsgWidget::onRefreshDevices() {
    if (m_manager) {
        m_manager->broadcastPresence();
    }
    updateDeviceList();
}

void IPMsgWidget::onDeviceFound(const QString& name, const QString& ip) {
    updateDeviceList();
    m_statusLabel->setText(tr("发现新设备: %1").arg(name));
}

void IPMsgWidget::onDeviceLeft(const QString& name) {
    updateDeviceList();
    m_statusLabel->setText(tr("设备离线: %1").arg(name));
}

void IPMsgWidget::onResumeTransfer(const QString& fileId) {
    if (m_manager) {
        m_manager->resumeFile(fileId);
        addMessage("", tr("请求断点续传..."), false);
    }
}

} // namespace xrk
