#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QString>
#include <functional>

namespace xrk {

// Real-time one-way file sync: watches local directories and uploads any
// newly created or modified file to a paired remote (host-side) directory.
//
// The manager is decoupled from the actual transfer mechanism via an upload
// callback, so it is trivially testable: the UI wires the callback to
// FileTransferManager::uploadFileTo(...), while unit tests inject a fake that
// just records the (localFile, remotePath) pairs.
class FileSyncManager : public QObject {
    Q_OBJECT
public:
    // Returns a transfer/file id (empty string on failure), mirroring
    // FileTransferManager::uploadFileTo's contract.
    using UploadFunc = std::function<QString(const QString&, const QString&)>;

    explicit FileSyncManager(QObject* parent = nullptr);
    ~FileSyncManager();

    void setUploadCallback(UploadFunc fn);

    // Pair a local directory with a remote (host-side) directory. Returns
    // false if the local directory is already paired. If the manager is
    // already running, the new pair is watched immediately and an initial
    // sync uploads every existing file under it.
    bool addPair(const QString& localDir, const QString& remoteDir);
    void removePair(const QString& localDir);
    void clearPairs();
    bool hasPair(const QString& localDir) const;
    QStringList pairKeys() const;

    void start();
    void stop();
    bool isRunning() const;

    // Force a rescan of a paired local directory (also used by the directory
    // watcher). Public so tests can drive it deterministically.
    void rescan(const QString& localDir);

    // Gate uploads on an active session. While false, pending uploads are
    // dropped (so we don't spam errors when the controller is disconnected).
    void setCanUpload(bool can);
    bool canUpload() const;

    // Pure mapping helpers (unit-tested without a filesystem watcher).
    static QString relativePath(const QString& root, const QString& file);
    static QString remoteTargetPath(const QString& remoteRoot, const QString& relative);

signals:
    void pairAdded(const QString& localDir, const QString& remoteDir);
    void pairRemoved(const QString& localDir);
    void started();
    void stopped();
    void fileSyncing(const QString& localFile, const QString& remotePath);
    void fileSynced(const QString& localFile);
    void syncError(const QString& message);

private slots:
    void onDirectoryChanged(const QString& dir);
    void onDebounceTimer();

private:
    struct Pair {
        QString localDir;
        QString remoteDir;
        QSet<QString> watchedDirs;                       // native-abs dir paths
        QHash<QString, QPair<qint64, qint64>> snapshot;  // absFile -> (size, mtime)
    };

    void watchRecursively(Pair& pair);
    void scanDir(Pair& pair, const QString& dir);
    void debouncedUpload(const QString& localFile, const QString& remotePath);

    UploadFunc m_uploadFn;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_debounceTimer = nullptr;
    QHash<QString, Pair> m_pairs;            // keyed by canonical local dir
    QHash<QString, QString> m_pendingUploads; // localFile -> remotePath (debounced)
    bool m_running = false;
    bool m_canUpload = false;
};

} // namespace xrk
