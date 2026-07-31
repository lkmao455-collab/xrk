#include <gtest/gtest.h>
#include <QApplication>

// Custom test harness entry point. A QApplication is required so that
// Qt timer/event-loop based code under test (RelayServer, P2PManager, ...) and
// the waitMs() helper in the P2P tests actually fire timers, and so that
// Qt icon-engine plugins (e.g. the SVG icon engine) are available for tests
// that verify resource icons load correctly. QApplication (rather than
// QGuiApplication) is needed so widget-based tests can dispatch drag events.
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
