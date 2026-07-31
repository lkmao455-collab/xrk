#include "system_info_collector.h"
#include "core/logger.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
    DWORD cbNeeded = 0;
    if (EnumProcesses(nullptr, 0, &cbNeeded) && cbNeeded > 0) {
        QVector<DWORD> processes(cbNeeded / sizeof(DWORD));
        if (EnumProcesses(processes.data(), cbNeeded, &cbNeeded)) {
            info.processCount = static_cast<int>(cbNeeded / sizeof(DWORD));
        }
    }

    LOG_DEBUG("SysInfo collected: CPU=" + QString::number(info.cpuUsage, 'f', 1) +
              "% Mem=" + QString::number(info.memoryUsage, 'f', 1) + "%");

    return info;
}

} // namespace xrk
