#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QMap>
#include <QSet>
#include <QCryptographicHash>

namespace xrk {

struct IPMsgDevice {
    QString id;
    QString name;
    QString ip;
    quint16 port;
    qint64 lastSeen;
};

struct IPMsgFileItem {
    QString name;
    QString absolutePath;
    QString relativePath;
    qint64 size;
    bool isDirectory;
    QString md5;
    QList<IPMsgFileItem> children;
};

struct IPMsgMessage {
    QString senderId;
    QString senderName;
    QString senderIp;
    QString content;
    qint64 timestamp;
    bool isFile;
    QString filePath;
    qint64 fileSize;
    bool isDirectory;
};

struct IPMsgTransferState {
    QString fileId;
    QString filePath;
    QString savePath;
    qint64 totalSize;
    qint64 transferredSize;
    QString md5;
    bool isDirectory;
    bool isSender;
    qint64 lastOffset;
};

class IPMsgManager : public QObject {
    Q_OBJECT
public:
    explicit IPMsgManager(QObject* parent = nullptr);
    ~IPMsgManager();

    bool start(quint16 port = 2425);
    void stop();
    bool isRunning() const;

    void broadcastPresence();
    void sendMessage(const QString& targetIp, const QString& message);
    void sendFile(const QString& targetIp, const QString& filePath);
    void sendFolder(const QString& targetIp, const QString& folderPath);
    void acceptFile(const QString& fileId, const QString& savePath);
    void rejectFile(const QString& fileId);
    void resumeFile(const QString& fileId);
    bool verifyFileIntegrity(const QString& filePath, const QString& expectedMd5);

    QList<IPMsgDevice> getOnlineDevices() const;
    QList<IPMsgMessage> getMessages() const;
    QList<IPMsgMessage> getMessagesWith(const QString& deviceId) const;

    void setUserName(const QString& name);
    QString userName() const;

    static QString calculateFileMd5(const QString& filePath);
    static QString calculateFileMd5(QIODevice* device);

signals:
    void deviceFound(const IPMsgDevice& device);
    void deviceLeft(const QString& deviceId);
    void messageReceived(const IPMsgMessage& message);
    void fileReceiveRequest(const QString& fileId, const QString& senderName, 
                           const QString& fileName, qint64 fileSize, bool isDirectory,
                           const QString& md5);
    void fileProgress(const QString& fileId, qint64 bytesTransferred, qint64 totalBytes);
    void fileCompleted(const QString& fileId, const QString& filePath, bool integrityOk);
    void fileError(const QString& fileId, const QString& error);
    void fileResuming(const QString& fileId, qint64 resumeOffset);
    void error(const QString& message);

private slots:
    void onUdpBroadcastReceived();
    void onNewTcpConnection();
    void onTcpDataReceived();
    void onTcpDisconnected();
    void onSendProgress(qint64 bytesWritten);

private:
    void processUdpMessage(const QByteArray& data, const QHostAddress& sender);
    void processTcpCommand(const QByteArray& data, QTcpSocket* socket);
    void sendUdpBroadcast(const QByteArray& data);
    void sendFileData(const QString& targetIp, const QList<IPMsgFileItem>& items);
    void receiveFileData(QTcpSocket* socket, const QJsonObject& header);
    IPMsgFileItem buildFileTree(const QString& path, const QString& basePath);
    qint64 calculateFolderSize(const QString& folderPath);
    void saveReceivedFile(QTcpSocket* socket, const QString& savePath, qint64 fileSize);
    void saveReceivedFolder(QTcpSocket* socket, const QString& savePath, qint64 totalSize);
    QString generateFileId();
    QString getLocalIp();
    void loadTransferStates();
    void saveTransferState(const IPMsgTransferState& state);
    void removeTransferState(const QString& fileId);
    IPMsgTransferState getTransferState(const QString& fileId) const;
    bool hasTransferState(const QString& fileId) const;
    void sendResumeRequest(const QString& targetIp, const QString& fileId, qint64 offset);
    void handleResumeRequest(const QString& fileId, qint64 offset, QTcpSocket* socket);

    QUdpSocket* m_udpSocket = nullptr;
    QTcpServer* m_tcpServer = nullptr;
    QMap<QString, IPMsgDevice> m_devices;
    QList<IPMsgMessage> m_messages;
    QMap<QString, QTcpSocket*> m_sendingSockets;
    QMap<QString, qint64> m_sendingProgress;
    QMap<QString, QString> m_pendingFiles;
    QMap<QString, IPMsgTransferState> m_transferStates;
    QString m_userName;
    QString m_userId;
    quint16 m_port;
    bool m_running = false;
    QString m_transferStateFile;
};

} // namespace xrk
