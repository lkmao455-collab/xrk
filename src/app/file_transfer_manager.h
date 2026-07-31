#pragma once

#include <QObject>
#include <QHash>
#include <QFile>
#include <QTimer>
#include <QElapsedTimer>
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
    QString downloadFile(const QString& remotePath, const QString& localPath, uint64_t remoteSize = 0);
    
    void cancelTransfer(const QString& fileId);
    void pauseTransfer(const QString& fileId);
    void resumeTransfer(const QString& fileId);
    
    QList<FileRequest> getActiveTransfers() const;
    bool isTransferActive(const QString& fileId) const;

    void setConnection(TcpConnection* connection);

signals:
    void transferStarted(const QString& fileId);
    void transferProgress(const QString& fileId, uint64_t current, uint64_t total);
    void transferCompleted(const QString& fileId);
    void transferFailed(const QString& fileId, const QString& errorString);
    void transferCancelled(const QString& fileId);

private slots:
    void onConnectionDataReceived(const QByteArray& data);
    void onSendTimer();

private:
    void processFileRequest(const QByteArray& data);
    void processFileData(const QByteArray& data);
    void sendNextChunk(const QString& fileId);

    TcpConnection* m_connection = nullptr;
    QTimer* m_sendTimer = nullptr;
    
    struct TransferInfo {
        FileRequest request;
        QFile* file = nullptr;
        bool paused = false;
        QElapsedTimer elapsed;
        uint64_t bytesSent = 0;
    };
    QHash<QString, TransferInfo> m_transfers;
    static constexpr int CHUNK_SIZE = 65536;
};

} // namespace xrk
