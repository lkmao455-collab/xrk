#pragma once

#include <QObject>
#include <QString>
#include <QFile>
#include <QMutex>

namespace xrk {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

class Logger : public QObject {
    Q_OBJECT
public:
    static Logger& instance();
    
    void setLogLevel(LogLevel level);
    LogLevel logLevel() const { return m_logLevel; }
    void setLogFile(const QString& path);
    QString logFile() const { return m_logFile.fileName(); }
    void clearLogFile();

    void debug(const QString& message);
    void info(const QString& message);
    void warning(const QString& message);
    void error(const QString& message);
    void fatal(const QString& message);
    
    void log(LogLevel level, const QString& message, const QString& file = QString(), int line = 0);

signals:
    void logMessage(LogLevel level, const QString& message);

private:
    Logger(QObject* parent = nullptr);
    ~Logger();
    
    void writeToFile(const QString& message);

    LogLevel m_logLevel = LogLevel::Info;
    QFile m_logFile;
    QMutex m_mutex;
};

#define LOG_DEBUG(msg) xrk::Logger::instance().debug(msg)
#define LOG_INFO(msg) xrk::Logger::instance().info(msg)
#define LOG_WARNING(msg) xrk::Logger::instance().warning(msg)
#define LOG_ERROR(msg) xrk::Logger::instance().error(msg)
#define LOG_FATAL(msg) xrk::Logger::instance().fatal(msg)

} // namespace xrk
