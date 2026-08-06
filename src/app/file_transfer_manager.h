#pragma once

#include <QObject>
#include <QHash>
#include <QFile>
#include <QTimer>
#include <QElapsedTimer>
#include <QByteArray>
#include <memory>
#include "core/types.h"

namespace xrk {

class TcpConnection;

class FileTransferManager : public QObject {
    Q_OBJECT
public:
    explicit FileTransferManager(TcpConnection* connection, QObject* parent = nullptr);
    ~FileTransferManager();

    QString uploadFile(const QString& filePath);
    QString uploadFileTo(const QString& filePath, const QString& remotePath);
    QString downloadFile(const QString& remotePath, const QString& localPath, uint64_t remoteSize = 0);

    void cancelTransfer(const QString& fileId);
    void pauseTransfer(const QString& fileId);
    void resumeTransfer(const QString& fileId);

    // Resumable transfer: get the current offset for a transfer
    uint64_t getTransferOffset(const QString& fileId) const;
    // Resumable transfer: get the checksum (SHA-256) for a completed transfer
    QByteArray getTransferChecksum(const QString& fileId) const;
    // Resumable transfer: check if a transfer can be resumed
    bool canResumeTransfer(const QString& fileId) const;
    // Resumable transfer: resume from a specific offset
    QString resumeTransfer(const QString& fileId, uint64_t offset);

    QList<FileRequest> getActiveTransfers() const;
    bool isTransferActive(const QString& fileId) const;

    void setConnection(TcpConnection* connection);

signals:
    void transferStarted(const QString& fileId);
    void transferProgress(const QString& fileId, uint64_t current, uint64_t total);
    void transferCompleted(const QString& fileId);
    void transferFailed(const QString& fileId, const QString& errorString);
    void transferCancelled(const QString& fileId);
    void transferResumed(const QString& fileId, uint64_t offset);

private slots:
    void onConnectionDataReceived(const QByteArray& data);
    void onSendTimer();

private:
    void processFileRequest(const QByteArray& data);
    void processFileData(const QByteArray& data);
    void processFileChecksum(const QString& fileId, const QByteArray& checksum);
    void sendNextChunk(const QString& fileId);
    void finalizeUpload(const QString& fileId);
    void finalizeDownload(const QString& fileId);

    TcpConnection* m_connection = nullptr;
    QTimer* m_sendTimer = nullptr;

    struct TransferInfo {
        FileRequest request;
        QFile* file = nullptr;
        bool paused = false;
        QElapsedTimer elapsed;
        uint64_t bytesSent = 0;
        QByteArray checksum;           // SHA-256 checksum for integrity verification
        bool checksumVerified = false;
        uint64_t resumeOffset = 0;     // Offset to resume from for interrupted transfers
        QString sessionId;             // Session ID for resumable transfer tracking
    };
    QHash<QString, TransferInfo> m_transfers;
    static constexpr int CHUNK_SIZE = 65536;
};

} // namespace xrk
