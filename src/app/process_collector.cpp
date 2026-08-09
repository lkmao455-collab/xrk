#include "process_collector.h"

#include <QProcess>
#include <QDir>
#include <QFile>
#include <QVector>

#if defined(Q_OS_WIN)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <tlhelp32.h>
#  include <psapi.h>
#elif defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
#  include <unistd.h>
#  include <signal.h>
#  include <errno.h>
#  include <sys/types.h>
#endif

#if defined(Q_OS_MACOS)
#  include <sys/sysctl.h>
#endif

namespace xrk {

QList<ProcessEntry> ProcessCollector::collectProcessList() {
    QList<ProcessEntry> result;

#if defined(Q_OS_WIN)
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return result;
    }
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snapshot, &pe)) {
        do {
            ProcessEntry entry;
            entry.pid = pe.th32ProcessID;
            entry.name = QString::fromWCharArray(pe.szExeFile);
            // Working set size via psapi (best-effort).
            HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
            if (hProc) {
                PROCESS_MEMORY_COUNTERS_EX counters = {};
                counters.cb = sizeof(counters);
                if (GetProcessMemoryInfo(hProc, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
                    entry.memoryBytes = static_cast<qint64>(counters.WorkingSetSize);
                }
                CloseHandle(hProc);
            }
            result.append(entry);
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);

#elif defined(Q_OS_LINUX)
    QDir procDir("/proc");
    const QStringList entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        bool ok = false;
        qint64 pid = entry.toLongLong(&ok);
        if (!ok) continue;
        ProcessEntry pe;
        pe.pid = pid;
        QFile comm(QString("/proc/%1/comm").arg(pid));
        if (comm.open(QIODevice::ReadOnly)) {
            pe.name = QString::fromUtf8(comm.readAll()).trimmed();
            comm.close();
        }
        QFile status(QString("/proc/%1/status").arg(pid));
        if (status.open(QIODevice::ReadOnly)) {
            const QByteArray data = status.readAll();
            status.close();
            int idx = data.indexOf("VmRSS:");
            if (idx >= 0) {
                int end = data.indexOf('\n', idx);
                QByteArray line = data.mid(idx, end - idx);
                // VmRSS:   12345 kB
                QList<QByteArray> parts = line.split(':');
                if (parts.size() == 2) {
                    QList<QByteArray> kv = parts[1].split(' ');
                    if (kv.size() >= 2) {
                        bool ok2 = false;
                        qint64 kb = kv[1].toLongLong(&ok2);
                        if (ok2) pe.memoryBytes = kb * 1024;
                    }
                }
            }
        }
        if (pe.name.isEmpty()) pe.name = QString::number(pid);
        result.append(pe);
    }

#elif defined(Q_OS_MACOS)
    // Best-effort: enumerate via sysctl(KERN_PROC). Memory omitted.
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
    size_t size = 0;
    if (sysctl(mib, 4, nullptr, &size, nullptr, 0) == 0 && size > 0) {
        QVector<struct kinfo_proc> procs(size / sizeof(struct kinfo_proc));
        if (sysctl(mib, 4, procs.data(), &size, nullptr, 0) == 0) {
            int count = static_cast<int>(size / sizeof(struct kinfo_proc));
            for (int i = 0; i < count; ++i) {
                ProcessEntry pe;
                pe.pid = procs[i].kp_proc.p_pid;
                pe.name = QString::fromUtf8(procs[i].kp_proc.p_comm);
                result.append(pe);
            }
        }
    }
#else
    Q_UNUSED(0);
#endif

    return result;
}

bool ProcessCollector::killProcess(qint64 pid, QString& err) {
    if (pid <= 0) {
        err = QString("无效的进程 ID: %1").arg(pid);
        return false;
    }

#if defined(Q_OS_WIN)
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (!hProc) {
        err = QString("无法打开进程 %1 (错误 %2)").arg(pid).arg(static_cast<int>(GetLastError()));
        return false;
    }
    bool ok = TerminateProcess(hProc, 1) != 0;
    CloseHandle(hProc);
    if (!ok) {
        err = QString("结束进程 %1 失败 (错误 %2)").arg(pid).arg(static_cast<int>(GetLastError()));
        return false;
    }
    return true;

#elif defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    if (::kill(static_cast<pid_t>(pid), SIGKILL) != 0) {
        err = QString("结束进程 %1 失败: %2").arg(pid).arg(strerror(errno));
        return false;
    }
    return true;
#else
    err = "当前平台不支持结束进程";
    return false;
#endif
}

bool ProcessCollector::startProcess(const QString& command, const QString& workingDir,
                                    qint64& pidOut, QString& err) {
    if (command.trimmed().isEmpty()) {
        err = "命令为空";
        return false;
    }
    QStringList args = QProcess::splitCommand(command);
    if (args.isEmpty()) {
        err = "命令解析失败";
        return false;
    }
    const QString program = args.takeFirst();
    qint64 pid = 0;
    bool ok = QProcess::startDetached(program, args, workingDir.isEmpty() ? QDir::currentPath() : workingDir, &pid);
    if (!ok) {
        err = QString("启动命令失败: %1").arg(command);
        return false;
    }
    pidOut = pid;
    return true;
}

} // namespace xrk
