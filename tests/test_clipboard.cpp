#include <gtest/gtest.h>
#include "app/clipboard_manager.h"
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
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
    // Simulate a local clipboard change: write text to the local clipboard and
    // let the manager's monitoring detect and broadcast it.
    //
    // On Windows a clipboard write can silently fail if a prior write still
    // owns the clipboard, so clear it first. setMimeData is used rather than
    // setText because setText alone was observed to leave the clipboard empty
    // in this headless test environment.
    QApplication::clipboard()->clear();
    QApplication::processEvents();

    QMimeData* mime = new QMimeData();
    mime->setText("local-change");
    QApplication::clipboard()->setMimeData(mime);

    // Poll up to 2s for the broadcast: the 500ms check timer plus Windows
    // clipboard propagation latency can exceed a fixed wait in a busy suite,
    // which would make this test flaky.
    QElapsedTimer timer;
    timer.start();
    while (broadcastCount < 1 && timer.elapsed() < 2000) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::qWait(50);
    }
    EXPECT_GE(broadcastCount, 1);
    EXPECT_EQ(lastBroadcast.mimeType, QString("text/plain"));
}
