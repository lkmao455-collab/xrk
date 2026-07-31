#include <gtest/gtest.h>
#include "app/remote_controller.h"
#include "core/protocol_manager.h"
#include "core/encryption.h"
#include "hw/video_encoder.h"
#include "hw/video_decoder.h"
#include "core/types.h"
#include <QImage>
#include <QPainter>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QThread>

namespace xrk {

// Pick an encoder that actually produces output in this environment. Prefer
// H264 (libx264); fall back to JPEG when libx264 is unavailable (e.g. the
// Media Foundation H264 encoder can't process input here). The encryption /
// decryption bug this test guards is codec-independent, so either is fine.
static std::unique_ptr<VideoEncoder> makeWorkingEncoder() {
    auto h264 = VideoEncoder::create(EncoderType::H264);
    if (h264 && h264->initialize(1920, 1080, 30) &&
        !h264->encode(QImage(64, 64, QImage::Format_RGB32)).isEmpty()) {
        return h264;
    }
    auto jpeg = VideoEncoder::create(EncoderType::JPEG);
    jpeg->initialize(1920, 1080, 30);
    return jpeg;
}

// Regression test for the black remote-desktop bug.
//
// Root cause: the Host ALWAYS encrypts screen frames, but
// RemoteController::handleAuthResponse used a strict ">" check
// (data.size() > 2 + 32 + 16) while the AUTH_RESP payload is exactly 50 bytes
// ("OK" + 32-byte key + 16-byte IV). So m_encryption was never initialized and
// the controller never decrypted the (encrypted) screen frames, which then
// failed to decode and rendered as a black desktop. File transfer kept working
// because file messages are sent without encryption.
//
// This test drives the real controller code path (via its private slots) with a
// host-style AUTH_RESP + encrypted SCREEN_FRAME and asserts the frame the
// controller emits is the decrypted (and decodable) payload.
TEST(ControllerScreen, EncryptedScreenFrameDecrypts) {
    // --- Host side: encrypt a frame the same way Host::EncodeWorker does ---
    Encryption hostEnc;
    ASSERT_TRUE(hostEnc.generateKey());

    auto encoder = makeWorkingEncoder();
    ASSERT_TRUE(encoder->isInitialized());

    QImage img(1920, 1080, QImage::Format_RGB32);
    img.fill(qRgb(10, 120, 240));
    QPainter p(&img);
    p.setPen(Qt::yellow);
    p.drawRect(200, 200, 500, 400);
    p.end();

    QByteArray encoded = encoder->encode(img);
    ASSERT_FALSE(encoded.isEmpty()) << "encoder produced no data";

    ScreenFrame sf;
    sf.data = encoded;
    sf.width = 1920;
    sf.height = 1080;
    sf.format = (encoder->type() == EncoderType::H264) ? FrameFormat::H264 : FrameFormat::JPEG;
    QByteArray sfPayload = ProtocolManager::encodeScreenFrame(sf);

    QByteArray encrypted = hostEnc.encrypt(sfPayload);
    ASSERT_FALSE(encrypted.isEmpty());
    QByteArray screenMsg = ProtocolManager::encode(MessageType::SCREEN_FRAME, encrypted);

    // --- Host side: AUTH_RESP carrying the AES key + IV ---
    QByteArray authPayload = "OK";
    authPayload.append(hostEnc.key());
    authPayload.append(hostEnc.iv());
    QByteArray authMsg = ProtocolManager::encode(MessageType::AUTH_RESP, authPayload);

    // --- Controller side ---
    // NetworkManager / SessionManager are unused by the code path under test.
    RemoteController ctrl(nullptr, nullptr);
    ScreenFrame received;
    bool got = false;
    QObject::connect(&ctrl, &RemoteController::screenFrameReceived,
                     [&](const ScreenFrame& f) { received = f; got = true; });

    // Drive the controller through its real (private) slots synchronously.
    bool authInvoked = QMetaObject::invokeMethod(&ctrl, "onMessageReceived",
                                                  Q_ARG(QByteArray, authMsg));
    ASSERT_TRUE(authInvoked);
    bool frameInvoked = QMetaObject::invokeMethod(&ctrl, "onMessageReceived",
                                                   Q_ARG(QByteArray, screenMsg));
    ASSERT_TRUE(frameInvoked);

    // The decode worker runs on a background thread; wait for it to emit.
    QElapsedTimer timer;
    timer.start();
    while (!got && timer.elapsed() < 2000) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }

    ASSERT_TRUE(got) << "controller never emitted screenFrameReceived";

    // After the fix, the controller decrypts, so the emitted frame's data must
    // equal the original (decrypted) encoded bytes, NOT the ciphertext.
    EXPECT_EQ(received.data, encoded) << "controller did not decrypt the screen frame";

    // And the decrypted payload must actually decode to a non-black image.
    // Mirror the widget's path: H264 goes through VideoDecoder, JPEG through
    // QImage::loadFromData.
    QImage out;
    if (sf.format == FrameFormat::H264) {
        VideoDecoder dec;
        ASSERT_TRUE(dec.initialize());
        out = dec.decode(received.data);
    } else {
        out.loadFromData(received.data, "JPEG");
    }
    ASSERT_FALSE(out.isNull()) << "decrypted frame failed to decode";
    bool nonBlack = false;
    for (int y = 0; y < out.height(); y += 53) {
        for (int x = 0; x < out.width(); x += 53) {
            QRgb px = out.pixel(x, y);
            if (qRed(px) > 8 || qGreen(px) > 8 || qBlue(px) > 8) { nonBlack = true; break; }
        }
        if (nonBlack) break;
    }
    EXPECT_TRUE(nonBlack) << "decoded frame is black";
}

} // namespace xrk
