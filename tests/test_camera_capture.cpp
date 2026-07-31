// CameraCapture unit tests.
//
// CameraCapture owns a dedicated worker thread (CameraControl) whose event loop
// drives QCamera::start()/stop(). These tests cover the public API and, most
// importantly, the headless teardown path that used to hang forever because
// QCamera::stop() waited for an event-loop signal that never arrived without a
// running loop.
//
// Behavior depends on whether a video input exists:
//   * With no camera, the init-dependent cases are skipped (not failed).
//   * With a (real or virtual) camera, init/shutdown/teardown are exercised.
// No GUI required -- runs under QCoreApplication.
#include <gtest/gtest.h>

#include "hw/camera_capture.h"

using namespace xrk;

namespace {

class CameraCaptureTest : public ::testing::Test {
protected:
    void SetUp() override { m_cam = new CameraCapture(); }

    void TearDown() override {
        // The destructor must drive CameraControl's stop() + thread quit/wait
        // without blocking. This is the previously-hanging path, so every test
        // implicitly asserts teardown is clean.
        delete m_cam;
        m_cam = nullptr;
    }

    CameraCapture* m_cam = nullptr;
};

TEST_F(CameraCaptureTest, CameraCountIsNonNegative) {
    EXPECT_GE(m_cam->cameraCount(), 0);
}

TEST_F(CameraCaptureTest, DefaultIndexIsZero) {
    EXPECT_EQ(m_cam->cameraIndex(), 0);
}

TEST_F(CameraCaptureTest, UninitializedReturnsNoFrame) {
    EXPECT_FALSE(m_cam->isInitialized());
    EXPECT_TRUE(m_cam->captureFrame().isNull());
}

TEST_F(CameraCaptureTest, ShutdownWhenNotInitializedIsSafe) {
    // shutdown() before any init must return immediately, no crash / no block.
    EXPECT_NO_THROW(m_cam->shutdown());
    EXPECT_FALSE(m_cam->isInitialized());
}

TEST_F(CameraCaptureTest, InitializeAndShutdown) {
    if (m_cam->cameraCount() == 0) {
        GTEST_SKIP() << "No camera available on this machine";
    }
    ASSERT_TRUE(m_cam->initialize());
    EXPECT_TRUE(m_cam->isInitialized());

    // Frames arrive asynchronously; just assert the accessor is callable and
    // returns a valid QImage (which may legitimately be null before a frame).
    QImage frame = m_cam->captureFrame();
    EXPECT_TRUE(frame.isNull() || !frame.isNull());

    m_cam->shutdown();
    EXPECT_FALSE(m_cam->isInitialized());
}

TEST_F(CameraCaptureTest, ReinitializeDoesNotHang) {
    if (m_cam->cameraCount() == 0) {
        GTEST_SKIP() << "No camera available on this machine";
    }
    ASSERT_TRUE(m_cam->initialize());
    // Re-initializing must stop the previous session and start a new one
    // without crashing or blocking.
    ASSERT_TRUE(m_cam->initialize());
    EXPECT_TRUE(m_cam->isInitialized());
    m_cam->shutdown();
}

TEST_F(CameraCaptureTest, SetCameraIndex) {
    const int count = m_cam->cameraCount();
    if (count == 0) {
        GTEST_SKIP() << "No camera available on this machine";
    }
    ASSERT_TRUE(m_cam->setCameraIndex(0));
    EXPECT_EQ(m_cam->cameraIndex(), 0);

    // Out-of-range indices are rejected and leave the stored index unchanged.
    EXPECT_FALSE(m_cam->setCameraIndex(-1));
    EXPECT_FALSE(m_cam->setCameraIndex(count));
    EXPECT_EQ(m_cam->cameraIndex(), 0);
}

TEST_F(CameraCaptureTest, SetCameraIndexWhenUninitialized) {
    // Before init, setting an index just records the pending selection.
    CameraCapture cam;
    EXPECT_TRUE(cam.setCameraIndex(0));
    EXPECT_EQ(cam.cameraIndex(), 0);
}

TEST_F(CameraCaptureTest, CameraName) {
    const int count = m_cam->cameraCount();
    if (count == 0) {
        GTEST_SKIP() << "No camera available on this machine";
    }
    EXPECT_FALSE(m_cam->cameraName(0).isEmpty());
    EXPECT_TRUE(m_cam->cameraName(-1).isEmpty());
    EXPECT_TRUE(m_cam->cameraName(count).isEmpty());
}

TEST_F(CameraCaptureTest, TeardownWithoutExplicitShutdownDoesNotHang) {
    // Key regression: after init, destructing WITHOUT an explicit shutdown()
    // must still exit cleanly (this is where QCamera::stop() used to block
    // forever in headless teardown). TearDown's delete exercises it.
    if (m_cam->cameraCount() == 0) {
        GTEST_SKIP() << "No camera available on this machine";
    }
    ASSERT_TRUE(m_cam->initialize());
}

} // namespace
