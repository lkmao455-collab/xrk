#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QTemporaryDir>
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

// Task 26: controller-pinned quality gear maps to the right jpeg quality /
// capture fps and suspends (or resumes) measured-bandwidth adaptation.
//
// We exercise setQualityLevel() WITHOUT Host::start(): at construction the
// encode/capture worker threads do not yet exist, so the setters apply safely
// to the stored m_jpegQuality / m_captureFps / m_qualityMode values. This
// avoids the headless screen-capture init (which blocks offscreen) while still
// validating the gear mapping and the AUTO-resume flag. The real Host path
// (worker-thread fps swap, encoder selection) is covered by the running app.
TEST(HostTeardown, SetQualityLevelAppliesGear) {
    Host host;

    // HIGH gear -> jpeg 82, fps 30, adaptation suspended.
    host.setQualityLevel(QualityLevel::HIGH, false);
    EXPECT_EQ(host.qualityMode(), QualityLevel::HIGH);
    EXPECT_EQ(host.jpegQuality(), 82);
    EXPECT_EQ(host.captureFps(), 30);

    // LOW gear -> jpeg 45, fps 24.
    host.setQualityLevel(QualityLevel::LOW, false);
    EXPECT_EQ(host.qualityMode(), QualityLevel::LOW);
    EXPECT_EQ(host.jpegQuality(), 45);
    EXPECT_EQ(host.captureFps(), 24);

    // ULTRA + game mode -> jpeg 92, fps 60.
    host.setQualityLevel(QualityLevel::ULTRA, true);
    EXPECT_EQ(host.qualityMode(), QualityLevel::ULTRA);
    EXPECT_EQ(host.jpegQuality(), 92);
    EXPECT_EQ(host.captureFps(), 60);

    // AUTO gear -> adaptation resumes.
    host.setQualityLevel(QualityLevel::AUTO, false);
    EXPECT_EQ(host.qualityMode(), QualityLevel::AUTO);
}

// Task 27b: reverse-sync registration on the host. addReverseSync() must create
// a watcher for the (client, hostDir) pair; the paired client disconnecting (or
// an explicit remove) must tear it down. No Host::start() needed — the watcher
// is a QFileSystemWatcher, not the screen-capture threads.
TEST(HostTeardown, ReverseSyncPairRegistration) {
    Host host;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString clientId = "client-1";
    QString hostDir = dir.path();

    host.addReverseSync(clientId, hostDir, "D:/Local/Target");
    EXPECT_TRUE(host.hasReverseSync(clientId, hostDir));

    // Re-adding the same pair replaces (still exactly one watcher).
    host.addReverseSync(clientId, hostDir, "D:/Local/Target2");
    EXPECT_TRUE(host.hasReverseSync(clientId, hostDir));

    // Removing the client's watchers clears it.
    host.removeReverseSyncForClient(clientId);
    EXPECT_FALSE(host.hasReverseSync(clientId, hostDir));
}

} // namespace xrk
