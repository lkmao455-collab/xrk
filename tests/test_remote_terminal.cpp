#include <gtest/gtest.h>
#include "app/remote_terminal.h"
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QEventLoop>

using namespace xrk;

class RemoteTerminalTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }

    static void pumpEvents(int ms) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
    }
};

TEST_F(RemoteTerminalTest, StartStop) {
    RemoteTerminal term;
    ASSERT_TRUE(term.startTerminal("cmd"));
    EXPECT_TRUE(term.isRunning());
    term.stopTerminal();
    EXPECT_FALSE(term.isRunning());
}

TEST_F(RemoteTerminalTest, EchoOutput) {
    RemoteTerminal term;
    QSignalSpy outputSpy(&term, &RemoteTerminal::outputReady);
    QSignalSpy closedSpy(&term, &RemoteTerminal::terminalClosed);

    ASSERT_TRUE(term.startTerminal("cmd"));
    term.writeInput("echo xrk_terminal_marker");

    bool found = false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000 && !found) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        for (const auto& args : outputSpy) {
            QByteArray data = args.at(0).toByteArray();
            if (data.contains("xrk_terminal_marker")) {
                found = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found);

    term.writeInput("exit");
    bool closed = false;
    QElapsedTimer timer2;
    timer2.start();
    while (timer2.elapsed() < 5000 && !closed) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        closed = closedSpy.count() > 0;
    }
    EXPECT_TRUE(closed);
    term.stopTerminal();
    EXPECT_FALSE(term.isRunning());
}
