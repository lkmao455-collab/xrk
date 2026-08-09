#pragma once

#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QSplitter>
#include <QListWidget>
#include <QProgressBar>
#include <QLabel>
#include <QMap>
#include <QSet>
#include "core/types.h"

namespace xrk {

class FileTransferManager;
class RemoteController;
class FileSyncManager;

class FileTransferWidget : public QWidget {
    Q_OBJECT
public:
    explicit FileTransferWidget(FileTransferManager* manager, QWidget* parent = nullptr);
    ~FileTransferWidget();

    void setRemoteController(RemoteController* controller);

signals:
    void uploadRequested(const QString& localPath, const QString& remoteDir);
    void downloadRequested(const QString& remotePath, const QString& localDir);

public slots:
    void onFileBrowserReceived(const FileBrowserResponse& response);
    void onRemoteConnected();
    void onRemoteDisconnected();

private slots:
    void onUploadClicked();
    void onDownloadClicked();
    void onRenameClicked();
    void onDeleteClicked();
    void onMkdirClicked();
    void onFileOpCompleted(const FileOpResponse& resp);
    void onCancelClicked();
    void onAddSyncClicked();
    void onRemoveSyncClicked();
    void onSyncToggleClicked();
    void onAddReverseSyncClicked();
    void onRemoveReverseSyncClicked();
    void onSyncNotifyReceived(const SyncNotify& note);
    void onTransferStarted(const QString& fileId);
    void onTransferProgress(const QString& fileId, uint64_t current, uint64_t total);
    void onTransferCompleted(const QString& fileId);
    void onTransferFailed(const QString& fileId, const QString& errorString);
    void onLocalDoubleClicked(const QModelIndex& index);
    void onRemoteDoubleClicked(int row, int column);
    void onRemotePathChanged();
    void onRemoteGoUp();
    void onRefreshRemote();
    void onRemoteDriveChanged(int index);
    void onRemoteDropped(const QMimeData* mime, const QModelIndex& index);

private:
    void setupUI();
    QWidget* createTransferItem(const QString& fileId, const QString& fileName);
    void requestRemoteDir(const QString& path);
    void populateRemoteTable(const FileBrowserResponse& resp);
    void populateDriveList();
    QString selectedRemotePath() const;

    // Drag-and-drop download (remote -> local)
    void downloadRemoteItem(const QString& remotePath, const QString& name,
                            bool isDir, uint64_t size, const QString& localDir);
    void startDirectoryDownload(const QString& remotePath, const QString& localPath);
    void scanNextDir();

    FileTransferManager* m_manager = nullptr;
    RemoteController* m_remoteController = nullptr;

    // Directory download state (async remote enumeration)
    struct PendingDir { QString remotePath; QString localPath; };
    QList<PendingDir> m_dirScanQueue;
    QSet<QString> m_activeScans;
    QMap<QString, QString> m_dirScanMap;

    // Local browser
    QTreeView* m_localTree = nullptr;
    QFileSystemModel* m_localModel = nullptr;
    QLabel* m_dropHint = nullptr;

    // Remote browser
    QLineEdit* m_remotePathEdit = nullptr;
    QTableWidget* m_remoteTable = nullptr;
    QPushButton* m_remoteUpButton = nullptr;
    QPushButton* m_remoteRefreshButton = nullptr;
    QComboBox* m_remoteDriveCombo = nullptr;

    // Transfer queue
    QListWidget* m_transferList = nullptr;
    QPushButton* m_uploadButton = nullptr;
    QPushButton* m_downloadButton = nullptr;
    QPushButton* m_renameButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QPushButton* m_mkdirButton = nullptr;
    QPushButton* m_cancelButton = nullptr;

    QMap<QString, QWidget*> m_transferWidgets;
    QString m_currentRemotePath;
    bool m_remoteConnected = false;

    // Real-time sync (Task 27): watch local folders and upload changes to a
    // paired remote folder on the host.
    FileSyncManager* m_sync = nullptr;
    QListWidget* m_syncList = nullptr;
    QPushButton* m_addSyncButton = nullptr;
    QPushButton* m_removeSyncButton = nullptr;
    QPushButton* m_syncToggleButton = nullptr;
    QLabel* m_syncStatus = nullptr;

    // Reverse sync UI
    QListWidget* m_reverseList = nullptr;
    QPushButton* m_addReverseButton = nullptr;
    QPushButton* m_removeReverseButton = nullptr;

    void addSyncListItem(const QString& localDir, const QString& remoteDir);
    void addReverseSyncListItem(const QString& hostDir, const QString& localDir);

    // Reverse real-time sync (remote -> local). hostDir is the watched directory
    // on the controlled machine; localDir is where changes land on this side.
    QMap<QString, QString> m_reversePairs;  // hostDir -> localDir
};

} // namespace xrk
