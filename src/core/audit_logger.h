#pragma once

#include <QObject>
#include <QFile>
#include <QMutex>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTimer>
#include <QDateTime>
#include <QString>

namespace xrk {

class AuditLogger : public QObject {
    Q_OBJECT
public:
    explicit AuditLogger(const QString& logDir = QString(), QObject* parent = nullptr);
    ~AuditLogger();

    void setLogDirectory(const QString& dir);
    QString currentLogPath() const;
    QJsonArray recentEntries(int count = 100) const;
    QJsonArray entriesSince(const QDateTime& since, int limit = 500) const;
    void clearOldLogs(int daysToKeep = 30);

    void logConnection(const QString& clientId, const QString& event, const QString& details = QString());
    void logAuth(const QString& clientId, bool success, const QString& details = QString());
    void logSession(const QString& sessionId, const QString& deviceId, const QString& event, const QString& details = QString());
    void logOperation(const QString& clientId, const QString& operation, const QString& details = QString());
    void logError(const QString& clientId, const QString& error, const QString& details = QString());

signals:
    void entryAdded(const QJsonObject& entry);

private:
    void writeEntry(const QJsonObject& entry);
    void rotateIfNeeded();
    void onRotationTimer();
    QString resolveLogDir() const;

    QString m_logDir;
    QFile* m_logFile = nullptr;
    mutable QMutex m_mutex;
    QTimer* m_rotationTimer = nullptr;
    QDateTime m_currentLogDate;

    static constexpr qint64 MAX_LOG_SIZE = 10 * 1024 * 1024; // 10MB
    static constexpr int ROTATION_INTERVAL_MS = 3600000;     // 1 hour
};

} // namespace xrk
