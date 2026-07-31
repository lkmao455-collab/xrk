#include "file_transfer_manager.h"
#include "core/tcp_connection.h"
#include "core/protocol_manager.h"
#include "core/logger.h"
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QTimer>

namespace xrk {

FileTransferManager::FileTransferManager(TcpConnection* connection, QObject* parent)
    : QObject(parent), m_connection(connection) {
    
    m_sendTimer = new QTimer(this);
    connect(m_sendTimer, &QTimer::timeout, this, &FileTransferManager::onSendTimer);
    m_sendTimer->start(10);
    
    setConnection(connection);
}

FileTransferManager::~FileTransferManager() {
    m_sendTimer->stop();
    for (auto it = m_transfers.begin(); it != m_transfers.end(); ++it) {
        if (it.value().file && it.value().file->isOpen()) {
            it.value().file->close();
        }
        delete it.value().file;
    }
}

QString FileTransferManager::uploadFile(const QString& filePath) {
    QFile* file = new QFile(filePath);
    if (!file->exists()) {
        LOG_ERROR("File not found: " + filePath);
        delete file;
        return QString();
    }
    
    QFileInfo fileInfo(filePath);
    QString fileId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    FileRequest request;
    request.fileId = fileId;
    request.fileName = fileInfo.fileName();
    request.fileSize = fileInfo.size();
    request.offset = 0;
    request.isUpload = true;
    
    if (!file->open(QIODevice::ReadOnly)) {
        LOG_ERROR("Failed to open file: " + filePath);
        delete file;
        return QString();
    }
    
    TransferInfo transfer;
    transfer.request = request;
    transfer.file = file;
    transfer.elapsed.start();
    
    m_transfers[fileId] = transfer;
    
    QByteArray payload = ProtocolManager::encodeFileRequest(request);
    QByteArray message = ProtocolManager::encode(MessageType::FILE_REQ, payload);
    if (m_connection) {
        m_connection->send(message);
    }
    
    emit transferStarted(fileId);
    emit transferProgress(fileId, 0, request.fileSize);
    LOG_INFO("File upload started: " + filePath + " (" + QString::number(request.fileSize) + " bytes)");
    
    return fileId;
}

QString FileTransferManager::downloadFile(const QString& remotePath, const QString& localPath, uint64_t remoteSize) {
    QString fileId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QFileInfo fileInfo(remotePath);

    FileRequest request;
    request.fileId = fileId;
    request.fileName = fileInfo.fileName();
    request.path = remotePath;
    request.fileSize = remoteSize;
    request.offset = 0;
    request.isUpload = false;
    
    QFile* file = new QFile(localPath);
    if (!file->open(QIODevice::WriteOnly)) {
        LOG_ERROR("Failed to create file: " + localPath);
        delete file;
        return QString();
    }
    
    TransferInfo transfer;
    transfer.request = request;
    transfer.file = file;
    
    m_transfers[fileId] = transfer;
    
    QByteArray payload = ProtocolManager::encodeFileRequest(request);
    QByteArray message = ProtocolManager::encode(MessageType::FILE_REQ, payload);
    if (m_connection) {
        m_connection->send(message);
    }
    
    emit transferStarted(fileId);
    LOG_INFO("File download started: " + remotePath);
    
    return fileId;
}

void FileTransferManager::cancelTransfer(const QString& fileId) {
    if (m_transfers.contains(fileId)) {
        TransferInfo& transfer = m_transfers[fileId];
        if (transfer.file && transfer.file->isOpen()) {
            transfer.file->close();
        }
        delete transfer.file;
        m_transfers.remove(fileId);
        emit transferCancelled(fileId);
        LOG_INFO("File transfer cancelled: " + fileId);
    }
}

void FileTransferManager::pauseTransfer(const QString& fileId) {
    if (m_transfers.contains(fileId)) {
        m_transfers[fileId].paused = true;
        LOG_DEBUG("File transfer paused: " + fileId);
    }
}

void FileTransferManager::resumeTransfer(const QString& fileId) {
    if (m_transfers.contains(fileId)) {
        m_transfers[fileId].paused = false;
        LOG_DEBUG("File transfer resumed: " + fileId);
    }
}

QList<FileRequest> FileTransferManager::getActiveTransfers() const {
    QList<FileRequest> requests;
    for (auto it = m_transfers.begin(); it != m_transfers.end(); ++it) {
        requests.append(it.value().request);
    }
    return requests;
}

bool FileTransferManager::isTransferActive(const QString& fileId) const {
    return m_transfers.contains(fileId);
}

void FileTransferManager::setConnection(TcpConnection* connection) {
    if (m_connection == connection) return;
    
    if (m_connection) {
        disconnect(m_connection, &TcpConnection::readyRead, this, &FileTransferManager::onConnectionDataReceived);
    }
    
    m_connection = connection;
    
    if (m_connection) {
        connect(m_connection, &TcpConnection::readyRead, this, &FileTransferManager::onConnectionDataReceived);
    }
}

void FileTransferManager::onConnectionDataReceived(const QByteArray& data) {
    MessageType type;
    QByteArray payload;
    QString sessionId;
    
    if (!ProtocolManager::decode(data, type, payload, sessionId)) {
        return;
    }
    
    switch (type) {
        case MessageType::FILE_REQ:
            processFileRequest(payload);
            break;
        case MessageType::FILE_DATA:
            processFileData(payload);
            break;
        case MessageType::FILE_ACK:
            break;
        default:
            break;
    }
}

void FileTransferManager::onSendTimer() {
    for (auto it = m_transfers.begin(); it != m_transfers.end(); ++it) {
        if (it.value().request.isUpload && !it.value().paused) {
            sendNextChunk(it.key());
        }
    }
}

void FileTransferManager::processFileRequest(const QByteArray& data) {
    FileRequest request = ProtocolManager::decodeFileRequest(data);
    
    if (request.isUpload) {
        LOG_DEBUG("File request received (upload): " + request.fileName);
    } else {
        LOG_DEBUG("File request received (download): " + request.fileName);
    }
}

void FileTransferManager::processFileData(const QByteArray& data) {
    FileData fileData = ProtocolManager::decodeFileData(data);
    
    if (!m_transfers.contains(fileData.fileId)) {
        return;
    }
    
    TransferInfo& transfer = m_transfers[fileData.fileId];
    if (!transfer.file || !transfer.file->isOpen()) {
        return;
    }

    // Empty data payload is the host's end-of-transfer marker.
    if (fileData.data.isEmpty()) {
        transfer.file->close();
        delete transfer.file;
        transfer.file = nullptr;
        m_transfers.remove(fileData.fileId);
        emit transferProgress(fileData.fileId, transfer.request.fileSize, transfer.request.fileSize);
        emit transferCompleted(fileData.fileId);
        LOG_INFO("File download completed: " + fileData.fileId);
        return;
    }

    transfer.file->seek(fileData.offset);
    transfer.file->write(fileData.data);

    transfer.bytesSent += fileData.data.size();
    transfer.request.offset = fileData.offset + fileData.data.size();

    emit transferProgress(fileData.fileId, transfer.bytesSent, transfer.request.fileSize);

    if (transfer.bytesSent >= transfer.request.fileSize && transfer.request.fileSize > 0) {
        transfer.file->close();
        delete transfer.file;
        transfer.file = nullptr;
        m_transfers.remove(fileData.fileId);
        emit transferCompleted(fileData.fileId);
        LOG_INFO("File download completed: " + fileData.fileId);
    }
}

void FileTransferManager::sendNextChunk(const QString& fileId) {
    if (!m_transfers.contains(fileId)) {
        return;
    }
    
    TransferInfo& transfer = m_transfers[fileId];
    if (transfer.paused || !transfer.file || !transfer.file->isOpen()) {
        return;
    }
    
    QByteArray chunk = transfer.file->read(CHUNK_SIZE);
    if (chunk.isEmpty()) {
        transfer.file->close();
        delete transfer.file;
        transfer.file = nullptr;
        m_transfers.remove(fileId);
        emit transferCompleted(fileId);
        LOG_INFO("File upload completed: " + fileId);
        return;
    }
    
    FileData fileData;
    fileData.fileId = fileId;
    fileData.data = chunk;
    fileData.offset = transfer.request.offset;
    
    QByteArray payload = ProtocolManager::encodeFileData(fileData);
    QByteArray message = ProtocolManager::encode(MessageType::FILE_DATA, payload);
    if (m_connection) {
        m_connection->send(message);
    }
    
    transfer.request.offset += chunk.size();
    transfer.bytesSent += chunk.size();
    
    emit transferProgress(fileId, transfer.bytesSent, transfer.request.fileSize);
}

} // namespace xrk
