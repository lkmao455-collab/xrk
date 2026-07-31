#include "logger.h"
#include <QDateTime>
#include <QMutexLocker>
#include <QDir>

namespace xrk {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::Logger(QObject* parent) : QObject(parent) {
}

Logger::~Logger() {
    if (m_logFile.isOpen()) {
        m_logFile.close();
    }
}

void Logger::setLogLevel(LogLevel level) {
    m_logLevel = level;
}

void Logger::setLogFile(const QString& path) {
    QMutexLocker locker(&m_mutex);
    if (m_logFile.isOpen()) {
        m_logFile.close();
    }
    m_logFile.setFileName(path);
    QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    if (!m_logFile.open(QIODevice::Append | QIODevice::Text)) {
        LOG_ERROR("Logger: failed to open log file: " + path);
    }
}

void Logger::clearLogFile() {
    QMutexLocker locker(&m_mutex);
    if (m_logFile.isOpen()) {
        m_logFile.resize(0);
    }
}

void Logger::debug(const QString& message) {
    log(LogLevel::Debug, message);
}

void Logger::info(const QString& message) {
    log(LogLevel::Info, message);
}

void Logger::warning(const QString& message) {
    log(LogLevel::Warning, message);
}

void Logger::error(const QString& message) {
    log(LogLevel::Error, message);
}

void Logger::fatal(const QString& message) {
    log(LogLevel::Fatal, message);
}

void Logger::log(LogLevel level, const QString& message, const QString& file, int line) {
    if (level < m_logLevel) {
        return;
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString levelStr;
    switch (level) {
        case LogLevel::Debug:   levelStr = "DEBUG"; break;
        case LogLevel::Info:    levelStr = "INFO"; break;
        case LogLevel::Warning: levelStr = "WARN"; break;
        case LogLevel::Error:   levelStr = "ERROR"; break;
        case LogLevel::Fatal:   levelStr = "FATAL"; break;
    }

    QString formattedMessage;
    if (!file.isEmpty()) {
        formattedMessage = QString("[%1] [%2] %3:%4 - %5")
            .arg(timestamp, levelStr, file, QString::number(line), message);
    } else {
        formattedMessage = QString("[%1] [%2] %3").arg(timestamp, levelStr, message);
    }

    writeToFile(formattedMessage);
    emit logMessage(level, formattedMessage);
}

void Logger::writeToFile(const QString& message) {
    QMutexLocker locker(&m_mutex);
    if (m_logFile.isOpen()) {
        m_logFile.write(message.toUtf8() + "\n");
        m_logFile.flush();
    }
}

} // namespace xrk
