#include "audit_logger.h"
#include "logger.h"
#include <QDir>
#include <QJsonArray>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDateTime>

namespace xrk {

AuditLogger::AuditLogger(const QString& logDir, QObject* parent)
    : QObject(parent), m_logDir(logDir) {
    if (m_logDir.isEmpty()) {
        m_logDir = resolveLogDir();
    }

    QDir().mkpath(m_logDir);

    m_currentLogDate = QDateTime::currentDateTime();
    QString logPath = m_logDir + "/audit_" + m_currentLogDate.toString("yyyyMMdd") + ".log";
    m_logFile = new QFile(logPath, this);
    [[maybe_unused]] bool ok1 = m_logFile->open(QIODevice::Append | QIODevice::Text);

    m_rotationTimer = new QTimer(this);
    connect(m_rotationTimer, &QTimer::timeout, this, &AuditLogger::onRotationTimer);
    m_rotationTimer->start(ROTATION_INTERVAL_MS);

    LOG_INFO("AuditLogger initialized, log file: " + logPath);
}

AuditLogger::~AuditLogger() {
    if (m_logFile && m_logFile->isOpen()) {
        m_logFile->close();
    }
}

void AuditLogger::setLogDirectory(const QString& dir) {
    QMutexLocker locker(&m_mutex);
    if (m_logFile && m_logFile->isOpen()) {
        m_logFile->close();
    }
    m_logDir = dir;
    QDir().mkpath(m_logDir);

    m_currentLogDate = QDateTime::currentDateTime();
    QString logPath = m_logDir + "/audit_" + m_currentLogDate.toString("yyyyMMdd") + ".log";
    if (m_logFile) {
        delete m_logFile;
    }
    m_logFile = new QFile(logPath, this);
    [[maybe_unused]] bool ok2 = m_logFile->open(QIODevice::Append | QIODevice::Text);
}

QString AuditLogger::currentLogPath() const {
    QMutexLocker locker(&m_mutex);
    return m_logFile ? m_logFile->fileName() : QString();
}

QJsonArray AuditLogger::recentEntries(int count) const {
    return entriesSince(QDateTime(), count);
}

QJsonArray AuditLogger::entriesSince(const QDateTime& since, int limit) const {
    QJsonArray sorted;

    QDir dir(m_logDir);
    // Names are audit_YYYY-MM-DD.log, so sorting by name sorts chronologically.
    QStringList logFiles = dir.entryList(QStringList() << "audit_*.log", QDir::Files, QDir::Name);
    if (logFiles.isEmpty()) return sorted;

    // Walk the newest file first and emit each file's lines newest-first, so the
    // result is globally newest-first and `limit` truncates the *oldest* tail.
    // (Reversing a list that was built newest-file-first would have returned the
    // oldest entries instead — the admin console relies on getting the newest.)
    for (int fi = logFiles.size() - 1; fi >= 0 && sorted.size() < limit; --fi) {
        QFile file(dir.absoluteFilePath(logFiles[fi]));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

        QJsonArray fileEntries;
        while (!file.atEnd()) {
            QByteArray line = file.readLine().trimmed();
            if (line.isEmpty()) continue;

            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(line, &error);
            if (error.error != QJsonParseError::NoError) continue;
            if (!doc.isObject()) continue;

            QJsonObject entry = doc.object();
            if (since.isValid()) {
                QString ts = entry.value("timestamp").toString();
                QDateTime entryTime = QDateTime::fromString(ts, Qt::ISODate);
                if (entryTime.isValid() && entryTime < since) continue;
            }

            fileEntries.append(entry);
        }
        file.close();

        for (int i = fileEntries.size() - 1; i >= 0 && sorted.size() < limit; --i) {
            sorted.append(fileEntries[i]);
        }
    }

    return sorted;
}

void AuditLogger::clearOldLogs(int daysToKeep) {
    QMutexLocker locker(&m_mutex);
    QDir dir(m_logDir);
    QStringList logFiles = dir.entryList(QStringList() << "audit_*.log", QDir::Files, QDir::Name);
    QDateTime cutoff = QDateTime::currentDateTime().addDays(-daysToKeep);

    for (const QString& fileName : logFiles) {
        QString dateStr = fileName.mid(6, 8);
        QDate fileDate = QDate::fromString(dateStr, "yyyyMMdd");
        if (fileDate.isValid() && fileDate.startOfDay() < cutoff) {
            dir.remove(fileName);
        }
    }
}

void AuditLogger::logConnection(const QString& clientId, const QString& event, const QString& details) {
    QJsonObject entry;
    entry["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    entry["type"] = "connection";
    entry["clientId"] = clientId;
    entry["event"] = event;
    if (!details.isEmpty()) entry["details"] = details;
    writeEntry(entry);
}

void AuditLogger::logAuth(const QString& clientId, bool success, const QString& details) {
    QJsonObject entry;
    entry["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    entry["type"] = "auth";
    entry["clientId"] = clientId;
    entry["success"] = success;
    if (!details.isEmpty()) entry["details"] = details;
    writeEntry(entry);
}

void AuditLogger::logSession(const QString& sessionId, const QString& deviceId, const QString& event, const QString& details) {
    QJsonObject entry;
    entry["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    entry["type"] = "session";
    entry["sessionId"] = sessionId;
    entry["deviceId"] = deviceId;
    entry["event"] = event;
    if (!details.isEmpty()) entry["details"] = details;
    writeEntry(entry);
}

void AuditLogger::logOperation(const QString& clientId, const QString& operation, const QString& details) {
    QJsonObject entry;
    entry["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    entry["type"] = "operation";
    entry["clientId"] = clientId;
    entry["operation"] = operation;
    if (!details.isEmpty()) entry["details"] = details;
    writeEntry(entry);
}

void AuditLogger::logError(const QString& clientId, const QString& error, const QString& details) {
    QJsonObject entry;
    entry["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    entry["type"] = "error";
    entry["clientId"] = clientId;
    entry["error"] = error;
    if (!details.isEmpty()) entry["details"] = details;
    writeEntry(entry);
}

void AuditLogger::writeEntry(const QJsonObject& entry) {
    QMutexLocker locker(&m_mutex);
    if (!m_logFile || !m_logFile->isOpen()) return;

    QJsonDocument doc(entry);
    QByteArray line = doc.toJson(QJsonDocument::Compact) + "\n";
    m_logFile->write(line);
    m_logFile->flush();

    locker.unlock();
    emit entryAdded(entry);

    rotateIfNeeded();
}

void AuditLogger::rotateIfNeeded() {
    QMutexLocker locker(&m_mutex);
    if (!m_logFile || !m_logFile->isOpen()) return;

    QDate today = QDateTime::currentDateTime().date();
    if (today != m_currentLogDate.date()) {
        m_logFile->close();
        m_currentLogDate = QDateTime::currentDateTime();
        QString logPath = m_logDir + "/audit_" + m_currentLogDate.toString("yyyyMMdd") + ".log";
        m_logFile->setFileName(logPath);
        [[maybe_unused]] bool ok3 = m_logFile->open(QIODevice::Append | QIODevice::Text);
    }

    if (m_logFile->size() > MAX_LOG_SIZE) {
        m_logFile->close();
        QString backup = m_logFile->fileName() + "." +
            QDateTime::currentDateTime().toString("HHmmss");
        QFile::rename(m_logFile->fileName(), backup);
        m_logFile->setFileName(m_logDir + "/audit_" + m_currentLogDate.toString("yyyyMMdd") + ".log");
        [[maybe_unused]] bool ok4 = m_logFile->open(QIODevice::Append | QIODevice::Text);
    }
}

void AuditLogger::onRotationTimer() {
    rotateIfNeeded();
}

QString AuditLogger::resolveLogDir() const {
#ifdef Q_OS_WIN
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#endif
    if (base.isEmpty()) {
        base = QDir::homePath() + "/.xrk";
    }
    return base + "/audit_logs";
}

} // namespace xrk
