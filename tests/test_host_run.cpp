#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QThread>
#include "host.h"

namespace xrk {

// Regression guard for the "启动服务闪退" bug: starting the host must not
// crash while the capture/encode worker threads are actually running. The
// earlier failure deleted and re-created the capture worker (and restarted its
// thread) from the GUI thread right after start(), causing a cross-thread
// QObject deletion / data race. Here we let the workers run for a while so any
// such defect aborts the whole process and fails the test.
TEST(HostRun, StartsAndRunsWithoutCrash) {
    Host host;
    host.setAudioEnabled(false);  // avoid WASAPI/COM init in headless
    ASSERT_TRUE(host.start(19998));
    EXPECT_TRUE(host.isRunning());

    // Apply FPS the way the UI does (must be safe on a running host now).
    host.setCaptureFps(30);

    // Keep the main thread alive while the capture/encode/network workers run.
    QThread::msleep(1200);

    host.stop();
    EXPECT_FALSE(host.isRunning());
}

} // namespace xrk
