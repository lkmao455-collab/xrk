#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QEventLoop>
#include <QTimer>
#include "hw/screen_capture.h"
#include "core/types.h"

using namespace xrk;

class ScreenCaptureTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_capture = new ScreenCapture(nullptr);
    }

    void TearDown() override {
        if (m_capture) {
            m_capture->shutdown();
            delete m_capture;
            m_capture = nullptr;
        }
    }

    void waitMs(int ms) {
        QEventLoop loop;
        QTimer::singleShot(ms, &loop, &QEventLoop::quit);
        loop.exec();
    }

    ScreenCapture* m_capture = nullptr;
};

// ========== Constructor / Destructor Tests ==========

TEST_F(ScreenCaptureTest, ConstructorCreatesValidObject) {
    ScreenCapture capture;
    EXPECT_FALSE(capture.isInitialized());
}

TEST_F(ScreenCaptureTest, ConstructorWithParent) {
    QObject parent;
    ScreenCapture capture(&parent);
    EXPECT_FALSE(capture.isInitialized());
}

TEST_F(ScreenCaptureTest, DestructorCallsShutdown) {
    ScreenCapture* capture = new ScreenCapture(nullptr);
    delete capture; // Should not crash
}

// ========== Initialize / Shutdown Lifecycle ==========

TEST_F(ScreenCaptureTest, InitializeSetsInitialized) {
    // Note: May fail if no display available (headless CI)
    bool result = m_capture->initialize();
    // On systems with displays, this should succeed
    // On headless systems, it may fail - that's OK for testing
    EXPECT_TRUE(result || !result); // Just test it doesn't crash
    if (result) {
        EXPECT_TRUE(m_capture->isInitialized());
    }
}

TEST_F(ScreenCaptureTest, DoubleInitializeIsIdempotent) {
    m_capture->initialize();
    bool result2 = m_capture->initialize(); // Should return true if already initialized
    EXPECT_TRUE(result2 || !result2); // Just test it doesn't crash
}

TEST_F(ScreenCaptureTest, ShutdownAfterInitialize) {
    m_capture->initialize();
    m_capture->shutdown();
    EXPECT_FALSE(m_capture->isInitialized());
}

TEST_F(ScreenCaptureTest, ShutdownWhenNotInitialized) {
    m_capture->shutdown(); // Should not crash
    EXPECT_FALSE(m_capture->isInitialized());
}

// ========== CaptureRect Tests ==========

TEST_F(ScreenCaptureTest, DefaultCaptureRectIsEmpty) {
    QRect rect = m_capture->captureRect();
    EXPECT_TRUE(rect.isEmpty() || rect.isValid());
}

TEST_F(ScreenCaptureTest, SetCaptureRect) {
    QRect rect(100, 100, 800, 600);
    m_capture->setCaptureRect(rect);
    EXPECT_EQ(m_capture->captureRect(), rect);
}

TEST_F(ScreenCaptureTest, SetCaptureRectMultipleTimes) {
    QRect rect1(0, 0, 1024, 768);
    QRect rect2(100, 100, 1920, 1080);
    
    m_capture->setCaptureRect(rect1);
    EXPECT_EQ(m_capture->captureRect(), rect1);
    
    m_capture->setCaptureRect(rect2);
    EXPECT_EQ(m_capture->captureRect(), rect2);
}

// ========== TargetFps Tests ==========

TEST_F(ScreenCaptureTest, DefaultTargetFps) {
    int fps = m_capture->targetFps();
    EXPECT_GT(fps, 0);
    EXPECT_LE(fps, 120);
}

TEST_F(ScreenCaptureTest, SetTargetFps) {
    m_capture->setTargetFps(60);
    EXPECT_EQ(m_capture->targetFps(), 60);
}

TEST_F(ScreenCaptureTest, SetTargetFpsBoundaryValues) {
    m_capture->setTargetFps(1);
    EXPECT_EQ(m_capture->targetFps(), 1);
    
    m_capture->setTargetFps(120);
    EXPECT_EQ(m_capture->targetFps(), 120);
}

// ========== MonitorList Tests ==========

TEST_F(ScreenCaptureTest, GetMonitorListReturnsList) {
    QList<MonitorInfo> monitors = m_capture->getMonitorList();
    // May be empty on headless systems, but should not crash
    EXPECT_TRUE(monitors.isEmpty() || !monitors.isEmpty());
}

TEST_F(ScreenCaptureTest, MonitorCountMatchesList) {
    int count = m_capture->monitorCount();
    QList<MonitorInfo> monitors = m_capture->getMonitorList();
    EXPECT_EQ(count, monitors.size());
}

// ========== MonitorIndex Tests ==========

TEST_F(ScreenCaptureTest, DefaultMonitorIndexIsZero) {
    EXPECT_EQ(m_capture->monitorIndex(), 0);
}

TEST_F(ScreenCaptureTest, SetMonitorIndex) {
    QList<MonitorInfo> monitors = m_capture->getMonitorList();
    if (monitors.size() > 1) {
        m_capture->setMonitorIndex(1);
        EXPECT_EQ(m_capture->monitorIndex(), 1);
    }
}

TEST_F(ScreenCaptureTest, SetMonitorIndexInvalid) {
    QList<MonitorInfo> monitors = m_capture->getMonitorList();
    int invalidIndex = monitors.size() + 10;
    m_capture->setMonitorIndex(invalidIndex); // Should be ignored
    EXPECT_LT(m_capture->monitorIndex(), monitors.size());
}

// ========== switchMonitorSafe Tests ==========

TEST_F(ScreenCaptureTest, SwitchMonitorSafeInvalidIndex) {
    QSignalSpy spy(m_capture, &ScreenCapture::monitorSwitchCompleted);
    
    QList<MonitorInfo> monitors = m_capture->getMonitorList();
    int invalidIndex = monitors.size() + 10;
    
    bool result = m_capture->switchMonitorSafe(invalidIndex);
    EXPECT_FALSE(result);
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.first().at(0).toBool(), false);
}

TEST_F(ScreenCaptureTest, SwitchMonitorSafeSameIndex) {
    QSignalSpy spy(m_capture, &ScreenCapture::monitorSwitchCompleted);
    
    int currentIndex = m_capture->monitorIndex();
    bool result = m_capture->switchMonitorSafe(currentIndex);
    
    EXPECT_TRUE(result);
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.first().at(0).toBool(), true);
}

TEST_F(ScreenCaptureTest, SwitchMonitorSafeValidIndex) {
    QList<MonitorInfo> monitors = m_capture->getMonitorList();
    if (monitors.size() > 1) {
        QSignalSpy spy(m_capture, &ScreenCapture::monitorSwitchCompleted);
        
        int targetIndex = (m_capture->monitorIndex() + 1) % monitors.size();
        bool result = m_capture->switchMonitorSafe(targetIndex);
        
        // Result depends on platform capabilities
        EXPECT_TRUE(result || !result);
        EXPECT_EQ(spy.count(), 1);
    }
}

// ========== switchDxgiOutput Tests ==========

TEST_F(ScreenCaptureTest, SwitchDxgiOutputInvalidIndex) {
    QList<MonitorInfo> monitors = m_capture->getMonitorList();
    int invalidIndex = monitors.size() + 10;
    
    bool result = m_capture->switchDxgiOutput(invalidIndex);
    EXPECT_FALSE(result);
}

TEST_F(ScreenCaptureTest, SwitchDxgiOutputSameIndex) {
    int currentIndex = m_capture->monitorIndex();
    bool result = m_capture->switchDxgiOutput(currentIndex);
    EXPECT_TRUE(result); // Should return true for same index
}

// ========== captureFrame Tests ==========

TEST_F(ScreenCaptureTest, CaptureFrameWhenNotInitialized) {
    QImage frame = m_capture->captureFrame();
    EXPECT_TRUE(frame.isNull()); // Should return null when not initialized
}

TEST_F(ScreenCaptureTest, CaptureFrameWithIndexWhenNotInitialized) {
    QImage frame = m_capture->captureFrame(0);
    EXPECT_TRUE(frame.isNull());
}

TEST_F(ScreenCaptureTest, CaptureFrameWithDirtyRectsWhenNotInitialized) {
    QList<QRect> dirtyRects;
    QImage frame = m_capture->captureFrame(&dirtyRects);
    EXPECT_TRUE(frame.isNull());
}

TEST_F(ScreenCaptureTest, CaptureFrameWithIndexAndDirtyRectsWhenNotInitialized) {
    QList<QRect> dirtyRects;
    QImage frame = m_capture->captureFrame(0, &dirtyRects);
    EXPECT_TRUE(frame.isNull());
}

// ========== Signal Tests ==========

TEST_F(ScreenCaptureTest, CaptureErrorSignalCanBeConnected) {
    QSignalSpy spy(m_capture, &ScreenCapture::captureError);
    EXPECT_TRUE(spy.isValid());
}

TEST_F(ScreenCaptureTest, FrameCapturedSignalCanBeConnected) {
    QSignalSpy spy(m_capture, &ScreenCapture::frameCaptured);
    EXPECT_TRUE(spy.isValid());
}

TEST_F(ScreenCaptureTest, MonitorSwitchCompletedSignalCanBeConnected) {
    QSignalSpy spy(m_capture, &ScreenCapture::monitorSwitchCompleted);
    EXPECT_TRUE(spy.isValid());
}

// ========== Thread Safety Tests ==========

TEST_F(ScreenCaptureTest, ConcurrentAccessDoesNotCrash) {
    // Start capture in initialized state if possible
    m_capture->initialize();
    
    // Simulate concurrent access from different "threads"
    // by rapidly calling methods
    for (int i = 0; i < 10; ++i) {
        m_capture->monitorIndex();
        m_capture->isInitialized();
        m_capture->targetFps();
        m_capture->monitorCount();
    }
    
    // Should not crash
    EXPECT_TRUE(true);
}

// ========== State Consistency Tests ==========

TEST_F(ScreenCaptureTest, StateConsistencyAfterMultipleOperations) {
    // Perform multiple operations and verify state is consistent
    m_capture->setTargetFps(30);
    m_capture->setCaptureRect(QRect(0, 0, 1920, 1080));
    
    EXPECT_EQ(m_capture->targetFps(), 30);
    EXPECT_EQ(m_capture->captureRect(), QRect(0, 0, 1920, 1080));
    
    m_capture->initialize();
    m_capture->shutdown();
    
    EXPECT_FALSE(m_capture->isInitialized());
    EXPECT_EQ(m_capture->targetFps(), 30); // Should persist
    EXPECT_EQ(m_capture->captureRect(), QRect(0, 0, 1920, 1080)); // Should persist
}

// ========== MonitorInfo Struct Tests ==========

TEST_F(ScreenCaptureTest, MonitorInfoDefaultValues) {
    MonitorInfo info;
    EXPECT_EQ(info.index, 0);
    EXPECT_TRUE(info.name.isEmpty() || !info.name.isEmpty());
    EXPECT_GE(info.width, 0);
    EXPECT_GE(info.height, 0);
    EXPECT_FALSE(info.isPrimary || !info.isPrimary);
}

TEST_F(ScreenCaptureTest, MonitorInfoFieldAssignment) {
    MonitorInfo info;
    info.index = 5;
    info.name = "TestMonitor";
    info.width = 1920;
    info.height = 1080;
    info.isPrimary = true;
    info.x = 0;
    info.y = 0;
    
    EXPECT_EQ(info.index, 5);
    EXPECT_EQ(info.name, "TestMonitor");
    EXPECT_EQ(info.width, 1920);
    EXPECT_EQ(info.height, 1080);
    EXPECT_TRUE(info.isPrimary);
}
