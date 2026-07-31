#include "file_sync_manager.h"
#include "core/logger.h"

#include <QDir>
#include <QFileInfo>
#include <QDateTime>

namespace xrk {

FileSyncManager::FileSyncManager(QObject* parent) : QObject(parent) {
}

FileSyncManager::~FileSyncManager() {
    stop();
}

void FileSyncManager::setUploadCallback(UploadFunc fn) {
    m_uploadFn = std::move(fn);
}

QString FileSyncManager::relativePath(const QString& root, const QString& file) {
    QString r = QDir::fromNativeSeparators(root);
    QString f = QDir::fromNativeSeparators(QFileInfo(file).absoluteFilePath());
    if (!r.endsWith('/')) r += '/';
    if (f.startsWith(r)) {
        QString rel = f.mid(r.length());
        return rel;
    }
    // Fallback: basename (e.g. root not an ancestor, should not happen).
    return QFileInfo(file).fileName();
}

QString FileSyncManager::remoteTargetPath(const QString& remoteRoot, const QString& relative) {
    QString r = remoteRoot;
    if (!r.endsWith('/') && !r.endsWith('\\')) r += '/';
    return QDir::toNativeSeparators(r + relative);
}

bool FileSyncManager::addPair(const QString& localDir, const QString& remoteDir) {
    QString key = QDir::toNativeSeparators(QDir(localDir).absolutePath());
    if (key.isEmpty() || m_pairs.contains(key)) return false;

    Pair pair;
    pair.localDir = key;
    pair.remoteDir = remoteDir;

    if (m_running) {
        pair.watchedDirs.insert(key);
        if (m_watcher) m_watcher->addPath(key);
        scanDir(pair, key); // initial sync uploads existing files
    }

    m_pairs[key] = pair;
    emit pairAdded(key, remoteDir);
    LOG_INFO("FileSync: added pair " + key + " -> " + remoteDir);
    return true;
}

void FileSyncManager::removePair(const QString& localDir) {
    QString key = QDir::toNativeSeparators(QDir(localDir).absolutePath());
    if (!m_pairs.contains(key)) return;
    m_pairs.remove(key);
    emit pairRemoved(key);
    LOG_INFO("FileSync: removed pair " + key);
}

void FileSyncManager::clearPairs() {
    m_pairs.clear();
}

bool FileSyncManager::hasPair(const QString& localDir) const {
    QString key = QDir::toNativeSeparators(QDir(localDir).absolutePath());
    return m_pairs.contains(key);
}

QStringList FileSyncManager::pairKeys() const {
    return m_pairs.keys();
}

void FileSyncManager::start() {
    if (m_running) return;
    m_running = true;

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &FileSyncManager::onDirectoryChanged);

    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    connect(m_debounceTimer, &QTimer::timeout, this, &FileSyncManager::onDebounceTimer);

    for (auto it = m_pairs.begin(); it != m_pairs.end(); ++it) {
        Pair& pair = it.value();
        pair.watchedDirs.clear();
        pair.snapshot.clear();
        pair.watchedDirs.insert(it.key());
        if (m_watcher) m_watcher->addPath(it.key());
        scanDir(pair, it.key()); // initial upload of existing files
    }

    emit started();
    LOG_INFO("FileSync: started, " + QString::number(m_pairs.size()) + " pair(s)");
}

void FileSyncManager::stop() {
    if (!m_running) return;
    m_running = false;

    if (m_debounceTimer) {
        m_debounceTimer->stop();
        delete m_debounceTimer;
        m_debounceTimer = nullptr;
    }
    delete m_watcher;
    m_watcher = nullptr;
    m_pendingUploads.clear();

    emit stopped();
    LOG_INFO("FileSync: stopped");
}

bool FileSyncManager::isRunning() const {
    return m_running;
}

void FileSyncManager::rescan(const QString& localDir) {
    QString key = QDir::toNativeSeparators(QDir(localDir).absolutePath());
    if (!m_pairs.contains(key)) return;
    scanDir(m_pairs[key], key);
}

void FileSyncManager::setCanUpload(bool can) {
    m_canUpload = can;
    if (!can) {
        m_pendingUploads.clear();
        if (m_debounceTimer) m_debounceTimer->stop();
    }
}

bool FileSyncManager::canUpload() const {
    return m_canUpload;
}

void FileSyncManager::scanDir(Pair& pair, const QString& dir) {
    QDir qdir(dir);
    if (!qdir.exists()) return;

    QFileInfoList entries = qdir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
    QSet<QString> currentFiles;
    for (const QFileInfo& fi : entries) {
        QString abs = QDir::toNativeSeparators(fi.absoluteFilePath());
        if (fi.isDir()) {
            if (!pair.watchedDirs.contains(abs)) {
                pair.watchedDirs.insert(abs);
                if (m_watcher) m_watcher->addPath(abs);
                scanDir(pair, abs); // recurse into newly discovered subdir
            }
        } else if (fi.isFile()) {
            currentFiles.insert(abs);
            qint64 size = fi.size();
            qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
            auto it = pair.snapshot.find(abs);
            if (it == pair.snapshot.end() || it->first != size || it->second != mtime) {
                pair.snapshot[abs] = qMakePair(size, mtime);
                QString rel = relativePath(pair.localDir, abs);
                QString remote = remoteTargetPath(pair.remoteDir, rel);
                debouncedUpload(abs, remote);
            }
        }
    }

    // Drop deleted files from the snapshot so re-creation is detected later.
    for (auto it = pair.snapshot.begin(); it != pair.snapshot.end();) {
        if (!currentFiles.contains(it.key())) {
            it = pair.snapshot.erase(it);
        } else {
            ++it;
        }
    }
}

void FileSyncManager::onDirectoryChanged(const QString& dir) {
    if (!m_running) return;
    QString nd = QDir::toNativeSeparators(dir);
    for (auto it = m_pairs.begin(); it != m_pairs.end(); ++it) {
        Pair& pair = it.value();
        if (pair.watchedDirs.contains(nd)) {
            scanDir(pair, nd);
            return;
        }
    }
}

void FileSyncManager::debouncedUpload(const QString& localFile, const QString& remotePath) {
    m_pendingUploads[localFile] = remotePath;
    if (m_debounceTimer && m_canUpload) {
        m_debounceTimer->start(400);
    }
}

void FileSyncManager::onDebounceTimer() {
    if (!m_canUpload) {
        m_pendingUploads.clear();
        return;
    }

    for (auto it = m_pendingUploads.begin(); it != m_pendingUploads.end(); ++it) {
        const QString local = it.key();
        const QString remote = it.value();
        emit fileSyncing(local, remote);
        if (m_uploadFn) {
            const QString id = m_uploadFn(local, remote);
            if (id.isEmpty()) {
                emit syncError("上传失败: " + local);
            } else {
                emit fileSynced(local);
            }
        }
    }
    m_pendingUploads.clear();
}

} // namespace xrk
