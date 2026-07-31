#include "file_transfer_widget.h"
#include "app/file_transfer_manager.h"
#include "app/remote_controller.h"
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>
#include <QStyle>

namespace xrk {

FileTransferWidget::FileTransferWidget(FileTransferManager* manager, QWidget* parent)
    : QWidget(parent), m_manager(manager) {
    setupUI();

    if (m_manager) {
        connect(m_manager, &FileTransferManager::transferStarted, this, &FileTransferWidget::onTransferStarted);
        connect(m_manager, &FileTransferManager::transferProgress, this, &FileTransferWidget::onTransferProgress);
        connect(m_manager, &FileTransferManager::transferCompleted, this, &FileTransferWidget::onTransferCompleted);
        connect(m_manager, &FileTransferManager::transferFailed, this, &FileTransferWidget::onTransferFailed);
    }
}

FileTransferWidget::~FileTransferWidget() {
}

void FileTransferWidget::setRemoteController(RemoteController* controller) {
    m_remoteController = controller;
}

void FileTransferWidget::onFileBrowserReceived(const FileBrowserResponse& response) {
    if (!response.success) {
        return;
    }
    m_currentRemotePath = response.path;
    m_remotePathEdit->setText(response.path);
    populateRemoteTable(response);
}

void FileTransferWidget::onRemoteConnected() {
    m_remoteConnected = true;
    m_remoteDriveCombo->setEnabled(true);
    m_remotePathEdit->setEnabled(true);
    m_remoteUpButton->setEnabled(true);
    m_remoteRefreshButton->setEnabled(true);
    m_downloadButton->setEnabled(true);
    populateDriveList();
    requestRemoteDir("C:\\");
}

void FileTransferWidget::onRemoteDisconnected() {
    m_remoteConnected = false;
    m_remoteDriveCombo->setEnabled(false);
    m_remotePathEdit->setEnabled(false);
    m_remoteUpButton->setEnabled(false);
    m_remoteRefreshButton->setEnabled(false);
    m_downloadButton->setEnabled(false);
    m_remoteTable->setRowCount(0);
}

void FileTransferWidget::onUploadClicked() {
    QString filePath = QFileDialog::getOpenFileName(this, "选择要上传的文件");
    if (!filePath.isEmpty() && m_manager) {
        m_manager->uploadFile(filePath);
    }
}

void FileTransferWidget::onDownloadClicked() {
    int row = m_remoteTable->currentRow();
    if (row < 0) return;

    QTableWidgetItem* nameItem = m_remoteTable->item(row, 0);
    QTableWidgetItem* typeItem = m_remoteTable->item(row, 1);
    if (!nameItem || !typeItem) return;

    QString name = nameItem->text();
    QString fullPath = m_currentRemotePath + "\\" + name;

    if (typeItem->text() == "文件夹") {
        requestRemoteDir(fullPath);
        return;
    }

    QString localDir = QFileDialog::getExistingDirectory(this, "保存到目录");
    if (localDir.isEmpty()) return;

    QString localPath = localDir + "/" + name;
    if (m_manager) {
        m_manager->downloadFile(fullPath, localPath);
    }
}

void FileTransferWidget::onCancelClicked() {
    QListWidgetItem* item = m_transferList->currentItem();
    if (item && m_manager) {
        QString fileId = item->data(Qt::UserRole).toString();
        m_manager->cancelTransfer(fileId);
    }
}

void FileTransferWidget::onTransferStarted(const QString& fileId) {
    QListWidgetItem* item = new QListWidgetItem(m_transferList);
    item->setData(Qt::UserRole, fileId);

    QWidget* widget = createTransferItem(fileId, fileId);
    item->setSizeHint(widget->sizeHint());
    m_transferList->addItem(item);
    m_transferList->setItemWidget(item, widget);
    m_transferWidgets[fileId] = widget;
}

void FileTransferWidget::onTransferProgress(const QString& fileId, uint64_t current, uint64_t total) {
    if (m_transferWidgets.contains(fileId)) {
        QWidget* widget = m_transferWidgets[fileId];
        QProgressBar* progressBar = widget->findChild<QProgressBar*>();
        QLabel* statusLabel = widget->findChild<QLabel*>();
        if (progressBar) {
            int progress = (total > 0) ? static_cast<int>((current * 100) / total) : 0;
            progressBar->setValue(progress);
        }
        if (statusLabel) {
            statusLabel->setText(QString("%1 / %2 KB")
                .arg(current / 1024).arg(total / 1024));
        }
    }
}

void FileTransferWidget::onTransferCompleted(const QString& fileId) {
    if (m_transferWidgets.contains(fileId)) {
        QWidget* widget = m_transferWidgets[fileId];
        QLabel* statusLabel = widget->findChild<QLabel*>();
        if (statusLabel) {
            statusLabel->setText("完成");
            statusLabel->setStyleSheet("color: green;");
        }
        m_transferWidgets.remove(fileId);
    }
}

void FileTransferWidget::onTransferFailed(const QString& fileId, const QString& errorString) {
    if (m_transferWidgets.contains(fileId)) {
        QWidget* widget = m_transferWidgets[fileId];
        QLabel* statusLabel = widget->findChild<QLabel*>();
        if (statusLabel) {
            statusLabel->setText("失败: " + errorString);
            statusLabel->setStyleSheet("color: red;");
        }
        m_transferWidgets.remove(fileId);
    }
}

void FileTransferWidget::onLocalDoubleClicked(const QModelIndex& index) {
    if (!m_localModel->isDir(index)) {
        QString filePath = m_localModel->filePath(index);
        if (m_manager) {
            m_manager->uploadFile(filePath);
        }
    }
}

void FileTransferWidget::onRemoteDoubleClicked(int row, int column) {
    Q_UNUSED(column);
    QTableWidgetItem* nameItem = m_remoteTable->item(row, 0);
    QTableWidgetItem* typeItem = m_remoteTable->item(row, 1);
    if (!nameItem || !typeItem) return;

    QString name = nameItem->text();
    if (typeItem->text() == "文件夹") {
        requestRemoteDir(m_currentRemotePath + "\\" + name);
    }
}

void FileTransferWidget::onRemotePathChanged() {
    QString path = m_remotePathEdit->text().trimmed();
    if (!path.isEmpty()) {
        requestRemoteDir(path);
    }
}

void FileTransferWidget::onRemoteGoUp() {
    QDir dir(m_currentRemotePath);
    dir.cdUp();
    requestRemoteDir(dir.absolutePath());
}

void FileTransferWidget::onRefreshRemote() {
    requestRemoteDir(m_currentRemotePath);
}

void FileTransferWidget::onRemoteDriveChanged(int index) {
    if (index < 0) return;
    QString drive = m_remoteDriveCombo->itemText(index);
    requestRemoteDir(drive);
}

void FileTransferWidget::requestRemoteDir(const QString& path) {
    if (m_remoteController) {
        m_remoteController->requestFileBrowser(path);
    }
}

void FileTransferWidget::populateRemoteTable(const FileBrowserResponse& resp) {
    m_remoteTable->setRowCount(0);
    m_remoteTable->setRowCount(resp.entries.size());

    for (int i = 0; i < resp.entries.size(); ++i) {
        const auto& entry = resp.entries[i];

        QTableWidgetItem* nameItem = new QTableWidgetItem(entry.name);
        nameItem->setData(Qt::UserRole, entry.path);
        if (entry.isDir) {
            nameItem->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
        } else {
            nameItem->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon));
        }
        m_remoteTable->setItem(i, 0, nameItem);

        QTableWidgetItem* typeItem = new QTableWidgetItem(entry.isDir ? "文件夹" : "文件");
        typeItem->setTextAlignment(Qt::AlignCenter);
        m_remoteTable->setItem(i, 1, typeItem);

        QString sizeStr;
        if (!entry.isDir) {
            if (entry.fileSize < 1024)
                sizeStr = QString::number(entry.fileSize) + " B";
            else if (entry.fileSize < 1024 * 1024)
                sizeStr = QString::number(entry.fileSize / 1024) + " KB";
            else
                sizeStr = QString::number(entry.fileSize / (1024 * 1024)) + " MB";
        }
        QTableWidgetItem* sizeItem = new QTableWidgetItem(sizeStr);
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_remoteTable->setItem(i, 2, sizeItem);

        QTableWidgetItem* dateItem = new QTableWidgetItem(entry.lastModified);
        dateItem->setTextAlignment(Qt::AlignCenter);
        m_remoteTable->setItem(i, 3, dateItem);
    }

    m_remoteTable->resizeColumnsToContents();
    if (m_remoteTable->columnWidth(0) < 200) m_remoteTable->setColumnWidth(0, 200);
}

void FileTransferWidget::populateDriveList() {
    m_remoteDriveCombo->clear();
    m_remoteDriveCombo->addItem("C:\\");
    m_remoteDriveCombo->addItem("D:\\");
    m_remoteDriveCombo->addItem("E:\\");
}

void FileTransferWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    // Splitter for local / remote panels
    QSplitter* browserSplitter = new QSplitter(Qt::Horizontal, this);

    // ---- Local panel ----
    QWidget* localPanel = new QWidget();
    QVBoxLayout* localLayout = new QVBoxLayout(localPanel);
    localLayout->setContentsMargins(0, 0, 0, 0);
    localLayout->addWidget(new QLabel("本地文件"));

    m_localModel = new QFileSystemModel(this);
    m_localModel->setRootPath(QDir::rootPath());
    m_localModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);

    m_localTree = new QTreeView(this);
    m_localTree->setModel(m_localModel);
    m_localTree->setRootIndex(m_localModel->index(QDir::rootPath()));
    m_localTree->setSortingEnabled(true);
    m_localTree->setAnimated(true);
    m_localTree->setColumnWidth(0, 180);
    m_localTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_localTree, &QTreeView::doubleClicked, this, &FileTransferWidget::onLocalDoubleClicked);
    localLayout->addWidget(m_localTree);

    // ---- Remote panel ----
    QWidget* remotePanel = new QWidget();
    QVBoxLayout* remoteLayout = new QVBoxLayout(remotePanel);
    remoteLayout->setContentsMargins(0, 0, 0, 0);
    remoteLayout->addWidget(new QLabel("远程文件"));

    QHBoxLayout* remoteToolbar = new QHBoxLayout();

    m_remoteDriveCombo = new QComboBox(this);
    m_remoteDriveCombo->setEnabled(false);
    m_remoteDriveCombo->setMinimumWidth(60);
    connect(m_remoteDriveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FileTransferWidget::onRemoteDriveChanged);
    remoteToolbar->addWidget(m_remoteDriveCombo);

    m_remotePathEdit = new QLineEdit(this);
    m_remotePathEdit->setPlaceholderText("远程路径...");
    m_remotePathEdit->setEnabled(false);
    connect(m_remotePathEdit, &QLineEdit::returnPressed, this, &FileTransferWidget::onRemotePathChanged);
    remoteToolbar->addWidget(m_remotePathEdit);

    m_remoteUpButton = new QPushButton("↑", this);
    m_remoteUpButton->setFixedWidth(32);
    m_remoteUpButton->setEnabled(false);
    m_remoteUpButton->setToolTip("上级目录");
    connect(m_remoteUpButton, &QPushButton::clicked, this, &FileTransferWidget::onRemoteGoUp);
    remoteToolbar->addWidget(m_remoteUpButton);

    m_remoteRefreshButton = new QPushButton("刷新", this);
    m_remoteRefreshButton->setFixedWidth(50);
    m_remoteRefreshButton->setEnabled(false);
    connect(m_remoteRefreshButton, &QPushButton::clicked, this, &FileTransferWidget::onRefreshRemote);
    remoteToolbar->addWidget(m_remoteRefreshButton);

    remoteLayout->addLayout(remoteToolbar);

    m_remoteTable = new QTableWidget(0, 4, this);
    m_remoteTable->setHorizontalHeaderLabels({"名称", "类型", "大小", "修改时间"});
    m_remoteTable->horizontalHeader()->setStretchLastSection(true);
    m_remoteTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_remoteTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_remoteTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_remoteTable->verticalHeader()->setVisible(false);
    m_remoteTable->setShowGrid(false);
    m_remoteTable->setAlternatingRowColors(true);
    connect(m_remoteTable, &QTableWidget::cellDoubleClicked, this, &FileTransferWidget::onRemoteDoubleClicked);
    remoteLayout->addWidget(m_remoteTable);

    browserSplitter->addWidget(localPanel);
    browserSplitter->addWidget(remotePanel);
    browserSplitter->setSizes({400, 400});

    mainLayout->addWidget(browserSplitter, 3);

    // ---- Transfer queue ----
    QLabel* queueLabel = new QLabel("传输队列");
    mainLayout->addWidget(queueLabel);

    m_transferList = new QListWidget(this);
    m_transferList->setMaximumHeight(150);
    m_transferList->setSpacing(2);
    mainLayout->addWidget(m_transferList, 1);

    // ---- Buttons ----
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    m_uploadButton = new QPushButton("上传", this);
    m_uploadButton->setFixedWidth(80);
    connect(m_uploadButton, &QPushButton::clicked, this, &FileTransferWidget::onUploadClicked);
    buttonLayout->addWidget(m_uploadButton);

    m_downloadButton = new QPushButton("下载", this);
    m_downloadButton->setFixedWidth(80);
    m_downloadButton->setEnabled(false);
    connect(m_downloadButton, &QPushButton::clicked, this, &FileTransferWidget::onDownloadClicked);
    buttonLayout->addWidget(m_downloadButton);

    m_cancelButton = new QPushButton("取消", this);
    m_cancelButton->setFixedWidth(80);
    connect(m_cancelButton, &QPushButton::clicked, this, &FileTransferWidget::onCancelClicked);
    buttonLayout->addWidget(m_cancelButton);

    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
}

QWidget* FileTransferWidget::createTransferItem(const QString& fileId, const QString& fileName) {
    QWidget* item = new QWidget();
    QHBoxLayout* layout = new QHBoxLayout(item);
    layout->setContentsMargins(4, 2, 4, 2);

    QLabel* nameLabel = new QLabel(fileName, item);
    nameLabel->setMinimumWidth(150);

    QProgressBar* progressBar = new QProgressBar(item);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setMaximumHeight(16);

    QLabel* statusLabel = new QLabel("0%", item);
    statusLabel->setFixedWidth(120);
    statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    layout->addWidget(nameLabel);
    layout->addWidget(progressBar);
    layout->addWidget(statusLabel);

    return item;
}

} // namespace xrk
