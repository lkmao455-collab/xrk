#include <gtest/gtest.h>
#include "app/host.h"
#include "app/remote_controller.h"
#include "ui/remote_desktop_widget.h"
#include "core/network_manager.h"
#include "core/logger.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QImage>

namespace xrk {

// End-to-end reproduction of the "mouse works but desktop is black" pipeline:
// a REAL Host (localhost) + a REAL RemoteController + a REAL RemoteDesktopWidget,
// driven over a REAL TCP connection. Both ends emit [DIAG] markers, all written
// to one log file, so the exact failing stage can be read directly.
TEST(E2E, BlackScreenPipeline) {
    // Capture [DIAG] output from BOTH ends into a single file.
    Logger::instance().setLogFile("E:/xrk/e2e_diag.log");

    Host host;
    host.setAudioEnabled(false);  // avoid WASAPI/COM init in headless
    // Auto-grant consent exactly like the headless-host / LAN-auto-grant path.
    QObject::connect(&host, &Host::consentRequested, &host,
                     [&host](const QString& clientId, const QString&) {
                         host.grantConsent(clientId);
                     });
    ASSERT_TRUE(host.start(19997));

    NetworkManager net;
    RemoteController ctrl(&net, nullptr);

    bool gotFrame = false;
    QObject::connect(&ctrl, &RemoteController::screenFrameReceived,
                     [&](const ScreenFrame&) { gotFrame = true; });

    // Real widget so the Widget-side [DIAG] marker fires.
    RemoteDesktopWidget widget(&ctrl);

    ASSERT_TRUE(ctrl.startRemote("127.0.0.1", 19997));

    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 5000) {
        QApplication::processEvents();
        QThread::msleep(10);
    }

    ctrl.stopRemote();
    host.stop();

    EXPECT_TRUE(gotFrame)
        << "No screen frame reached the controller over a real localhost TCP connection. "
           "Inspect /e/xrk/e2e_diag.log for the [DIAG] markers to see where the pipeline broke.";
}

} // namespace xrk
