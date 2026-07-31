#include "app/host.h"          // host.h pulls in core/types.h BEFORE any <windows.h>,
                                // avoiding the MOUSE_EVENT/KEY_EVENT macro clash.
#include "app/host_service.h"
#include "core/types.h"
#include "core/logger.h"

#include <QApplication>
#include <QSettings>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>

namespace xrk {

SERVICE_STATUS_HANDLE HostService::s_statusHandle = nullptr;
SERVICE_STATUS HostService::s_status = {};
volatile bool HostService::s_running = false;

// Stored so the SCM-style ServiceMain thunk can forward to serviceMain(int,char**).
static int s_argc = 0;
static char** s_argv = nullptr;

namespace {
    const TCHAR* kServiceName = TEXT("XRKHostService");
    const TCHAR* kServiceDisplay = TEXT("XRK Remote Host Service");

    QApplication* g_app = nullptr;
    Host* g_host = nullptr;

    void WINAPI serviceMainThunk(DWORD argc, LPTSTR* argv) {
        Q_UNUSED(argc);
        Q_UNUSED(argv);
        HostService::serviceMain(s_argc, s_argv);
    }
}

void HostService::reportStatus(DWORD state, DWORD exitCode) {
    s_status.dwCurrentState = state;
    s_status.dwWin32ExitCode = exitCode;
    s_status.dwWaitHint = (state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING) ? 3000 : 0;
    if (s_statusHandle) {
        SetServiceStatus(s_statusHandle, &s_status);
    }
}

void WINAPI HostService::ctrlHandler(DWORD ctrl) {
    switch (ctrl) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            reportStatus(SERVICE_STOP_PENDING);
            s_running = false;
            if (g_host) g_host->stop();
            if (g_app) g_app->quit();
            break;
        default:
            reportStatus(s_status.dwCurrentState);
            break;
    }
}

int HostService::serviceMain(int argc, char** argv) {
    Q_UNUSED(argc);
    Q_UNUSED(argv);

    s_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    s_status.dwCurrentState = SERVICE_START_PENDING;
    s_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    s_status.dwWin32ExitCode = 0;

    s_statusHandle = RegisterServiceCtrlHandler(kServiceName, ctrlHandler);
    if (!s_statusHandle) {
        LOG_ERROR("HostService: RegisterServiceCtrlHandler failed");
        return 1;
    }
    reportStatus(SERVICE_START_PENDING);

    LOG_WARNING("HostService: running as a Windows service (session-0). "
                "Screen capture and input injection are NOT available in this "
                "mode; use the interactive (tray) GUI for a real remote session. "
                "This service provides an always-on listener + connection gating.");

    QApplication app(s_argc, s_argv);
    g_app = &app;

    Host* host = new Host();
    g_host = host;

    // Read the same settings the GUI uses.
    QSettings settings("XRK", "XRK");
    uint16_t port = settings.value("host/port", DEFAULT_PORT).value<uint16_t>();
    QString password = settings.value("security/password", "").toString();
    if (!password.isEmpty()) {
        host->setPassword(password);
    }

    // In service mode there is no human to approve, so the consent dialog is
    // replaced by an automatic decision: allow only when a password is set
    // (otherwise an unattended, open host would be reachable by anyone).
    QObject::connect(host, &Host::consentRequested, host, [host](const QString& clientId, const QString&) {
        if (host->isPasswordRequired()) {
            LOG_INFO("HostService: auto-granting consent for " + clientId + " (password protected)");
            host->grantConsent(clientId);
        } else {
            LOG_WARNING("HostService: refusing consent for " + clientId +
                        " (no password configured; unattended host left closed)");
            host->denyConsent(clientId);
        }
    });

    if (!host->start(port)) {
        LOG_ERROR("HostService: failed to start host on port " + QString::number(port));
        reportStatus(SERVICE_STOPPED, 1);
        delete host;
        g_host = nullptr;
        return 1;
    }

    reportStatus(SERVICE_RUNNING);

    s_running = true;
    int ret = app.exec();
    s_running = false;

    host->stop();
    delete host;
    g_host = nullptr;

    reportStatus(SERVICE_STOPPED);
    return ret;
}

int HostService::runService(int argc, char** argv) {
    s_argc = argc;
    s_argv = argv;

    SERVICE_TABLE_ENTRY table[] = {
        { const_cast<LPTSTR>(kServiceName), serviceMainThunk },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcher(table)) {
        DWORD err = GetLastError();
        if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            LOG_ERROR("HostService: not started by the SCM (use --install-service then start the service).");
        } else {
            LOG_ERROR("HostService: StartServiceCtrlDispatcher failed (" + QString::number(err) + ")");
        }
        return 1;
    }
    return 0;
}

bool HostService::installService() {
    TCHAR path[MAX_PATH];
    DWORD len = GetModuleFileName(nullptr, path, MAX_PATH);
    if (len == 0) {
        LOG_ERROR("HostService: GetModuleFileName failed");
        return false;
    }

    // Append the --service argument.
    QString fullPath = QString::fromWCharArray(path) + " --service";

    SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        LOG_ERROR("HostService: OpenSCManager failed (run as administrator)");
        return false;
    }

    SC_HANDLE svc = CreateService(
        scm, kServiceName, kServiceDisplay,
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
        SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
        reinterpret_cast<const TCHAR*>(fullPath.utf16()),
        nullptr, nullptr, nullptr, nullptr, nullptr);

    bool ok = true;
    if (!svc) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS) {
            LOG_WARNING("HostService: service already installed");
        } else {
            LOG_ERROR("HostService: CreateService failed (" + QString::number(err) + ")");
            ok = false;
        }
    } else {
        LOG_INFO("HostService: installed. Start it with 'sc start XRKHostService' "
                 "(or Services.msc). It runs as a listener only (session-0 limitation).");
        CloseServiceHandle(svc);
    }

    CloseServiceHandle(scm);
    return ok;
}

bool HostService::uninstallService() {
    SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        LOG_ERROR("HostService: OpenSCManager failed (run as administrator)");
        return false;
    }

    SC_HANDLE svc = OpenService(scm, kServiceName, SERVICE_ALL_ACCESS);
    if (!svc) {
        LOG_WARNING("HostService: service not installed");
        CloseServiceHandle(scm);
        return true;
    }

    // Stop if running.
    SERVICE_STATUS status;
    if (ControlService(svc, SERVICE_CONTROL_STOP, &status)) {
        LOG_INFO("HostService: sent stop request");
    }

    bool ok = DeleteService(svc);
    if (!ok) {
        LOG_ERROR("HostService: DeleteService failed (" + QString::number(GetLastError()) + ")");
    } else {
        LOG_INFO("HostService: uninstalled");
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

} // namespace xrk
