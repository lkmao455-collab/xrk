#pragma once

#include <QObject>
#include <QProcess>
#include <QByteArray>
#include <memory>
#include "core/types.h"

namespace xrk {

class TcpConnection;

class RemoteTerminal : public QObject {
    Q_OBJECT
public:
    explicit RemoteTerminal(QObject* parent = nullptr);
    ~RemoteTerminal();

    bool startTerminal(const QString& shellType, uint32_t cols = 80, uint32_t rows = 25);
    void stopTerminal();
    void writeInput(const QByteArray& data);
    void resize(uint32_t cols, uint32_t rows);
    bool isRunning() const;

signals:
    void outputReady(const QByteArray& data);
    void terminalError(const QString& error);
    void terminalClosed(int exitCode);

private slots:
    void onReadyRead();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    QProcess* m_process = nullptr;
    QString m_shellType;
};

} // namespace xrk
