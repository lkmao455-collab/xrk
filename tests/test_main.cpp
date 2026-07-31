#include <gtest/gtest.h>
#include <QGuiApplication>

// Custom test harness entry point. A QGuiApplication is required so that
// Qt timer/event-loop based code under test (RelayServer, P2PManager, ...) and
// the waitMs() helper in the P2P tests actually fire timers, and so that
// Qt icon-engine plugins (e.g. the SVG icon engine) are available for tests
// that verify resource icons load correctly.
int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
