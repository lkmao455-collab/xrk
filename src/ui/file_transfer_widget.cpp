#include "file_transfer_widget.h"
#include "app/file_transfer_manager.h"
#include "app/file_sync_manager.h"
#include "core/logger.h"
#include "app/remote_controller.h"
#include <QFileDialog>
#include <QInputDialog>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>
#include <QStyle>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDragLeaveEvent>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QMimeData>
#include <QList>

namespace xrk {

// Remote table acts as a drag source, carrying a custom MIME with the full
// remote path, name and isDir flag so the local tree can initiate a download.
class RemoteDragTable : public QTableWidget {
    Q_OBJECT
public:
    explicit RemoteDragTable(int rows, int cols, QWidget* parent = nullptr)
        : QTableWidget(rows, cols, parent) {}

protected:
    QMimeData* mimeData(const QList<QTableWidgetItem*>& items) const override {
        QMimeData* mime = QTableWidget::mimeData(items);
        if (!mime) mime = new QMimeData();
        if (items.isEmpty()) return mime;

        QTableWidgetItem* item = items.first();
        QString remotePath = item->data(Qt::UserRole).toString();
        QString name = item->text();
        QTableWidgetItem* typeItem = this->item(item->row(), 1);
        bool isDir = typeItem && typeItem->text() == "文件夹";

        QString payload = remotePath + "\n" + name + "\n" + (isDir ? "1" : "0");
        mime->setData("application/x-xrk-remote-path", payload.toUtf8());
        mime->setText(remotePath);
        return mime;
    }
};

// Paints the currently-targeted drop folder with a highlighted row.
class LocalDropDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit LocalDropDelegate(QTreeView* tree, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_tree(tree) {}

    void setDropIndex(const QModelIndex& idx) { m_dropIndex = idx; }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem opt = option;
        if (index == m_dropIndex && index.isValid() && m_tree) {
            int w = m_tree->viewport()->width();
            QRect r(0, opt.rect.y(), w, opt.rect.height());
            painter->fillRect(r, QColor(43, 108, 255, 70));
        }
        QStyledItemDelegate::paint(painter, opt, index);
    }

private:
    QTreeView* m_tree = nullptr;
    QModelIndex m_dropIndex;
};

// Local tree acts as a drop target for the custom remote-path MIME.
class LocalDropTree : public QTreeView {
    Q_OBJECT
public:
    explicit LocalDropTree(QWidget* parent = nullptr) : QTreeView(parent) {
        m_delegate = new LocalDropDelegate(this, this);
        setItemDelegate(m_delegate);
    }

signals:
    void remoteDropped(const QMimeData* mime, const QModelIndex& index);
    void dragActiveChanged(bool active);
    void dropTargetChanged(const QString& path);

private:
    // Resolve the folder a drop at point p would land in: the hovered dir,
    // or the parent folder of a hovered file.
    QModelIndex folderIndexAt(const QPoint& p) const {
        QModelIndex idx = indexAt(p);
        if (!idx.isValid()) return QModelIndex();
        auto* fs = qobject_cast<QFileSystemModel*>(model());
        if (fs && !fs->isDir(idx)) idx = fs->parent(idx);
        return idx;
    }

    void updateDropTarget(const QPoint& p) {
        QModelIndex folder = folderIndexAt(p);
        m_delegate->setDropIndex(folder);
        viewport()->update();

        QString path;
        auto* fs = qobject_cast<QFileSystemModel*>(model());
        if (fs && folder.isValid()) path = fs->filePath(folder);
        if (path != m_lastTarget) {
            m_lastTarget = path;
            emit dropTargetChanged(path);
        }
    }

    void setActive(bool active) {
        if (m_active == active) return;
        m_active = active;
        setProperty("dropping", active);
        style()->unpolish(this);
        style()->polish(this);
        emit dragActiveChanged(active);
        if (!active) {
            m_delegate->setDropIndex(QModelIndex());
            viewport()->update();
            if (!m_lastTarget.isEmpty()) {
                m_lastTarget.clear();
                emit dropTargetChanged(QString());
            }
        }
    }

    LocalDropDelegate* m_delegate = nullptr;
    QString m_lastTarget;
    bool m_active = false;

protected:
    void dragEnterEvent(QDragEnterEvent* e) override {
        if (e->mimeData()->hasFormat("application/x-xrk-remote-path")) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
            setActive(true);
            updateDropTarget(e->pos());
        } else {
            e->ignore();
        }
    }

    void dragMoveEvent(QDragMoveEvent* e) override {
        if (e->mimeData()->hasFormat("application/x-xrk-remote-path")) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
            setActive(true);
            updateDropTarget(e->pos());
        } else {
            e->ignore();
        }
    }

    void dragLeaveEvent(QDragLeaveEvent* e) override {
        setActive(false);
        QTreeView::dragLeaveEvent(e);
    }

    void dropEvent(QDropEvent* e) override {
        if (e->mimeData()->hasFormat("application/x-xrk-remote-path")) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
            emit remoteDropped(e->mimeData(), indexAt(e->pos()));
        } else {
            e->ignore();
        }
        setActive(false);
    }
};

FileTransferWidget::FileTransferWidget(FileTransferManager* manager, QWidget* parent)
    : QWidget(parent), m_manager(manager) {
    setupUI();

    if (m_manager) {
        connect(m_manager, &FileTransferManager::transferStarted, this, &FileTransferWidget::onTransferStarted);
        connect(m_manager, &FileTransferManager::transferProgress, this, &FileTransferWidget::onTransferProgress);
        connect(m_manager, &FileTransferManager::transferCompleted, this, &FileTransferWidget::onTransferCompleted);
        connect(m_manager, &FileTransferManager::transferFailed, this, &FileTransferWidget::onTransferFailed);
        connect(m_manager, &FileTransferManager::transferCancelled, this, [this](const QString& fileId) {
            onTransferFailed(fileId, tr("传输已取消"));
        });
        connect(m_manager, &FileTransferManager::transferResumed, this, [this](const QString& fileId, uint64_t offset) {
            LOG_INFO("File transfer resumed: " + fileId + " from " + QString::number(offset) + " bytes");
        });
    }

    // Task 27: real-time file sync. The manager is decoupled from the transfer
    // mechanism via an upload callback wired to the live FileTransferManager.
    m_sync = new FileSyncManager(this);
    m_sync->setUploadCallback([this](const QString& local, const QString& remote) {
        if (!m_manager || !m_remoteConnected) {
            LOG_WARNING("Sync skipped (not connected): " + local);
            return QString();
        }
        return m_manager->uploadFileTo(local, remote);
    });
    connect(m_sync, &FileSyncManager::started, this, [this]() {
        if (m_syncToggleButton) m_syncToggleButton->setText("停止同步");
        if (m_syncStatus) m_syncStatus->setText("同步已启动");
    });
    connect(m_sync, &FileSyncManager::stopped, this, [this]() {
        if (m_syncToggleButton) m_syncToggleButton->setText("开始同步");
        if (m_syncStatus) m_syncStatus->setText("同步已停止");
    });
    connect(m_sync, &FileSyncManager::fileSynced, this, [this](const QString& local) {
        if (m_syncStatus) m_syncStatus->setText("已同步: " + QFileInfo(local).fileName());
    });
    connect(m_sync, &FileSyncManager::syncError, this, [this](const QString& msg) {
        if (m_syncStatus) m_syncStatus->setText(msg);
    });
}

FileTransferWidget::~FileTransferWidget() {
}

void FileTransferWidget::setRemoteController(RemoteController* controller) {
    m_remoteController = controller;
    if (m_remoteController) {
        connect(m_remoteController, &RemoteController::syncNotifyReceived,
                this, &FileTransferWidget::onSyncNotifyReceived);
    }
}

void FileTransferWidget::onFileBrowserReceived(const FileBrowserResponse& response) {
    // Directory download scan responses are intercepted and never touch the UI.
    if (m_activeScans.contains(response.path)) {
        if (response.success) {
            QString localBase = m_dirScanMap.value(response.path);
            for (const auto& entry : response.entries) {
                QString childLocal = localBase + "/" + entry.name;
                if (entry.isDir) {
                    QDir().mkpath(childLocal);
                    m_activeScans.insert(entry.path);
                    m_dirScanMap[entry.path] = childLocal;
                    m_dirScanQueue.append({entry.path, childLocal});
                } else if (m_manager) {
                    m_manager->downloadFile(entry.path, childLocal, entry.fileSize);
                }
            }
        }
        m_activeScans.remove(response.path);
        m_dirScanMap.remove(response.path);
        scanNextDir();
        return;
    }

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
    if (m_sync) m_sync->setCanUpload(true);
    // Re-establish reverse-sync watches that were torn down when the host
    // dropped this client's watchers on disconnect.
    for (auto it = m_reversePairs.begin(); it != m_reversePairs.end(); ++it) {
        if (m_remoteController) m_remoteController->sendSyncAdd(it.key(), it.value());
    }
}

void FileTransferWidget::onRemoteDisconnected() {
    m_remoteConnected = false;
    if (m_sync) m_sync->setCanUpload(false);
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

    uint64_t size = nameItem->data(Qt::UserRole + 1).toULongLong();

    QString localDir = QFileDialog::getExistingDirectory(this, "保存到目录");
    if (localDir.isEmpty()) return;

    QString localPath = localDir + "/" + name;
    if (m_manager) {
        m_manager->downloadFile(fullPath, localPath, size);
    }
}

void FileTransferWidget::onCancelClicked() {
    QListWidgetItem* item = m_transferList->currentItem();
    if (item && m_manager) {
        QString fileId = item->data(Qt::UserRole).toString();
        m_manager->cancelTransfer(fileId);
    }
}

void FileTransferWidget::addSyncListItem(const QString& localDir, const QString& remoteDir) {
    if (!m_syncList) return;
    QListWidgetItem* item = new QListWidgetItem(m_syncList);
    item->setData(Qt::UserRole, localDir);
    QWidget* w = new QWidget();
    QHBoxLayout* hl = new QHBoxLayout(w);
    hl->setContentsMargins(4, 2, 4, 2);
    QLabel* label = new QLabel(localDir + "\n→ " + remoteDir, w);
    label->setWordWrap(true);
    hl->addWidget(label);
    m_syncList->setItemWidget(item, w);
}

void FileTransferWidget::onAddSyncClicked() {
    QString localDir = QFileDialog::getExistingDirectory(this, "选择要同步的本地目录");
    if (localDir.isEmpty()) return;

    bool ok = false;
    QString remoteDir = QInputDialog::getText(this, "远程目录",
        "远程（被控端）目标目录：", QLineEdit::Normal, m_currentRemotePath, &ok);
    if (!ok || remoteDir.isEmpty()) return;

    if (m_sync && m_sync->addPair(localDir, remoteDir)) {
        addSyncListItem(localDir, remoteDir);
        if (m_syncStatus) m_syncStatus->setText("已添加同步：本地 " + localDir + " → 远程 " + remoteDir);
    } else {
        QMessageBox::information(this, "实时同步", "该本地目录已在同步列表中。");
    }
}

void FileTransferWidget::onRemoveSyncClicked() {
    if (!m_syncList || !m_sync) return;
    QListWidgetItem* item = m_syncList->currentItem();
    if (!item) return;
    QString localDir = item->data(Qt::UserRole).toString();
    m_sync->removePair(localDir);
    delete item;
}

void FileTransferWidget::onSyncToggleClicked() {
    if (!m_sync) return;
    if (m_sync->isRunning()) {
        m_sync->stop();
    } else {
        if (m_sync->pairKeys().isEmpty()) {
            QMessageBox::information(this, "实时同步", "请先添加同步目录。");
            return;
        }
        m_sync->setCanUpload(m_remoteConnected);
        m_sync->start();
    }
}

void FileTransferWidget::addReverseSyncListItem(const QString& hostDir, const QString& localDir) {
    if (!m_reverseList) return;
    QListWidgetItem* item = new QListWidgetItem(m_reverseList);
    item->setData(Qt::UserRole, hostDir);
    QWidget* w = new QWidget();
    QHBoxLayout* hl = new QHBoxLayout(w);
    hl->setContentsMargins(4, 2, 4, 2);
    QLabel* label = new QLabel("远程 " + hostDir + "\n→ 本地 " + localDir, w);
    label->setWordWrap(true);
    hl->addWidget(label);
    m_reverseList->setItemWidget(item, w);
}

void FileTransferWidget::onAddReverseSyncClicked() {
    // The controlled machine's directory is watched; changes land in the local dir.
    QString localDir = QFileDialog::getExistingDirectory(this, "选择本地目标目录（远程变更将下载到此）");
    if (localDir.isEmpty()) return;

    bool ok = false;
    QString hostDir = QInputDialog::getText(this, "远程目录",
        "远程（被控端）要监控的目录：", QLineEdit::Normal, m_currentRemotePath, &ok);
    if (!ok || hostDir.isEmpty()) return;

    QString key = QDir::toNativeSeparators(QDir(hostDir).absolutePath());
    if (m_reversePairs.contains(key)) {
        QMessageBox::information(this, "反向同步", "该远程目录已在同步列表中。");
        return;
    }
    m_reversePairs[key] = localDir;
    addReverseSyncListItem(key, localDir);

    if (m_remoteController && m_remoteConnected) {
        m_remoteController->sendSyncAdd(hostDir, localDir);
        if (m_syncStatus) m_syncStatus->setText("已请求反向同步：远程 " + key + " → 本地 " + localDir);
    } else {
        if (m_syncStatus) m_syncStatus->setText("反向同步已记录，连接后自动生效");
    }
}

void FileTransferWidget::onRemoveReverseSyncClicked() {
    if (!m_reverseList || !m_remoteController) return;
    QListWidgetItem* item = m_reverseList->currentItem();
    if (!item) return;
    QString hostDir = item->data(Qt::UserRole).toString();
    m_reversePairs.remove(hostDir);
    if (m_remoteConnected) {
        m_remoteController->sendSyncRemove(hostDir);
    }
    delete item;
}

void FileTransferWidget::onSyncNotifyReceived(const SyncNotify& note) {
    QString hostDir = QDir::toNativeSeparators(QDir(note.hostDir).absolutePath());
    if (!m_reversePairs.contains(hostDir)) {
        LOG_DEBUG("SyncNotify for unknown pair: " + hostDir);
        return;
    }
    if (!m_manager) return;

    QString localDir = m_reversePairs.value(hostDir);
    QString relative = FileSyncManager::relativePath(note.hostDir, note.hostFilePath);
    QString target = QDir::toNativeSeparators(localDir + "/" + relative);
    QDir().mkpath(QFileInfo(target).path());  // ensure subdirs exist locally

    m_manager->downloadFile(note.hostFilePath, target, note.size);
    if (m_syncStatus) {
        m_syncStatus->setText("反向同步下载: " + QFileInfo(note.hostFilePath).fileName() +
                              " → " + localDir);
    }
}

void FileTransferWidget::onRemoteDropped(const QMimeData* mime, const QModelIndex& index) {
    if (!mime || !mime->hasFormat("application/x-xrk-remote-path")) return;

    QByteArray data = mime->data("application/x-xrk-remote-path");
    QList<QByteArray> parts = data.split('\n');
    if (parts.size() < 3) return;

    QString remotePath = QString::fromUtf8(parts[0]).trimmed();
    QString name = QString::fromUtf8(parts[1]).trimmed();
    bool isDir = (parts[2].trimmed() == "1");
    if (remotePath.isEmpty() || name.isEmpty()) return;

    QString localDir;
    if (!index.isValid()) {
        localDir = m_localModel->rootPath();
    } else if (m_localModel->isDir(index)) {
        localDir = m_localModel->filePath(index);
    } else {
        localDir = m_localModel->filePath(m_localModel->parent(index));
    }

    downloadRemoteItem(remotePath, name, isDir, 0, localDir);
}

void FileTransferWidget::downloadRemoteItem(const QString& remotePath, const QString& name,
                                            bool isDir, uint64_t size, const QString& localDir) {
    if (!m_manager) return;
    QDir().mkpath(localDir);
    if (isDir) {
        startDirectoryDownload(remotePath, localDir + "/" + name);
    } else {
        m_manager->downloadFile(remotePath, localDir + "/" + name, size);
    }
}

void FileTransferWidget::startDirectoryDownload(const QString& remotePath, const QString& localPath) {
    QDir().mkpath(localPath);
    if (m_activeScans.contains(remotePath)) return;

    bool wasEmpty = m_dirScanQueue.isEmpty();
    m_activeScans.insert(remotePath);
    m_dirScanMap[remotePath] = localPath;
    m_dirScanQueue.append({remotePath, localPath});
    if (wasEmpty) {
        scanNextDir();
    }
}

void FileTransferWidget::scanNextDir() {
    if (m_dirScanQueue.isEmpty()) return;
    PendingDir pd = m_dirScanQueue.takeFirst();
    requestRemoteDir(pd.remotePath);
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
        nameItem->setData(Qt::UserRole + 1, QVariant::fromValue(entry.fileSize));
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

    m_localTree = new LocalDropTree(this);
    m_localTree->setObjectName("local-tree");
    m_localTree->setModel(m_localModel);
    m_localTree->setRootIndex(m_localModel->index(QDir::rootPath()));
    m_localTree->setSortingEnabled(true);
    m_localTree->setAnimated(true);
    m_localTree->setColumnWidth(0, 180);
    m_localTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_localTree->setDragDropMode(QAbstractItemView::DropOnly);
    m_localTree->setAcceptDrops(true);
    m_localTree->setDropIndicatorShown(true);
    connect(m_localTree, &QTreeView::doubleClicked, this, &FileTransferWidget::onLocalDoubleClicked);
    connect(qobject_cast<LocalDropTree*>(m_localTree), &LocalDropTree::remoteDropped,
            this, &FileTransferWidget::onRemoteDropped);
    localLayout->addWidget(m_localTree);

    QLabel* dropHint = new QLabel("将远程文件或目录拖拽到此处即可下载", localPanel);
    dropHint->setObjectName("drop-hint");
    dropHint->setWordWrap(true);
    dropHint->setAlignment(Qt::AlignCenter);
    localLayout->addWidget(dropHint);
    m_dropHint = dropHint;

    auto* dropTree = qobject_cast<LocalDropTree*>(m_localTree);
    connect(dropTree, &LocalDropTree::dragActiveChanged, this, [this](bool active) {
        if (m_dropHint) {
            m_dropHint->setProperty("dropping", active);
            m_dropHint->style()->unpolish(m_dropHint);
            m_dropHint->style()->polish(m_dropHint);
            if (!active) m_dropHint->setText("将远程文件或目录拖拽到此处即可下载");
        }
    });
    connect(dropTree, &LocalDropTree::dropTargetChanged, this, [this](const QString& path) {
        if (m_dropHint) {
            m_dropHint->setText(path.isEmpty()
                ? "将远程文件或目录拖拽到此处即可下载"
                : QString("松开鼠标即可下载到：%1").arg(path));
        }
    });

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

    m_remoteTable = new RemoteDragTable(0, 4, this);
    m_remoteTable->setHorizontalHeaderLabels({"名称", "类型", "大小", "修改时间"});
    m_remoteTable->horizontalHeader()->setStretchLastSection(true);
    m_remoteTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_remoteTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_remoteTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_remoteTable->verticalHeader()->setVisible(false);
    m_remoteTable->setShowGrid(false);
    m_remoteTable->setAlternatingRowColors(true);
    m_remoteTable->setDragEnabled(true);
    m_remoteTable->setDragDropMode(QAbstractItemView::DragOnly);
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

    // ---- Real-time sync (Task 27) ----
    QGroupBox* syncGroup = new QGroupBox("实时同步（本地 → 远程）", this);
    QVBoxLayout* syncLayout = new QVBoxLayout(syncGroup);
    syncLayout->setContentsMargins(6, 6, 6, 6);

    QHBoxLayout* syncBtnLayout = new QHBoxLayout();
    m_addSyncButton = new QPushButton("添加同步目录…", this);
    m_addSyncButton->setFixedWidth(120);
    connect(m_addSyncButton, &QPushButton::clicked, this, &FileTransferWidget::onAddSyncClicked);
    m_removeSyncButton = new QPushButton("移除选中", this);
    m_removeSyncButton->setFixedWidth(90);
    connect(m_removeSyncButton, &QPushButton::clicked, this, &FileTransferWidget::onRemoveSyncClicked);
    m_syncToggleButton = new QPushButton("开始同步", this);
    m_syncToggleButton->setFixedWidth(90);
    connect(m_syncToggleButton, &QPushButton::clicked, this, &FileTransferWidget::onSyncToggleClicked);
    syncBtnLayout->addWidget(m_addSyncButton);
    syncBtnLayout->addWidget(m_removeSyncButton);
    syncBtnLayout->addWidget(m_syncToggleButton);
    syncBtnLayout->addStretch();
    syncLayout->addLayout(syncBtnLayout);

    m_syncList = new QListWidget(this);
    m_syncList->setMaximumHeight(90);
    m_syncList->setSpacing(2);
    syncLayout->addWidget(m_syncList);

    m_syncStatus = new QLabel("未启动", this);
    m_syncStatus->setWordWrap(true);
    syncLayout->addWidget(m_syncStatus);

    mainLayout->addWidget(syncGroup);

    // ---- Reverse real-time sync (remote -> local) ----
    QGroupBox* reverseGroup = new QGroupBox("反向同步（远程 → 本地）", this);
    QVBoxLayout* reverseLayout = new QVBoxLayout(reverseGroup);
    reverseLayout->setContentsMargins(6, 6, 6, 6);

    QHBoxLayout* reverseBtnLayout = new QHBoxLayout();
    m_addReverseButton = new QPushButton("添加反向同步…", this);
    m_addReverseButton->setFixedWidth(120);
    connect(m_addReverseButton, &QPushButton::clicked, this, &FileTransferWidget::onAddReverseSyncClicked);
    m_removeReverseButton = new QPushButton("移除选中", this);
    m_removeReverseButton->setFixedWidth(90);
    connect(m_removeReverseButton, &QPushButton::clicked, this, &FileTransferWidget::onRemoveReverseSyncClicked);
    reverseBtnLayout->addWidget(m_addReverseButton);
    reverseBtnLayout->addWidget(m_removeReverseButton);
    reverseBtnLayout->addStretch();
    reverseLayout->addLayout(reverseBtnLayout);

    m_reverseList = new QListWidget(this);
    m_reverseList->setMaximumHeight(90);
    m_reverseList->setSpacing(2);
    reverseLayout->addWidget(m_reverseList);

    mainLayout->addWidget(reverseGroup);

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

#include "file_transfer_widget.moc"
