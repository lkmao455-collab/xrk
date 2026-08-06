#include "system_info_collector.h"
#include "core/logger.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <pdh.h>
#include <psapi.h>
#include <QDir>
#include <QStorageInfo>
#include <QSysInfo>
#include <QVector>

#pragma comment(lib, "pdh.lib")

namespace xrk {

static PDH_HQUERY s_cpuQuery = nullptr;
static PDH_HCOUNTER s_cpuCounter = nullptr;
static bool s_cpuInit = false;

static bool initCpuCounter() {
    if (s_cpuInit) return true;
    if (PdhOpenQueryW(nullptr, 0, &s_cpuQuery) != ERROR_SUCCESS) {
        s_cpuInit = true;
        return false;
    }
    if (PdhAddEnglishCounterW(s_cpuQuery, L"\\Processor(_Total)\\% Processor Time",
                               0, &s_cpuCounter) != ERROR_SUCCESS) {
        PdhCloseQuery(s_cpuQuery);
        s_cpuQuery = nullptr;
        s_cpuInit = true;
        return false;
    }
    PdhCollectQueryData(s_cpuQuery);
    s_cpuInit = true;
    return true;
}

static double getCpuUsage() {
    if (!initCpuCounter()) return 0.0;
    PdhCollectQueryData(s_cpuQuery);
    Sleep(200);
    PdhCollectQueryData(s_cpuQuery);

    PDH_FMT_COUNTERVALUE counterVal;
    if (PdhGetFormattedCounterValue(s_cpuCounter, PDH_FMT_DOUBLE,
                                     nullptr, &counterVal) == ERROR_SUCCESS) {
        return counterVal.doubleValue;
    }
    return 0.0;
}

SysInfo SystemInfoCollector::collect() {
    SysInfo info;

    // OS info
    info.osName = "Windows";
    info.osVersion = QSysInfo::kernelVersion();

    // CPU
    info.cpuUsage = getCpuUsage();

    // CPU name via registry
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        WCHAR buffer[256];
        DWORD size = sizeof(buffer);
        if (RegQueryValueExW(hKey, L"ProcessorNameString", nullptr,
                             nullptr, (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
            info.cpuName = QString::fromWCharArray(buffer);
        }
        RegCloseKey(hKey);
    }

    // Memory
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(memInfo);
    if (GlobalMemoryStatusEx(&memInfo)) {
        info.memoryTotal = memInfo.ullTotalPhys;
        info.memoryAvailable = memInfo.ullAvailPhys;
        info.memoryUsage = 100.0 * (1.0 - (double)memInfo.ullAvailPhys / memInfo.ullTotalPhys);
    }

    // Disk (system drive)
    QStorageInfo storage(QDir::rootPath());
    if (storage.isValid()) {
        info.diskTotal = static_cast<uint64_t>(storage.bytesTotal());
        info.diskFree = static_cast<uint64_t>(storage.bytesAvailable());
        if (info.diskTotal > 0) {
            info.diskUsage = 100.0 * (1.0 - (double)info.diskFree / info.diskTotal);
        }
    }

    // Uptime
    info.uptime = static_cast<uint64_t>(GetTickCount64() / 1000);

    // Process count
    info.processCount = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snapshot, &pe)) {
            info.processCount = 1;
            while (Process32NextW(snapshot, &pe)) {
                ++info.processCount;
            }
        }
        CloseHandle(snapshot);
    }

    LOG_DEBUG("SysInfo collected: CPU=" + QString::number(info.cpuUsage, 'f', 1) +
              "% Mem=" + QString::number(info.memoryUsage, 'f', 1) + "%");

    return info;
}

} // namespace xrk

#else // non-Windows: cross-platform implementation using Qt

#include <QDir>
#include <QStorageInfo>
#include <QSysInfo>
#include <QFile>
#include <QTextStream>
#include <unistd.h>
#include <sys/time.h>

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#else
// Linux: /proc filesystem
#endif

namespace xrk {

SysInfo SystemInfoCollector::collect() {
    SysInfo info;

    // OS info
#ifdef __APPLE__
    info.osName = "macOS";
#else
    info.osName = "Linux";
#endif
    info.osVersion = QSysInfo::kernelVersion();

    // CPU usage (simplified: read /proc/stat on Linux, sysctl on macOS)
    info.cpuUsage = 0.0;
#ifdef __linux__
    QFile statFile("/proc/stat");
    if (statFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString line = statFile.readLine();
        statFile.close();
        // Parse "cpu  user nice system idle iowait irq softirq steal"
        QStringList parts = line.split(QRegularExpression("\\s+"));
        if (parts.size() >= 5) {
            long user = parts[1].toLong();
            long nice = parts[2].toLong();
            long system = parts[3].toLong();
            long idle = parts[4].toLong();
            long total = user + nice + system + idle;
            long active = user + nice + system;
            if (total > 0) {
                info.cpuUsage = 100.0 * active / total;
            }
        }
    }
#elif defined(__APPLE__)
    host_cpu_load_info_data_t cpuinfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        reinterpret_cast<integer_t*>(&cpuinfo), &count) == KERN_SUCCESS) {
        uint64_t total = cpuinfo.cpu_ticks[CPU_STATE_USER] +
                         cpuinfo.cpu_ticks[CPU_STATE_SYSTEM] +
                         cpuinfo.cpu_ticks[CPU_STATE_IDLE] +
                         cpuinfo.cpu_ticks[CPU_STATE_NICE];
        uint64_t active = total - cpuinfo.cpu_ticks[CPU_STATE_IDLE];
        if (total > 0) {
            info.cpuUsage = 100.0 * active / total;
        }
    }
#endif

    // CPU name
#ifdef __linux__
    QFile cpuInfo("/proc/cpuinfo");
    if (cpuInfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!cpuInfo.atEnd()) {
            QString line = cpuInfo.readLine();
            if (line.startsWith("model name")) {
                info.cpuName = line.section(":", 1).trimmed();
                break;
            }
        }
        cpuInfo.close();
    }
#elif defined(__APPLE__)
    char buffer[128];
    size_t size = sizeof(buffer);
    if (sysctlbyname("machdep.cpu.brand_string", buffer, &size, nullptr, 0) == 0) {
        info.cpuName = QString::fromLocal8Bit(buffer);
    }
#endif

    // Memory
#ifdef __linux__
    QFile memInfo("/proc/meminfo");
    if (memInfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!memInfo.atEnd()) {
            QString line = memInfo.readLine();
            if (line.startsWith("MemTotal")) {
                info.memoryTotal = line.section(":", 1).section("kB", 0, 0).trimmed().toULongLong() * 1024;
            } else if (line.startsWith("MemAvailable")) {
                info.memoryAvailable = line.section(":", 1).section("kB", 0, 0).trimmed().toULongLong() * 1024;
            }
        }
        memInfo.close();
        if (info.memoryTotal > 0) {
            info.memoryUsage = 100.0 * (1.0 - (double)info.memoryAvailable / info.memoryTotal);
        }
    }
#elif defined(__APPLE__)
    uint64_t totalMem = 0;
    size_t size = sizeof(totalMem);
    if (sysctlbyname("hw.memsize", &totalMem, &size, nullptr, 0) == 0) {
        info.memoryTotal = totalMem;
        // Approximate available memory using vm_stat
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        vm_statistics64_data_t vmStats;
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                              reinterpret_cast<integer_t*>(&vmStats), &count) == KERN_SUCCESS) {
            uint64_t pageSize = vm_page_size;
            info.memoryAvailable = (vmStats.free_count + vmStats.inactive_count) * pageSize;
            if (info.memoryTotal > 0) {
                info.memoryUsage = 100.0 * (1.0 - (double)info.memoryAvailable / info.memoryTotal);
            }
        }
    }
#endif

    // Disk
    QStorageInfo storage(QDir::rootPath());
    if (storage.isValid()) {
        info.diskTotal = static_cast<uint64_t>(storage.bytesTotal());
        info.diskFree = static_cast<uint64_t>(storage.bytesAvailable());
        if (info.diskTotal > 0) {
            info.diskUsage = 100.0 * (1.0 - (double)info.diskFree / info.diskTotal);
        }
    }

    // Uptime
#ifdef __linux__
    QFile uptimeFile("/proc/uptime");
    if (uptimeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString line = uptimeFile.readLine();
        uptimeFile.close();
        double seconds = line.section(" ", 0, 0).toDouble();
        info.uptime = static_cast<uint64_t>(seconds);
    }
#elif defined(__APPLE__)
    struct timeval tv;
    size_t size = sizeof(tv);
    int mib[2] = { CTL_KERN, KERN_BOOTTIME };
    if (sysctl(mib, 2, &tv, &size, nullptr, 0) == 0) {
        info.uptime = static_cast<uint64_t>(time(nullptr) - tv.tv_sec);
    }
#endif

    // Process count
#ifdef __linux__
    QDir procDir("/proc");
    QStringList entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    int count = 0;
    for (const QString& entry : entries) {
        bool ok;
        entry.toULongLong(&ok);
        if (ok) count++;
    }
    info.processCount = count;
#elif defined(__APPLE__)
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
    size_t size = 0;
    if (sysctl(mib, 4, nullptr, &size, nullptr, 0) == 0) {
        info.processCount = size / sizeof(struct kinfo_proc);
    }
#endif

    LOG_DEBUG("SysInfo collected: CPU=" + QString::number(info.cpuUsage, 'f', 1) +
              "% Mem=" + QString::number(info.memoryUsage, 'f', 1) + "%");

    return info;
}

} // namespace xrk

#endif // _WIN32
