#include <gtest/gtest.h>
#include "app/clipboard_manager.h"
#include <QApplication>
#include <QClipboard>
#include <QTest>

using namespace xrk;

// Verifies the new host-side / bidirectional clipboard behaviors:
//  - setBroadcastCallback is preferred over a single TcpConnection
//  - applyRemoteClipboard writes local clipboard WITHOUT triggering a
//    re-broadcast (prevents infinite sync loops)
class ClipboardManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager = new ClipboardManager(nullptr, nullptr);  // (TcpConnection*, parent)
        broadcastCount = 0;
        lastBroadcast = ClipboardData();
        manager->setBroadcastCallback([this](const ClipboardData& d) {
            ++broadcastCount;
            lastBroadcast = d;
        });
        manager->startMonitoring();
        // Use the check timer path (processEvents drives it) but we won't rely
        // on it; instead we directly exercise applyRemoteClipboard + the
        // loop-guard behavior via the timer's content comparison.
    }

    void TearDown() override {
        delete manager;
    }

    ClipboardManager* manager = nullptr;
    int broadcastCount = 0;
    ClipboardData lastBroadcast;
};

TEST_F(ClipboardManagerTest, ApplyRemoteDoesNotBroadcast) {
    // Writing remote content must NOT call the broadcast callback (loop guard).
    manager->applyRemoteClipboard("hello-from-remote", "text/plain");
    EXPECT_EQ(broadcastCount, 0);

    // The local clipboard should now contain the applied text.
    QClipboard* cb = QApplication::clipboard();
    EXPECT_EQ(cb->text(), QString("hello-from-remote"));
}

TEST_F(ClipboardManagerTest, LocalChangeBroadcasts) {
    // Simulate a local clipboard change: set text locally, then drive the
    // manager's monitoring. We cannot easily trigger QClipboard::changed in a
    // headless test, so assert the broadcast callback is wired and that the
    // manager prefers it over a (null) connection.
    EXPECT_NE(manager, nullptr);
    // A local change through the timer path would broadcast; we just confirm
    // the callback is stored/used by sending via the public path indirectly:
    // set local clipboard then call onCheckTimer through the public timer.
    QApplication::clipboard()->setText("local-change");
    // Allow the internal check timer (500ms) to fire once.
    QTest::qWait(700);
    EXPECT_GE(broadcastCount, 1);
    EXPECT_EQ(lastBroadcast.mimeType, QString("text/plain"));
}
