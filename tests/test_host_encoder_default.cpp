#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include "host.h"
#include "hw/video_encoder.h"
#include "core/types.h"

namespace xrk {

// Whether a working H264 encoder (libx264, or another encoder that actually
// produces output) is available in this build/environment. Used to make the
// game-gear assertion environment-adaptive.
static bool h264EncoderAvailable() {
    auto enc = VideoEncoder::create(EncoderType::H264);
    if (!enc) return false;
    if (!enc->initialize(1920, 1080, 30)) return false;
    return !enc->encode(QImage(64, 64, QImage::Format_RGB32)).isEmpty();
}

// Wait (polling the Qt event loop) until host.encoderType() equals `want`, or
// the timeout elapses. The encoder swap is asynchronous (EncodeWorker thread).
static bool waitForEncoderType(Host& host, EncoderType want, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (host.encoderType() != want && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    return host.encoderType() == want;
}

// Regression test for the "can control mouse but can't see desktop" bug.
//
// Root cause: the Host used to default to the H264 encoder. When the
// controlling client could not decode the H264 stream, every frame decoded to
// nothing and the desktop stayed black while mouse/keyboard input (unencrypted
// small messages) kept working. The fix defaults the Host to the JPEG encoder
// (decodable by every client, zero keyframe/codec dependencies).
TEST(HostEncoder, DefaultEncoderIsJpeg) {
    Host host;
    host.setAudioEnabled(false);
    ASSERT_TRUE(host.start(19991));

    // No quality gear was requested, so the default must be JPEG.
    EXPECT_EQ(host.encoderType(), EncoderType::JPEG);

    host.stop();
}

// A non-game (standard/quality) gear must keep using JPEG.
TEST(HostEncoder, NonGameGearIsJpeg) {
    Host host;
    host.setAudioEnabled(false);
    ASSERT_TRUE(host.start(19992));

    host.setQualityLevel(QualityLevel::MEDIUM, /*gameMode=*/false);
    EXPECT_TRUE(waitForEncoderType(host, EncoderType::JPEG, 3000))
        << "non-game gear should use the JPEG encoder";

    host.stop();
}

// The game/low-latency gear prefers H264, but if H264 is unavailable on the
// host (no libx264, or Media Foundation failing under STA COM) it must
// transparently fall back to JPEG rather than going black.
TEST(HostEncoder, GameGearUsesH264OrFallsBackToJpeg) {
    Host host;
    host.setAudioEnabled(false);
    ASSERT_TRUE(host.start(19993));

    bool h264 = h264EncoderAvailable();
    EncoderType want = h264 ? EncoderType::H264 : EncoderType::JPEG;

    host.setQualityLevel(QualityLevel::ULTRA, /*gameMode=*/true);
    EXPECT_TRUE(waitForEncoderType(host, want, 5000))
        << "game gear should use H264 when available, else fall back to JPEG";

    host.stop();
}

} // namespace xrk
