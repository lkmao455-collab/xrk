#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>
#include <QVersionNumber>

namespace xrk {

struct UpdateInfo {
    bool available = false;
    QString version;
    QString downloadUrl;
    QString changelog;
    qint64 fileSize = 0;
};

class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QObject* parent = nullptr);
    ~Updater();

    void checkForUpdates(bool silent = true);
    void setUpdateUrl(const QString& url);
    QString currentVersion() const;
    bool isUpdateAvailable() const;
    UpdateInfo lastUpdateInfo() const;

    void setAutoCheckEnabled(bool enabled);
    bool isAutoCheckEnabled() const;

signals:
    void updateAvailable(const UpdateInfo& info);
    void updateChecked(bool available);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadCompleted(const QString& filePath);
    void downloadFailed(const QString& error);

private slots:
    void onCheckFinished(QNetworkReply* reply);
    void onDownloadFinished(QNetworkReply* reply);
    void onDownloadProgress(qint64 received, qint64 total);

private:
    QNetworkAccessManager* m_networkManager = nullptr;
    QString m_updateUrl;
    QString m_currentVersion;
    bool m_autoCheck = true;
    bool m_silent = true;
    UpdateInfo m_lastInfo;
};

} // namespace xrk
