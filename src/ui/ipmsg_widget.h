#pragma once

#include <QWidget>
#include <QListWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QProgressBar>
#include <QTimer>

namespace xrk {

class IPMsgManager;

class IPMsgWidget : public QWidget {
    Q_OBJECT
public:
    explicit IPMsgWidget(IPMsgManager* manager, QWidget* parent = nullptr);
    ~IPMsgWidget();

    void setTargetDevice(const QString& deviceId, const QString& deviceName);

signals:
    void sendMessage(const QString& targetIp, const QString& message);
    void sendFile(const QString& targetIp, const QString& filePath);
    void sendFolder(const QString& targetIp, const QString& folderPath);

private slots:
    void onSendClicked();
    void onSendFileClicked();
    void onSendFolderClicked();
    void onDeviceDoubleClicked(QTreeWidgetItem* item, int column);
    void onMessageReceived(const QString& sender, const QString& message);
    void onFileProgress(const QString& fileName, qint64 bytesTransferred, qint64 totalBytes);
    void onFileCompleted(const QString& fileName, bool integrityOk);
    void onFileResuming(const QString& fileName, qint64 offset);
    void onRefreshDevices();
    void onDeviceFound(const QString& name, const QString& ip);
    void onDeviceLeft(const QString& name);
    void onResumeTransfer(const QString& fileId);

private:
    void setupUI();
    void addMessage(const QString& sender, const QString& message, bool isSelf = false);
    void updateDeviceList();
    void updateTransferStatus(const QString& fileName, const QString& status, const QString& color);

    IPMsgManager* m_manager = nullptr;
    QString m_targetIp;
    QString m_targetName;

    // UI Elements
    QSplitter* m_splitter = nullptr;
    QTreeWidget* m_deviceTree = nullptr;
    QTextEdit* m_messageDisplay = nullptr;
    QLineEdit* m_messageInput = nullptr;
    QPushButton* m_sendBtn = nullptr;
    QPushButton* m_sendFileBtn = nullptr;
    QPushButton* m_sendFolderBtn = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_targetLabel = nullptr;
    QLabel* m_integrityLabel = nullptr;
    QPushButton* m_resumeBtn = nullptr;
};

} // namespace xrk
