#pragma once

#include <QString>

#ifdef _WIN32

// windows.h is needed for SERVICE_STATUS / SERVICE_STATUS_HANDLE / DWORD used
// below. It must come AFTER core/types.h (MessageType) is parsed to avoid the
// MOUSE_EVENT/KEY_EVENT macro clash — every translation unit includes this
// header only after types.h has been seen (via host.h / main_window.h).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Windows service wrapper for the XRK host.
//
// NOTE (session-0 isolation): a Windows service runs in session 0, which has
// NO access to the interactive user's desktop. That means DXGI/GDI screen
// capture and SendInput-style input injection will NOT work from a service
// process. Screen/input therefore only function when the app runs as a normal
// interactive (tray-resident) GUI process.
//
// This service mode is still useful as an *always-on listener*: it brings up the
// Host (network accept, encryption handshake, consent bookkeeping) under the
// SCM so the machine can be reached and the connection gated even before a user
// logs in. When a real remote session is needed, the interactive (tray) GUI is
// the supported path. We log this limitation loudly on start.

namespace xrk {

class HostService {
public:
    // Register/unregister the service in the SCM (must run elevated).
    static bool installService();
    static bool uninstallService();

    // Entry point used when the SCM launches us with --service. Blocks inside
    // StartServiceCtrlDispatcher; the real work happens in serviceMain().
    static int runService(int argc, char** argv);

    // The actual service body: builds a headless Qt event loop and runs Host.
    static int serviceMain(int argc, char** argv);

private:
    static void WINAPI ctrlHandler(DWORD ctrl);
    static void reportStatus(DWORD state, DWORD exitCode = 0);

    static SERVICE_STATUS_HANDLE s_statusHandle;
    static SERVICE_STATUS s_status;
    static volatile bool s_running;
};

} // namespace xrk

#else // non-Windows: stub declarations so callers compile but can never run

namespace xrk {

class HostService {
public:
    static bool installService()  { return false; }
    static bool uninstallService() { return false; }
    static int  runService(int, char**) { return 1; }
    static int  serviceMain(int, char**) { return 1; }
};

} // namespace xrk

#endif // _WIN32
