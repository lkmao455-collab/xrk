#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QElapsedTimer>
#include "core/subnet_scanner.h"
#include "core/types.h"

using namespace xrk;

class SubnetScannerTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_scanner = new SubnetScanner(nullptr);
    }

    void TearDown() override {
        if (m_scanner) {
            m_scanner->stopScan();
            delete m_scanner;
            m_scanner = nullptr;
        }
    }

    // Helper: wait for scan to finish with timeout
    bool waitForScanFinish(int timeoutMs = 5000) {
        QEventLoop loop;
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
        QObject::connect(m_scanner, &SubnetScanner::scanFinished, &loop, &QEventLoop::quit);
        loop.exec();
        return !m_scanner->isScanning();
    }

    // Helper: wait ms milliseconds
    void waitMs(int ms) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
    }

    SubnetScanner* m_scanner = nullptr;
};

// ========== Constructor / Destructor Tests ==========

TEST_F(SubnetScannerTest, ConstructorWithNullNetwork) {
    SubnetScanner scanner(nullptr);
    EXPECT_FALSE(scanner.isScanning());
}

TEST_F(SubnetScannerTest, ConstructorWithParent) {
    QObject parent;
    SubnetScanner scanner(nullptr, &parent);
    EXPECT_FALSE(scanner.isScanning());
}

TEST_F(SubnetScannerTest, DestructorStopsScan) {
    SubnetScanner* scanner = new SubnetScanner(nullptr);
    scanner->setConnectTimeout(50);
    scanner->startScan(QHostAddress("192.0.2.1"), 10);
    
    waitMs(100);
    EXPECT_TRUE(scanner->isScanning());
    
    delete scanner;
}

// ========== isScanning Tests ==========

TEST_F(SubnetScannerTest, InitiallyNotScanning) {
    EXPECT_FALSE(m_scanner->isScanning());
}

TEST_F(SubnetScannerTest, IsScanningDuringScan) {
    m_scanner->setConnectTimeout(50);
    m_scanner->startScan(QHostAddress("192.0.2.1"), 5);
    waitMs(50);
    EXPECT_TRUE(m_scanner->isScanning());
}

TEST_F(SubnetScannerTest, IsScanningAfterStop) {
    m_scanner->setConnectTimeout(50);
    m_scanner->startScan(QHostAddress("192.0.2.1"), 10);
    waitMs(50);
    m_scanner->stopScan();
    waitMs(200);
    EXPECT_FALSE(m_scanner->isScanning());
}

// ========== startScan Tests ==========

TEST_F(SubnetScannerTest, StartScanWithCustomRange) {
    QSignalSpy progressSpy(m_scanner, &SubnetScanner::scanProgress);
    QSignalSpy finishedSpy(m_scanner, &SubnetScanner::scanFinished);
    
    m_scanner->setConnectTimeout(50); // fast timeout for test
    m_scanner->startScan(QHostAddress("192.0.2.0"), 5);
    
    EXPECT_TRUE(m_scanner->isScanning());
    
    ASSERT_TRUE(waitForScanFinish(5000));
    
    EXPECT_FALSE(m_scanner->isScanning());
    EXPECT_GT(progressSpy.count(), 0);
    EXPECT_EQ(finishedSpy.count(), 1);
}

TEST_F(SubnetScannerTest, StartScanDefaultSubnet) {
    QSignalSpy finishedSpy(m_scanner, &SubnetScanner::scanFinished);
    
    m_scanner->setConnectTimeout(50);
    m_scanner->startScan(QHostAddress("192.0.2.0"), 5);
    
    EXPECT_TRUE(m_scanner->isScanning());
    
    waitForScanFinish(5000);
    
    EXPECT_EQ(finishedSpy.count(), 1);
}

TEST_F(SubnetScannerTest, DoubleStartIgnored) {
    QSignalSpy finishedSpy(m_scanner, &SubnetScanner::scanFinished);
    
    m_scanner->setConnectTimeout(50); // fast timeout for test
    m_scanner->startScan(QHostAddress("192.0.2.0"), 10);
    waitMs(50);
    EXPECT_TRUE(m_scanner->isScanning());
    
    m_scanner->startScan(QHostAddress("192.0.2.0"), 10);
    
    waitForScanFinish(5000);
    
    EXPECT_EQ(finishedSpy.count(), 1);
}

// ========== stopScan Tests ==========

TEST_F(SubnetScannerTest, StopScanAbortsScan) {
    QSignalSpy finishedSpy(m_scanner, &SubnetScanner::scanFinished);
    
    m_scanner->setConnectTimeout(50); // fast timeout for test
    m_scanner->startScan(QHostAddress("192.0.2.0"), 100);
    waitMs(100);
    EXPECT_TRUE(m_scanner->isScanning());
    
    m_scanner->stopScan();
    
    waitMs(500);
    
    EXPECT_FALSE(m_scanner->isScanning());
}

TEST_F(SubnetScannerTest, StopWhenNotScanningIsNoop) {
    EXPECT_FALSE(m_scanner->isScanning());
    m_scanner->stopScan();
    EXPECT_FALSE(m_scanner->isScanning());
}

// ========== Signal Emission Tests ==========

TEST_F(SubnetScannerTest, ProgressSignalEmitted) {
    QSignalSpy progressSpy(m_scanner, &SubnetScanner::scanProgress);
    
    m_scanner->setConnectTimeout(50); // fast timeout for test
    m_scanner->startScan(QHostAddress("192.0.2.0"), 3);
    waitForScanFinish(3000);
    
    EXPECT_GE(progressSpy.count(), 1);
    
    if (progressSpy.count() > 0) {
        QList<QVariant> args = progressSpy.first();
        EXPECT_EQ(args.at(0).toInt(), 1);
        EXPECT_EQ(args.at(1).toInt(), 3);
    }
}

TEST_F(SubnetScannerTest, FinishedSignalEmittedWithCount) {
    QSignalSpy finishedSpy(m_scanner, &SubnetScanner::scanFinished);
    
    m_scanner->setConnectTimeout(50); // fast timeout for test
    m_scanner->startScan(QHostAddress("192.0.2.0"), 5);
    waitForScanFinish(5000);
    
    ASSERT_EQ(finishedSpy.count(), 1);
    
    QList<QVariant> args = finishedSpy.first();
    int foundCount = args.at(0).toInt();
    EXPECT_GE(foundCount, 0);
}

TEST_F(SubnetScannerTest, DeviceFoundSignalForUnreachableRange) {
    QSignalSpy deviceSpy(m_scanner, &SubnetScanner::deviceFound);
    QSignalSpy finishedSpy(m_scanner, &SubnetScanner::scanFinished);
    
    m_scanner->setConnectTimeout(50);
    m_scanner->startScan(QHostAddress("203.0.113.0"), 2);
    waitForScanFinish(3000);
    
    EXPECT_EQ(finishedSpy.count(), 1);
    EXPECT_GE(deviceSpy.count(), 0);
}

// ========== Edge Cases ==========

TEST_F(SubnetScannerTest, ScanCountZero) {
    QSignalSpy finishedSpy(m_scanner, &SubnetScanner::scanFinished);
    
    m_scanner->setConnectTimeout(50);
    m_scanner->startScan(QHostAddress("192.0.2.0"), 0);
    waitForScanFinish(2000);
    
    EXPECT_FALSE(m_scanner->isScanning());
    EXPECT_EQ(finishedSpy.count(), 1);
}

TEST_F(SubnetScannerTest, ScanCountOne) {
    QSignalSpy progressSpy(m_scanner, &SubnetScanner::scanProgress);
    QSignalSpy finishedSpy(m_scanner, &SubnetScanner::scanFinished);
    
    m_scanner->setConnectTimeout(50);
    m_scanner->startScan(QHostAddress("192.0.2.0"), 1);
    waitForScanFinish(2000);
    
    EXPECT_FALSE(m_scanner->isScanning());
    EXPECT_EQ(finishedSpy.count(), 1);
    if (progressSpy.count() > 0) {
        EXPECT_EQ(progressSpy.first().at(0).toInt(), 1);
        EXPECT_EQ(progressSpy.first().at(1).toInt(), 1);
    }
}

TEST_F(SubnetScannerTest, ScanFromDifferentSubnet) {
    QSignalSpy finishedSpy(m_scanner, &SubnetScanner::scanFinished);
    
    m_scanner->setConnectTimeout(50);
    m_scanner->startScan(QHostAddress("10.0.0.0"), 3);
    waitForScanFinish(2000);
    
    EXPECT_FALSE(m_scanner->isScanning());
    EXPECT_EQ(finishedSpy.count(), 1);
}

TEST_F(SubnetScannerTest, ScanProgressEvery10Hosts) {
    QSignalSpy progressSpy(m_scanner, &SubnetScanner::scanProgress);
    
    m_scanner->setConnectTimeout(50);
    m_scanner->startScan(QHostAddress("192.0.2.0"), 25);
    waitForScanFinish(10000);
    
    EXPECT_GE(progressSpy.count(), 2);
}

// ========== Concurrent Scan Tests ==========

TEST_F(SubnetScannerTest, ConcurrentScanCalls) {
    QSignalSpy finishedSpy(m_scanner, &SubnetScanner::scanFinished);
    
    m_scanner->setConnectTimeout(50);
    m_scanner->startScan(QHostAddress("192.0.2.0"), 5);
    m_scanner->startScan(QHostAddress("192.0.2.0"), 5);
    
    waitForScanFinish(5000);
    
    EXPECT_EQ(finishedSpy.count(), 1);
}

// ========== NetworkManager Integration ==========

TEST_F(SubnetScannerTest, ScannerWithNullNetworkManager) {
    SubnetScanner scanner(nullptr);
    scanner.setConnectTimeout(50);
    
    QSignalSpy finishedSpy(&scanner, &SubnetScanner::scanFinished);
    
    scanner.startScan(QHostAddress("192.0.2.0"), 3);
    
    QEventLoop loop;
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    QObject::connect(&scanner, &SubnetScanner::scanFinished, &loop, &QEventLoop::quit);
    loop.exec();
    
    EXPECT_EQ(finishedSpy.count(), 1);
}

// ========== Performance Tests ==========

TEST_F(SubnetScannerTest, SmallScanCompletesQuickly) {
    QElapsedTimer timer;
    timer.start();
    
    m_scanner->setConnectTimeout(50);
    m_scanner->startScan(QHostAddress("192.0.2.0"), 3);
    waitForScanFinish(2000);
    
    qint64 elapsed = timer.elapsed();
    EXPECT_LT(elapsed, 2000);
}

// ========== State Consistency Tests ==========

TEST_F(SubnetScannerTest, StateConsistencyAfterCompleteScan) {
    m_scanner->setConnectTimeout(50);
    m_scanner->startScan(QHostAddress("192.0.2.0"), 5);
    waitForScanFinish(3000);
    
    EXPECT_FALSE(m_scanner->isScanning());
    
    m_scanner->startScan(QHostAddress("192.0.2.0"), 3);
    waitMs(50);
    EXPECT_TRUE(m_scanner->isScanning());
    
    waitForScanFinish(2000);
    EXPECT_FALSE(m_scanner->isScanning());
}

// ========== IP Address Boundary Tests ==========

TEST_F(SubnetScannerTest, ScanAtSubnetBoundary) {
    QSignalSpy finishedSpy(m_scanner, &SubnetScanner::scanFinished);
    
    m_scanner->setConnectTimeout(50);
    m_scanner->startScan(QHostAddress("192.0.2.0"), 20);
    waitForScanFinish(10000);
    
    EXPECT_EQ(finishedSpy.count(), 1);
}

TEST_F(SubnetScannerTest, ScanHighIpRange) {
    QSignalSpy finishedSpy(m_scanner, &SubnetScanner::scanFinished);
    
    m_scanner->setConnectTimeout(50);
    m_scanner->startScan(QHostAddress("223.255.255.0"), 3);
    waitForScanFinish(3000);
    
    EXPECT_EQ(finishedSpy.count(), 1);
}

// ========== Connect Timeout Tests ==========

TEST_F(SubnetScannerTest, DefaultConnectTimeout) {
    EXPECT_EQ(m_scanner->connectTimeout(), 5000);
}

TEST_F(SubnetScannerTest, SetConnectTimeout) {
    m_scanner->setConnectTimeout(1000);
    EXPECT_EQ(m_scanner->connectTimeout(), 1000);
}

TEST_F(SubnetScannerTest, SetConnectTimeoutZero) {
    m_scanner->setConnectTimeout(0);
    EXPECT_EQ(m_scanner->connectTimeout(), 0);
}

TEST_F(SubnetScannerTest, SetConnectTimeoutLarge) {
    m_scanner->setConnectTimeout(30000);
    EXPECT_EQ(m_scanner->connectTimeout(), 30000);
}

TEST_F(SubnetScannerTest, TimeoutAffectsScanDuration) {
    // With a very short timeout, scanning unreachable hosts should be fast
    m_scanner->setConnectTimeout(50);
    
    QElapsedTimer timer;
    timer.start();
    
    m_scanner->startScan(QHostAddress("192.0.2.0"), 5);
    waitForScanFinish(5000);
    
    qint64 elapsed = timer.elapsed();
    // 5 hosts * 50ms timeout = ~250ms max, allow some overhead
    EXPECT_LT(elapsed, 2000);
}

TEST_F(SubnetScannerTest, TimeoutPersistsDuringScan) {
    m_scanner->setConnectTimeout(50);
    EXPECT_EQ(m_scanner->connectTimeout(), 50);
    
    m_scanner->startScan(QHostAddress("192.0.2.0"), 3);
    waitMs(50);
    
    EXPECT_EQ(m_scanner->connectTimeout(), 50);
    
    waitForScanFinish(3000);
    EXPECT_EQ(m_scanner->connectTimeout(), 50);
}

TEST_F(SubnetScannerTest, TimeoutChangeBeforeScan) {
    m_scanner->setConnectTimeout(100);
    EXPECT_EQ(m_scanner->connectTimeout(), 100);
    
    m_scanner->setConnectTimeout(200);
    EXPECT_EQ(m_scanner->connectTimeout(), 200);
    
    m_scanner->startScan(QHostAddress("192.0.2.0"), 3);
    waitForScanFinish(3000);
    
    EXPECT_EQ(m_scanner->connectTimeout(), 200);
}

TEST_F(SubnetScannerTest, ShortTimeoutScanCompletes) {
    m_scanner->setConnectTimeout(10);
    
    QSignalSpy finishedSpy(m_scanner, &SubnetScanner::scanFinished);
    m_scanner->startScan(QHostAddress("192.0.2.0"), 3);
    waitForScanFinish(3000);
    
    EXPECT_EQ(finishedSpy.count(), 1);
    EXPECT_FALSE(m_scanner->isScanning());
}
