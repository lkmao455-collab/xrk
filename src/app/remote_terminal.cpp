#include "remote_terminal.h"
#include "core/logger.h"

namespace xrk {

RemoteTerminal::RemoteTerminal(QObject* parent) : QObject(parent) {
}

RemoteTerminal::~RemoteTerminal() {
    stopTerminal();
}

bool RemoteTerminal::startTerminal(const QString& shellType, uint32_t cols, uint32_t rows) {
    if (m_process) {
        stopTerminal();
    }

    m_shellType = shellType;
    m_process = new QProcess(this);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &RemoteTerminal::onReadyRead);
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        QByteArray err = m_process->readAllStandardError();
        emit outputReady(err);
    });
    connect(m_process, &QProcess::errorOccurred, this, &RemoteTerminal::onProcessError);
    connect(m_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &RemoteTerminal::onProcessFinished);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TERM", "xterm");
    env.insert("COLUMNS", QString::number(cols));
    env.insert("LINES", QString::number(rows));
    m_process->setProcessEnvironment(env);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

#ifdef _WIN32
    if (shellType == "powershell") {
        m_process->start("powershell.exe", QStringList());
    } else {
        m_process->start("cmd.exe", QStringList());
    }
#else
    QString shell = (shellType == "bash") ? "bash" : (shellType == "zsh" ? "zsh" : "/bin/sh");
    m_process->start(shell, QStringList());
#endif

    if (!m_process->waitForStarted(3000)) {
        LOG_ERROR("Terminal: Failed to start " + shellType);
        delete m_process;
        m_process = nullptr;
        return false;
    }

    LOG_INFO("Terminal started: " + shellType);
    return true;
}

void RemoteTerminal::stopTerminal() {
    if (!m_process) return;

    if (m_process->state() != QProcess::NotRunning) {
        m_process->write("exit\n");
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }

    delete m_process;
    m_process = nullptr;
    LOG_INFO("Terminal stopped");
}

void RemoteTerminal::writeInput(const QByteArray& data) {
    if (m_process && m_process->state() == QProcess::Running) {
        m_process->write(data);
        if (!data.endsWith('\n')) {
            m_process->write("\n");
        }
    }
}

void RemoteTerminal::resize(uint32_t cols, uint32_t rows) {
    if (m_process && m_process->state() == QProcess::Running) {
        QProcessEnvironment env = m_process->processEnvironment();
        env.insert("COLUMNS", QString::number(cols));
        env.insert("LINES", QString::number(rows));
        m_process->setProcessEnvironment(env);
    }
}

bool RemoteTerminal::isRunning() const {
    return m_process && m_process->state() == QProcess::Running;
}

void RemoteTerminal::onReadyRead() {
    QByteArray output = m_process->readAllStandardOutput();
    if (!output.isEmpty()) {
        emit outputReady(output);
    }
}

void RemoteTerminal::onProcessError(QProcess::ProcessError error) {
    QString errorStr;
    switch (error) {
        case QProcess::FailedToStart: errorStr = "Failed to start"; break;
        case QProcess::Crashed: errorStr = "Process crashed"; break;
        case QProcess::Timedout: errorStr = "Process timed out"; break;
        case QProcess::WriteError: errorStr = "Write error"; break;
        case QProcess::ReadError: errorStr = "Read error"; break;
        default: errorStr = "Unknown error"; break;
    }
    emit terminalError(errorStr);
    LOG_ERROR("Terminal error: " + errorStr);
}

void RemoteTerminal::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    Q_UNUSED(status);
    emit terminalClosed(exitCode);
    LOG_INFO("Terminal closed with exit code: " + QString::number(exitCode));
}

} // namespace xrk
