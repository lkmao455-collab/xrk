#pragma once

#include "core/types.h"
#include <QList>

namespace xrk {

// Cross-platform process enumeration / control used by the Remote Process Manager.
class ProcessCollector {
public:
    // Snapshot of all running processes (pid, name, working-set bytes).
    static QList<ProcessEntry> collectProcessList();

    // Terminate a process by pid. Returns true on success; err holds a message on failure.
    static bool killProcess(qint64 pid, QString& err);

    // Start a command line. On success pidOut receives the new process id.
    static bool startProcess(const QString& command, const QString& workingDir,
                             qint64& pidOut, QString& err);
};

} // namespace xrk
