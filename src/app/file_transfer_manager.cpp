#include "file_transfer_manager.h"
#include "core/tcp_connection.h"
#include "core/protocol_manager.h"
#include "core/logger.h"
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QTimer>
#include <QCryptographicHash>

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

QString FileTransferManager::uploadFileTo(const QString& filePath, const QString& remotePath) {
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
    request.path = remotePath;
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
    LOG_INFO("File upload (to " + remotePath + ") started: " + filePath);

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
        TransferInfo& transfer = m_transfers[fileId];
        transfer.paused = false;
        emit transferResumed(fileId, transfer.request.offset);
        LOG_DEBUG("File transfer resumed: " + fileId);
    }
}

uint64_t FileTransferManager::getTransferOffset(const QString& fileId) const {
    if (!m_transfers.contains(fileId)) {
        return 0;
    }
    return m_transfers.value(fileId).request.offset;
}

QByteArray FileTransferManager::getTransferChecksum(const QString& fileId) const {
    if (!m_transfers.contains(fileId)) {
        return QByteArray();
    }
    return m_transfers.value(fileId).checksum;
}

bool FileTransferManager::canResumeTransfer(const QString& fileId) const {
    if (!m_transfers.contains(fileId)) {
        return false;
    }
    const TransferInfo& transfer = m_transfers.value(fileId);
    return transfer.file != nullptr && transfer.file->isOpen() && transfer.request.offset > 0;
}

QString FileTransferManager::resumeTransfer(const QString& fileId, uint64_t offset) {
    if (!m_transfers.contains(fileId)) {
        return QString();
    }

    TransferInfo& transfer = m_transfers[fileId];
    if (!transfer.file || !transfer.file->isOpen()) {
        return QString();
    }

    // Seek to the resume offset
    if (!transfer.file->seek(offset)) {
        LOG_ERROR("Failed to seek to offset " + QString::number(offset) + " for file: " + fileId);
        return QString();
    }

    transfer.request.offset = offset;
    transfer.resumeOffset = offset;
    transfer.paused = false;

    // Send resume request to peer
    FileRequest resumeRequest = transfer.request;
    resumeRequest.offset = offset;
    QByteArray payload = ProtocolManager::encodeFileRequest(resumeRequest);
    QByteArray message = ProtocolManager::encode(MessageType::FILE_REQ, payload);
    if (m_connection) {
        m_connection->send(message);
    }

    emit transferResumed(fileId, offset);
    LOG_INFO("File transfer resumed: " + fileId + " at offset " + QString::number(offset));

    return fileId;
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
        case MessageType::FILE_CHECKSUM: {
            QString fileId;
            QByteArray checksum = ProtocolManager::decodeFileChecksum(payload, fileId);
            processFileChecksum(fileId, checksum);
            break;
        }
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

    // Handle resume request: if offset > 0, the peer is resuming an interrupted transfer
    if (request.offset > 0) {
        LOG_INFO("Resume request received for file: " + request.fileId + " at offset " + QString::number(request.offset));
    }
}

void FileTransferManager::processFileChecksum(const QString& fileId, const QByteArray& checksum) {
    if (!m_transfers.contains(fileId)) {
        return;
    }

    TransferInfo& transfer = m_transfers[fileId];
    transfer.checksum = checksum;

    // Verify checksum against the received file
    if (transfer.file && transfer.file->isOpen()) {
        transfer.file->close();
    }

    QFile* verifyFile = new QFile(transfer.request.path);
    if (verifyFile->open(QIODevice::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!verifyFile->atEnd()) {
            hash.addData(verifyFile->read(CHUNK_SIZE));
        }
        QByteArray computedChecksum = hash.result();
        transfer.checksumVerified = (computedChecksum == checksum);
        verifyFile->close();
    }
    delete verifyFile;

    if (transfer.checksumVerified) {
        LOG_INFO("File checksum verified successfully: " + fileId);
    } else {
        LOG_ERROR("File checksum verification failed: " + fileId);
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
        finalizeDownload(fileData.fileId);
        return;
    }

    transfer.file->seek(fileData.offset);
    transfer.file->write(fileData.data);

    transfer.bytesSent += fileData.data.size();
    transfer.request.offset = fileData.offset + fileData.data.size();

    emit transferProgress(fileData.fileId, transfer.bytesSent, transfer.request.fileSize);

    if (transfer.bytesSent >= transfer.request.fileSize && transfer.request.fileSize > 0) {
        finalizeDownload(fileData.fileId);
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
        finalizeUpload(fileId);
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

void FileTransferManager::finalizeUpload(const QString& fileId) {
    if (!m_transfers.contains(fileId)) {
        return;
    }

    TransferInfo& transfer = m_transfers[fileId];

    // Compute SHA-256 checksum for integrity verification
    if (transfer.file) {
        transfer.file->close();
    }

    QFile* checkFile = new QFile(transfer.request.path);
    if (checkFile->open(QIODevice::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!checkFile->atEnd()) {
            hash.addData(checkFile->read(CHUNK_SIZE));
        }
        transfer.checksum = hash.result();
        transfer.checksumVerified = true;
        checkFile->close();
    }
    delete checkFile;

    // Send checksum to peer for verification
    QByteArray checksumPayload = transfer.checksum;
    QByteArray checksumMessage = ProtocolManager::encode(MessageType::FILE_CHECKSUM, checksumPayload);
    if (m_connection) {
        m_connection->send(checksumMessage);
    }

    if (transfer.file) {
        delete transfer.file;
        transfer.file = nullptr;
    }
    m_transfers.remove(fileId);

    emit transferCompleted(fileId);
    LOG_INFO("File upload completed with checksum: " + fileId);
}

void FileTransferManager::finalizeDownload(const QString& fileId) {
    if (!m_transfers.contains(fileId)) {
        return;
    }

    TransferInfo& transfer = m_transfers[fileId];

    if (transfer.file && transfer.file->isOpen()) {
        transfer.file->close();
    }

    // Compute SHA-256 checksum for integrity verification
    if (transfer.file) {
        QFile* checkFile = new QFile(transfer.request.path);
        if (checkFile->open(QIODevice::ReadOnly)) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            while (!checkFile->atEnd()) {
                hash.addData(checkFile->read(CHUNK_SIZE));
            }
            transfer.checksum = hash.result();
            transfer.checksumVerified = true;
            checkFile->close();
        }
        delete checkFile;
    }

    if (transfer.file) {
        delete transfer.file;
        transfer.file = nullptr;
    }
    m_transfers.remove(fileId);

    emit transferCompleted(fileId);
    LOG_INFO("File download completed with checksum: " + fileId);
}

} // namespace xrk
