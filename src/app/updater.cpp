#include "updater.h"
#include "core/logger.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrl>
#include <QStandardPaths>
#include <QFile>
#include <QSettings>

namespace xrk {

Updater::Updater(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_currentVersion("1.3.0")
    , m_updateUrl("https://api.github.com/repos/lkmao455-collab/xrk/releases/latest")
{
    QSettings s("XRK", "Updater");
    m_autoCheck = s.value("auto_check", true).toBool();
    m_currentVersion = s.value("current_version", m_currentVersion).toString();
}

Updater::~Updater() {}

void Updater::checkForUpdates(bool silent) {
    m_silent = silent;
    QUrl url(m_updateUrl);
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github.v3+json");
    request.setRawHeader("User-Agent", "XRK-Updater");
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onCheckFinished(reply);
    });
}

void Updater::setUpdateUrl(const QString& url) {
    m_updateUrl = url;
}

QString Updater::currentVersion() const {
    return m_currentVersion;
}

bool Updater::isUpdateAvailable() const {
    return m_lastInfo.available;
}

UpdateInfo Updater::lastUpdateInfo() const {
    return m_lastInfo;
}

void Updater::setAutoCheckEnabled(bool enabled) {
    m_autoCheck = enabled;
    QSettings s("XRK", "Updater");
    s.setValue("auto_check", enabled);
}

bool Updater::isAutoCheckEnabled() const {
    return m_autoCheck;
}

void Updater::onCheckFinished(QNetworkReply* reply) {
    if (reply->error() != QNetworkReply::NoError) {
        LOG_ERROR(QString("Update check failed: %1").arg(reply->errorString()));
        emit updateChecked(false);
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();

    UpdateInfo info;
    QString tagName = obj["tag_name"].toString();
    if (tagName.startsWith('v') || tagName.startsWith('V')) {
        tagName = tagName.mid(1);
    }
    info.version = tagName;
    info.changelog = obj["body"].toString();

    // Find asset URL for Windows
    QJsonArray assets = obj["assets"].toArray();
    for (const auto& asset : assets) {
        QJsonObject assetObj = asset.toObject();
        QString name = assetObj["name"].toString();
        if (name.endsWith(".exe") || name.endsWith(".zip")) {
            info.downloadUrl = assetObj["browser_download_url"].toString();
            info.fileSize = assetObj["size"].toVariant().toLongLong();
            break;
        }
    }

    QVersionNumber current = QVersionNumber::fromString(m_currentVersion);
    QVersionNumber latest = QVersionNumber::fromString(info.version);
    info.available = (latest > current);
    m_lastInfo = info;

    LOG_INFO(QString("Update check: current=%1 latest=%2 available=%3")
             .arg(m_currentVersion, info.version).arg(info.available));

    if (info.available && !m_silent) {
        emit updateAvailable(info);
    }
    emit updateChecked(info.available);
    reply->deleteLater();
}

void Updater::onDownloadFinished(QNetworkReply* reply) {
    if (reply->error() != QNetworkReply::NoError) {
        emit downloadFailed(reply->errorString());
        reply->deleteLater();
        return;
    }

    QString savePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                       + "/XRK_Update_" + m_lastInfo.version + ".exe";
    QFile file(savePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(reply->readAll());
        file.close();
        emit downloadCompleted(savePath);
    } else {
        emit downloadFailed(tr("无法保存更新文件"));
    }
    reply->deleteLater();
}

void Updater::onDownloadProgress(qint64 received, qint64 total) {
    emit downloadProgress(received, total);
}

} // namespace xrk
