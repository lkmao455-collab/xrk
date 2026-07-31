#include <gtest/gtest.h>
#include <QCoreApplication>
#include "host.h"

namespace xrk {

// Diagnostic repro: start a real Host, stop it, then let it go out of scope so
// the destructor runs. The destructor (and the media-object destructors) carry
// fprintf(stderr, ...) markers that tell us exactly which object blocks during
// headless teardown. Keep this test in the suite once the hang is fixed so we
// never regress.
TEST(HostTeardown, StartsStopsAndDestroys) {
    Host host;
    host.setAudioEnabled(false);  // avoid WASAPI/COM init in headless
    ASSERT_TRUE(host.start(19999));
    EXPECT_TRUE(host.isRunning());
    host.stop();
    EXPECT_FALSE(host.isRunning());
    // Let `host` fall out of scope -> ~Host() runs. If teardown blocks this
    // test process will hang; markers on stderr show the last object reached.
}

} // namespace xrk
