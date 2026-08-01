#include "ipmsg_manager.h"
#include "core/logger.h"
#include <QJsonArray>
#include <QNetworkAddressEntry>
#include <QStandardPaths>
#include <QDateTime>
#include <QUuid>
#include <QFileInfo>
#include <QDirIterator>
#include <QMutex>
#include <QMutexLocker>
#include <QFileDevice>

namespace xrk {

// Protocol constants
static const quint16 IPMSG_DEFAULT_PORT = 2425;
static const QByteArray IPMSG_HELLO = "HELLO";
static const QByteArray IPMSG_LEAVE = "LEAVE";
static const QByteArray IPMSG_MSG = "MSG";
static const QByteArray IPMSG_FILE_START = "FILE_START";
static const QByteArray IPMSG_FILE_DATA = "FILE_DATA";
static const QByteArray IPMSG_FILE_END = "FILE_END";
static const QByteArray IPMSG_FILE_ACCEPT = "FILE_ACCEPT";
static const QByteArray IPMSG_FILE_REJECT = "FILE_REJECT";
static const QByteArray IPMSG_FILE_RESUME = "FILE_RESUME";
static const QByteArray IPMSG_FILE_RESUME_REQUEST = "FILE_RESUME_REQUEST";
static const QByteArray IPMSG_FILE_VERIFY = "FILE_VERIFY";

IPMsgManager::IPMsgManager(QObject* parent)
    : QObject(parent)
    , m_port(IPMSG_DEFAULT_PORT)
    , m_userId(QUuid::createUuid().toString().remove('{').remove('}').remove('-').left(8))
    , m_transferStateFile(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/ipmsg_transfers.json") {
    loadTransferStates();
}

IPMsgManager::~IPMsgManager() {
    stop();
}

bool IPMsgManager::start(quint16 port) {
    if (m_running) return true;

    m_port = port;

    // Setup UDP socket for broadcast
    m_udpSocket = new QUdpSocket(this);
    if (!m_udpSocket->bind(QHostAddress::Any, m_port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        LOG_ERROR("IPMsg: Failed to bind UDP port " + QString::number(m_port));
        return false;
    }
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &IPMsgManager::onUdpBroadcastReceived);

    // Setup TCP server for file transfers
    m_tcpServer = new QTcpServer(this);
    if (!m_tcpServer->listen(QHostAddress::Any, m_port + 1)) {
        LOG_ERROR("IPMsg: Failed to start TCP server on port " + QString::number(m_port + 1));
        return false;
    }
    connect(m_tcpServer, &QTcpServer::newConnection, this, &IPMsgManager::onNewTcpConnection);

    m_running = true;
    broadcastPresence();
    LOG_INFO("IPMsg: Started on port " + QString::number(m_port));

    return true;
}

void IPMsgManager::stop() {
    if (!m_running) return;

    // Broadcast leave message
    QByteArray data = IPMSG_LEAVE + "|" + m_userId.toUtf8();
    sendUdpBroadcast(data);

    // Close all connections
    for (auto* socket : m_sendingSockets) {
        socket->close();
    }
    m_sendingSockets.clear();

    if (m_udpSocket) {
        m_udpSocket->close();
        delete m_udpSocket;
        m_udpSocket = nullptr;
    }

    if (m_tcpServer) {
        m_tcpServer->close();
        delete m_tcpServer;
        m_tcpServer = nullptr;
    }

    m_devices.clear();
    m_running = false;
    LOG_INFO("IPMsg: Stopped");
}

bool IPMsgManager::isRunning() const {
    return m_running;
}

void IPMsgManager::broadcastPresence() {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["port"] = m_port;
    json["command"] = QString::fromUtf8(IPMSG_HELLO);

    QJsonDocument doc(json);
    sendUdpBroadcast(doc.toJson());
}

void IPMsgManager::sendMessage(const QString& targetIp, const QString& message) {
    QJsonObject json;
    json["id"] = m_userId;
    json["name"] = m_userName;
    json["command"] = QString::fromUtf8(IPMSG_MSG);
    json["message"] = message;
    json["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(json);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [this, socket, data = doc.toJson()]() {
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::bytesWritten, this, [this, socket](qint64) {
        socket->deleteLater();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
        emit error("Failed to send message: " + socket->errorString());
        socket->deleteLater();
    });

    socket->connectToHost(targetIp, m_port + 1);
}

void IPMsgManager::sendFile(const QString& targetIp, const QString& filePath) {
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        emit fileError("", "File not found: " + filePath);
        return;
    }

    QList<IPMsgFileItem> items;
    IPMsgFileItem item;
    item.name = fileInfo.fileName();
    item.absolutePath = fileInfo.absoluteFilePath();
    item.relativePath = fileInfo.fileName();
    item.size = fileInfo.size();
    item.isDirectory = fileInfo.isDir();
    item.md5 = calculateFileMd5(filePath);

    if (fileInfo.isDir()) {
        item.size = calculateFolderSize(filePath);
        QDir dir(filePath);
        QDirIterator it(filePath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            IPMsgFileItem child;
            child.name = it.fileName();
            child.absolutePath = it.filePath();
            child.relativePath = dir.relativeFilePath(it.filePath());
            child.size = it.fileInfo().size();
            child.isDirectory = it.fileInfo().isDir();
            if (!child.isDirectory) {
                child.md5 = calculateFileMd5(it.filePath());
            }
            item.children.append(child);
        }
    }

    items.append(item);
    sendFileData(targetIp, items);
}

void IPMsgManager::sendFolder(const QString& targetIp, const QString& folderPath) {
    sendFile(targetIp, folderPath);
}

void IPMsgManager::acceptFile(const QString& fileId, const QString& savePath) {
    QJsonObject json;
    json["command"] = QString::fromUtf8(IPMSG_FILE_ACCEPT);
    json["fileId"] = fileId;
    json["savePath"] = savePath;

    QJsonDocument doc(json);

    if (m_pendingFiles.contains(fileId)) {
        QTcpSocket* socket = m_sendingSockets.value(fileId);
        if (socket && socket->isOpen()) {
            socket->write(doc.toJson());
            socket->flush();
        }
    }

    m_pendingFiles.remove(fileId);
}

void IPMsgManager::rejectFile(const QString& fileId) {
    QJsonObject json;
    json["command"] = QString::fromUtf8(IPMSG_FILE_REJECT);
    json["fileId"] = fileId;

    QJsonDocument doc(json);

    if (m_pendingFiles.contains(fileId)) {
        QTcpSocket* socket = m_sendingSockets.value(fileId);
        if (socket && socket->isOpen()) {
            socket->write(doc.toJson());
            socket->flush();
        }
    }

    m_pendingFiles.remove(fileId);
}

void IPMsgManager::resumeFile(const QString& fileId) {
    IPMsgTransferState state = getTransferState(fileId);
    if (state.fileId.isEmpty()) {
        emit fileError(fileId, "No transfer state found for resume");
        return;
    }

    // Send resume request to sender
    QJsonObject json;
    json["command"] = QString::fromUtf8(IPMSG_FILE_RESUME_REQUEST);
    json["fileId"] = fileId;
    json["offset"] = state.lastOffset;
    json["md5"] = state.md5;

    QJsonDocument doc(json);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [this, socket, data = doc.toJson()]() {
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::bytesWritten, this, [this, socket](qint64) {
        socket->deleteLater();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
        emit error("Failed to send resume request: " + socket->errorString());
        socket->deleteLater();
    });

    // Get sender IP from device list
    QString senderIp;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.value().id == state.fileId.left(8)) {
            senderIp = it.value().ip;
            break;
        }
    }

    if (!senderIp.isEmpty()) {
        socket->connectToHost(senderIp, m_port + 1);
    }
}

bool IPMsgManager::verifyFileIntegrity(const QString& filePath, const QString& expectedMd5) {
    if (expectedMd5.isEmpty()) return true;
    
    QString actualMd5 = calculateFileMd5(filePath);
    return actualMd5 == expectedMd5;
}

QList<IPMsgDevice> IPMsgManager::getOnlineDevices() const {
    QList<IPMsgDevice> devices;
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (now - it.value().lastSeen < 30000) { // 30 seconds timeout
            devices.append(it.value());
        }
    }

    return devices;
}

QList<IPMsgMessage> IPMsgManager::getMessages() const {
    return m_messages;
}

QList<IPMsgMessage> IPMsgManager::getMessagesWith(const QString& deviceId) const {
    QList<IPMsgMessage> result;
    for (const auto& msg : m_messages) {
        if (msg.senderId == deviceId) {
            result.append(msg);
        }
    }
    return result;
}

void IPMsgManager::setUserName(const QString& name) {
    m_userName = name;
}

QString IPMsgManager::userName() const {
    return m_userName;
}

QString IPMsgManager::calculateFileMd5(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return calculateFileMd5(&file);
}

QString IPMsgManager::calculateFileMd5(QIODevice* device) {
    if (!device || !device->isOpen()) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Md5);
    if (hash.addData(device)) {
        return hash.result().toHex();
    }
    return QString();
}

// ────────── Private Slots ──────────

void IPMsgManager::onUdpBroadcastReceived() {
    while (m_udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_udpSocket->pendingDatagramSize());
        QHostAddress sender;
        quint16 senderPort;

        m_udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        processUdpMessage(datagram, sender);
    }
}

void IPMsgManager::onNewTcpConnection() {
    while (m_tcpServer->hasPendingConnections()) {
        QTcpSocket* socket = m_tcpServer->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, &IPMsgManager::onTcpDataReceived);
        connect(socket, &QTcpSocket::disconnected, this, &IPMsgManager::onTcpDisconnected);
    }
}

void IPMsgManager::onTcpDataReceived() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QByteArray data = socket->readAll();
    processTcpCommand(data, socket);
}

void IPMsgManager::onTcpDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    // Check if this was a file transfer
    QString fileId;
    for (auto it = m_sendingSockets.constBegin(); it != m_sendingSockets.constEnd(); ++it) {
        if (it.value() == socket) {
            fileId = it.key();
            break;
        }
    }

    if (!fileId.isEmpty()) {
        m_sendingSockets.remove(fileId);
        m_sendingProgress.remove(fileId);
        emit fileCompleted(fileId, "", false);
    }

    socket->deleteLater();
}

void IPMsgManager::onSendProgress(qint64 bytesWritten) {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QString fileId;
    for (auto it = m_sendingSockets.constBegin(); it != m_sendingSockets.constEnd(); ++it) {
        if (it.value() == socket) {
            fileId = it.key();
            break;
        }
    }

    if (!fileId.isEmpty()) {
        m_sendingProgress[fileId] += bytesWritten;
        IPMsgTransferState state = getTransferState(fileId);
        state.transferredSize = m_sendingProgress[fileId];
        state.lastOffset = m_sendingProgress[fileId];
        saveTransferState(state);
        emit fileProgress(fileId, m_sendingProgress[fileId], state.totalSize);
    }
}

// ────────── Private Methods ──────────

void IPMsgManager::processUdpMessage(const QByteArray& data, const QHostAddress& sender) {
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return;

    QJsonObject json = doc.object();
    QString command = json["command"].toString();
    QString senderId = json["id"].toString();

    if (senderId == m_userId) return; // Ignore own messages

    if (command == QString::fromUtf8(IPMSG_HELLO)) {
        IPMsgDevice device;
        device.id = senderId;
        device.name = json["name"].toString();
        device.ip = sender.toString();
        device.port = json["port"].toInt();
        device.lastSeen = QDateTime::currentMSecsSinceEpoch();

        m_devices[senderId] = device;
        emit deviceFound(device);

        // Respond with our presence
        QJsonObject response;
        response["id"] = m_userId;
        response["name"] = m_userName;
        response["port"] = m_port;
        response["command"] = QString::fromUtf8(IPMSG_HELLO);

        QJsonDocument responseDoc(response);
        m_udpSocket->writeDatagram(responseDoc.toJson(), sender, m_port);
    } else if (command == QString::fromUtf8(IPMSG_LEAVE)) {
        m_devices.remove(senderId);
        emit deviceLeft(senderId);
    }
}

void IPMsgManager::processTcpCommand(const QByteArray& data, QTcpSocket* socket) {
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return;

    QJsonObject json = doc.object();
    QString command = json["command"].toString();

    if (command == QString::fromUtf8(IPMSG_MSG)) {
        IPMsgMessage msg;
        msg.senderId = json["id"].toString();
        msg.senderName = json["name"].toString();
        msg.senderIp = socket->peerAddress().toString();
        msg.content = json["message"].toString();
        msg.timestamp = json["timestamp"].toVariant().toLongLong();
        msg.isFile = false;

        m_messages.append(msg);
        emit messageReceived(msg);
    } else if (command == QString::fromUtf8(IPMSG_FILE_START)) {
        QString fileId = json["fileId"].toString();
        QString fileName = json["fileName"].toString();
        qint64 fileSize = json["fileSize"].toVariant().toLongLong();
        bool isDirectory = json["isDirectory"].toBool();
        QString senderName = json["senderName"].toString();
        QString md5 = json["md5"].toString();

        m_pendingFiles[fileId] = "";
        emit fileReceiveRequest(fileId, senderName, fileName, fileSize, isDirectory, md5);
    } else if (command == QString::fromUtf8(IPMSG_FILE_ACCEPT)) {
        QString fileId = json["fileId"].toString();
        QString savePath = json["savePath"].toString();
        m_pendingFiles[fileId] = savePath;
    } else if (command == QString::fromUtf8(IPMSG_FILE_REJECT)) {
        QString fileId = json["fileId"].toString();
        emit fileError(fileId, "File transfer rejected");
        m_pendingFiles.remove(fileId);
        removeTransferState(fileId);
    } else if (command == QString::fromUtf8(IPMSG_FILE_RESUME_REQUEST)) {
        QString fileId = json["fileId"].toString();
        qint64 offset = json["offset"].toVariant().toLongLong();
        QString md5 = json["md5"].toString();
        handleResumeRequest(fileId, offset, socket);
    } else if (command == QString::fromUtf8(IPMSG_FILE_RESUME)) {
        QString fileId = json["fileId"].toString();
        qint64 offset = json["offset"].toVariant().toLongLong();
        emit fileResuming(fileId, offset);
    } else if (command == QString::fromUtf8(IPMSG_FILE_DATA)) {
        QString fileId = json["fileId"].toString();
        QString filePath = json["filePath"].toString();
        qint64 fileSize = json["fileSize"].toVariant().toLongLong();
        bool isDirectory = json["isDirectory"].toBool();
        QString md5 = json["md5"].toString();
        qint64 offset = json["offset"].toVariant().toLongLong();

        if (m_pendingFiles.contains(fileId)) {
            QString savePath = m_pendingFiles[fileId];
            if (savePath.isEmpty()) {
                savePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
            }

            // Create transfer state
            IPMsgTransferState state;
            state.fileId = fileId;
            state.filePath = filePath;
            state.savePath = savePath;
            state.totalSize = fileSize;
            state.transferredSize = 0;
            state.md5 = md5;
            state.isDirectory = isDirectory;
            state.isSender = false;
            state.lastOffset = 0;
            saveTransferState(state);

            if (isDirectory) {
                QDir().mkpath(savePath + "/" + filePath);
            } else {
                QDir().mkpath(QFileInfo(savePath + "/" + filePath).absolutePath());
            }
        }
    } else if (command == QString::fromUtf8(IPMSG_FILE_VERIFY)) {
        QString fileId = json["fileId"].toString();
        QString md5 = json["md5"].toString();
        QString filePath = json["filePath"].toString();
        
        bool integrityOk = verifyFileIntegrity(filePath, md5);
        emit fileCompleted(fileId, filePath, integrityOk);
        removeTransferState(fileId);
    }
}

void IPMsgManager::sendUdpBroadcast(const QByteArray& data) {
    if (!m_udpSocket) return;

    // Broadcast to all network interfaces
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            !(iface.flags() & QNetworkInterface::IsRunning) ||
            (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        const auto entries = iface.addressEntries();
        for (const QNetworkAddressEntry& entry : entries) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                QHostAddress broadcast = entry.broadcast();
                if (!broadcast.isNull()) {
                    m_udpSocket->writeDatagram(data, broadcast, m_port);
                }
            }
        }
    }
}

void IPMsgManager::sendFileData(const QString& targetIp, const QList<IPMsgFileItem>& items) {
    for (const IPMsgFileItem& item : items) {
        QString fileId = generateFileId();

        // Create initial transfer state
        IPMsgTransferState state;
        state.fileId = fileId;
        state.filePath = item.absolutePath;
        state.savePath = "";
        state.totalSize = item.size;
        state.transferredSize = 0;
        state.md5 = item.md5;
        state.isDirectory = item.isDirectory;
        state.isSender = true;
        state.lastOffset = 0;
        saveTransferState(state);

        // Send file start command
        QJsonObject header;
        header["command"] = QString::fromUtf8(IPMSG_FILE_START);
        header["fileId"] = fileId;
        header["fileName"] = item.name;
        header["filePath"] = item.relativePath;
        header["fileSize"] = item.size;
        header["isDirectory"] = item.isDirectory;
        header["senderId"] = m_userId;
        header["senderName"] = m_userName;
        header["md5"] = item.md5;

        QJsonDocument doc(header);

        QTcpSocket* socket = new QTcpSocket(this);
        m_sendingSockets[fileId] = socket;
        m_sendingProgress[fileId] = 0;

        connect(socket, &QTcpSocket::connected, this, [this, socket, data = doc.toJson()]() {
            socket->write(data);
            socket->flush();
        });

        connect(socket, &QTcpSocket::bytesWritten, this, &IPMsgManager::onSendProgress);

        connect(socket, &QTcpSocket::readyRead, this, [this, socket, fileId, item, targetIp]() {
            QByteArray response = socket->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(response);
            QJsonObject json = doc.object();

            if (json["command"].toString() == QString::fromUtf8(IPMSG_FILE_ACCEPT)) {
                // Send file data
                QFile file(item.absolutePath);
                if (file.open(QIODevice::ReadOnly)) {
                    // Support resume from offset
                    qint64 offset = json["offset"].toVariant().toLongLong();
                    if (offset > 0) {
                        file.seek(offset);
                    }

                    QByteArray fileData = file.readAll();
                    QJsonObject dataHeader;
                    dataHeader["command"] = QString::fromUtf8(IPMSG_FILE_DATA);
                    dataHeader["fileId"] = fileId;
                    dataHeader["filePath"] = item.relativePath;
                    dataHeader["fileSize"] = item.size;
                    dataHeader["isDirectory"] = item.isDirectory;
                    dataHeader["md5"] = item.md5;
                    dataHeader["offset"] = offset;

                    QJsonDocument dataDoc(dataHeader);
                    socket->write(dataDoc.toJson());
                    socket->flush();

                    // Send file data in chunks
                    const qint64 chunkSize = 1024 * 1024; // 1MB chunks
                    qint64 totalSent = offset;
                    
                    while (!file.atEnd()) {
                        QByteArray chunk = file.read(chunkSize);
                        socket->write(chunk);
                        socket->flush();
                        totalSent += chunk.size();
                        
                        // Update progress
                        m_sendingProgress[fileId] = totalSent;
                        emit fileProgress(fileId, totalSent, item.size);
                    }

                    file.close();

                    // Send verify command
                    QJsonObject verifyHeader;
                    verifyHeader["command"] = QString::fromUtf8(IPMSG_FILE_VERIFY);
                    verifyHeader["fileId"] = fileId;
                    verifyHeader["md5"] = item.md5;
                    verifyHeader["filePath"] = item.absolutePath;

                    QJsonDocument verifyDoc(verifyHeader);
                    socket->write(verifyDoc.toJson());
                    socket->flush();

                    emit fileCompleted(fileId, item.absolutePath, true);
                } else {
                    emit fileError(fileId, "Failed to open file: " + item.absolutePath);
                }
            } else if (json["command"].toString() == QString::fromUtf8(IPMSG_FILE_REJECT)) {
                emit fileError(fileId, "File transfer rejected");
            } else if (json["command"].toString() == QString::fromUtf8(IPMSG_FILE_RESUME_REQUEST)) {
                // Handle resume request from receiver
                qint64 offset = json["offset"].toVariant().toLongLong();
                emit fileResuming(fileId, offset);
                
                // Update transfer state
                IPMsgTransferState state = getTransferState(fileId);
                state.lastOffset = offset;
                state.transferredSize = offset;
                saveTransferState(state);
            }

            socket->deleteLater();
        });

        connect(socket, &QTcpSocket::errorOccurred, this, [this, socket, fileId](QAbstractSocket::SocketError) {
            emit fileError(fileId, "Connection error: " + socket->errorString());
            socket->deleteLater();
        });

        socket->connectToHost(targetIp, m_port + 1);
    }
}

void IPMsgManager::receiveFileData(QTcpSocket* socket, const QJsonObject& header) {
    QString fileId = header["fileId"].toString();
    QString fileName = header["fileName"].toString();
    QString filePath = header["filePath"].toString();
    qint64 fileSize = header["fileSize"].toVariant().toLongLong();
    bool isDirectory = header["isDirectory"].toBool();
    QString md5 = header["md5"].toString();
    QString savePath = m_pendingFiles.value(fileId, 
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));

    if (isDirectory) {
        QDir().mkpath(savePath + "/" + filePath);
    } else {
        // Receive file data
        QJsonObject dataHeader;
        dataHeader["command"] = QString::fromUtf8(IPMSG_FILE_ACCEPT);
        dataHeader["fileId"] = fileId;
        dataHeader["savePath"] = savePath;
        dataHeader["offset"] = 0;

        QJsonDocument doc(dataHeader);
        socket->write(doc.toJson());
        socket->flush();
    }
}

IPMsgFileItem IPMsgManager::buildFileTree(const QString& path, const QString& basePath) {
    QFileInfo info(path);
    IPMsgFileItem item;
    item.name = info.fileName();
    item.absolutePath = info.absoluteFilePath();
    item.relativePath = QDir(basePath).relativeFilePath(path);
    item.size = info.size();
    item.isDirectory = info.isDir();
    item.md5 = calculateFileMd5(path);

    if (info.isDir()) {
        QDir dir(path);
        QDirIterator it(path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            IPMsgFileItem child = buildFileTree(it.filePath(), basePath);
            item.children.append(child);
        }
    }

    return item;
}

qint64 IPMsgManager::calculateFolderSize(const QString& folderPath) {
    qint64 totalSize = 0;
    QDirIterator it(folderPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        totalSize += it.fileInfo().size();
    }
    return totalSize;
}

void IPMsgManager::saveReceivedFile(QTcpSocket* socket, const QString& savePath, qint64 fileSize) {
    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit fileError("", "Failed to create file: " + savePath);
        return;
    }

    qint64 bytesReceived = 0;
    while (bytesReceived < fileSize) {
        QByteArray data = socket->read(qMin(fileSize - bytesReceived, (qint64)1024 * 1024));
        if (data.isEmpty()) {
            socket->waitForReadyRead(10000);
            continue;
        }
        file.write(data);
        bytesReceived += data.size();
        emit fileProgress("", bytesReceived, fileSize);
    }

    file.close();
}

void IPMsgManager::saveReceivedFolder(QTcpSocket* socket, const QString& savePath, qint64 totalSize) {
    QDir().mkpath(savePath);
}

QString IPMsgManager::generateFileId() {
    return QUuid::createUuid().toString().remove('{').remove('}').remove('-');
}

QString IPMsgManager::getLocalIp() {
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            !(iface.flags() & QNetworkInterface::IsRunning) ||
            (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        const auto entries = iface.addressEntries();
        for (const QNetworkAddressEntry& entry : entries) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                return entry.ip().toString();
            }
        }
    }

    return "127.0.0.1";
}

void IPMsgManager::loadTransferStates() {
    QFile file(m_transferStateFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return;

    QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        QJsonObject stateObj = it.value().toObject();
        IPMsgTransferState state;
        state.fileId = stateObj["fileId"].toString();
        state.filePath = stateObj["filePath"].toString();
        state.savePath = stateObj["savePath"].toString();
        state.totalSize = stateObj["totalSize"].toVariant().toLongLong();
        state.transferredSize = stateObj["transferredSize"].toVariant().toLongLong();
        state.md5 = stateObj["md5"].toString();
        state.isDirectory = stateObj["isDirectory"].toBool();
        state.isSender = stateObj["isSender"].toBool();
        state.lastOffset = stateObj["lastOffset"].toVariant().toLongLong();
        m_transferStates[it.key()] = state;
    }
}

void IPMsgManager::saveTransferState(const IPMsgTransferState& state) {
    m_transferStates[state.fileId] = state;

    QFile file(m_transferStateFile);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    QJsonObject root;
    for (auto it = m_transferStates.constBegin(); it != m_transferStates.constEnd(); ++it) {
        QJsonObject stateObj;
        stateObj["fileId"] = it.value().fileId;
        stateObj["filePath"] = it.value().filePath;
        stateObj["savePath"] = it.value().savePath;
        stateObj["totalSize"] = it.value().totalSize;
        stateObj["transferredSize"] = it.value().transferredSize;
        stateObj["md5"] = it.value().md5;
        stateObj["isDirectory"] = it.value().isDirectory;
        stateObj["isSender"] = it.value().isSender;
        stateObj["lastOffset"] = it.value().lastOffset;
        root[it.key()] = stateObj;
    }

    QJsonDocument doc(root);
    file.write(doc.toJson());
}

void IPMsgManager::removeTransferState(const QString& fileId) {
    m_transferStates.remove(fileId);
    
    QFile file(m_transferStateFile);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    QJsonObject root;
    for (auto it = m_transferStates.constBegin(); it != m_transferStates.constEnd(); ++it) {
        QJsonObject stateObj;
        stateObj["fileId"] = it.value().fileId;
        stateObj["filePath"] = it.value().filePath;
        stateObj["savePath"] = it.value().savePath;
        stateObj["totalSize"] = it.value().totalSize;
        stateObj["transferredSize"] = it.value().transferredSize;
        stateObj["md5"] = it.value().md5;
        stateObj["isDirectory"] = it.value().isDirectory;
        stateObj["isSender"] = it.value().isSender;
        stateObj["lastOffset"] = it.value().lastOffset;
        root[it.key()] = stateObj;
    }

    QJsonDocument doc(root);
    file.write(doc.toJson());
}

IPMsgTransferState IPMsgManager::getTransferState(const QString& fileId) const {
    return m_transferStates.value(fileId);
}

bool IPMsgManager::hasTransferState(const QString& fileId) const {
    return m_transferStates.contains(fileId);
}

void IPMsgManager::sendResumeRequest(const QString& targetIp, const QString& fileId, qint64 offset) {
    QJsonObject json;
    json["command"] = QString::fromUtf8(IPMSG_FILE_RESUME_REQUEST);
    json["fileId"] = fileId;
    json["offset"] = offset;

    QJsonDocument doc(json);

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [this, socket, data = doc.toJson()]() {
        socket->write(data);
        socket->flush();
    });
    connect(socket, &QTcpSocket::bytesWritten, this, [this, socket](qint64) {
        socket->deleteLater();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
        emit error("Failed to send resume request: " + socket->errorString());
        socket->deleteLater();
    });

    socket->connectToHost(targetIp, m_port + 1);
}

void IPMsgManager::handleResumeRequest(const QString& fileId, qint64 offset, QTcpSocket* socket) {
    IPMsgTransferState state = getTransferState(fileId);
    if (state.fileId.isEmpty()) {
        emit fileError(fileId, "No transfer state found for resume");
        return;
    }

    // Update state and send resume response
    state.lastOffset = offset;
    state.transferredSize = offset;
    saveTransferState(state);

    QJsonObject json;
    json["command"] = QString::fromUtf8(IPMSG_FILE_RESUME);
    json["fileId"] = fileId;
    json["offset"] = offset;

    QJsonDocument doc(json);
    socket->write(doc.toJson());
    socket->flush();
}

} // namespace xrk
