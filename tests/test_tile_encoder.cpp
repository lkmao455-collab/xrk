#include <gtest/gtest.h>
#include "hw/tile_encoder.h"
#include "core/protocol_manager.h"
#include "core/types.h"
#include <QImage>
#include <QPainter>
#include <QByteArray>
#include <QCryptographicHash>

namespace xrk {

namespace {

// Returns true if every pixel of `a` exactly equals `b`.
bool imagesIdentical(const QImage& a, const QImage& b) {
    if (a.size() != b.size() || a.format() != b.format()) return false;
    const int h = a.height();
    const int w = a.width();
    for (int y = 0; y < h; ++y) {
        const uchar* pa = a.constScanLine(y);
        const uchar* pb = b.constScanLine(y);
        if (memcmp(pa, pb, static_cast<size_t>(w) * 4) != 0) return false;
    }
    return true;
}

} // namespace

// A single flat colour is classified as RLE and decodes back bit-exactly.
TEST(TileEncoder, FlatColorRoundTripIsLosslessRLE) {
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(qRgb(12, 34, 56));

    TileEncoding enc = TileEncoding::JPEG;
    QByteArray data = TileEncoder::encodeTilePixels(img, 70, &enc);
    ASSERT_FALSE(data.isEmpty());
    EXPECT_EQ(enc, TileEncoding::RLE);

    QImage out = TileEncoder::decodeTilePixels(data, enc, 64, 64);
    ASSERT_FALSE(out.isNull());
    EXPECT_EQ(out.size(), QSize(64, 64));
    EXPECT_TRUE(imagesIdentical(img, out));
}

// A small palette (UI / text) stays lossless via RLE.
TEST(TileEncoder, FewColorRoundTripIsLosslessRLE) {
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setPen(Qt::black);
    for (int i = 0; i < 8; ++i) p.drawLine(0, i * 8, 63, i * 8);
    p.end();

    TileEncoding enc = TileEncoding::JPEG;
    QByteArray data = TileEncoder::encodeTilePixels(img, 70, &enc);
    ASSERT_FALSE(data.isEmpty());
    EXPECT_EQ(enc, TileEncoding::RLE);

    QImage out = TileEncoder::decodeTilePixels(data, enc, 64, 64);
    ASSERT_FALSE(out.isNull());
    EXPECT_TRUE(imagesIdentical(img, out));
}

// A photographic / gradient tile is JPEG: decodes to a valid image of the
// right size (lossy, so we do not require bit-exact equality).
TEST(TileEncoder, PhotographicTileEncodesJpeg) {
    QImage img(64, 64, QImage::Format_RGB32);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            img.setPixel(x, y, qRgb((x * 4) % 256, (y * 4) % 256, ((x + y) * 2) % 256));
        }
    }

    TileEncoding enc = TileEncoding::RLE; // force a wrong guess; must be overwritten
    QByteArray data = TileEncoder::encodeTilePixels(img, 70, &enc);
    ASSERT_FALSE(data.isEmpty());
    EXPECT_EQ(enc, TileEncoding::JPEG);

    QImage out = TileEncoder::decodeTilePixels(data, enc, 64, 64);
    ASSERT_FALSE(out.isNull());
    EXPECT_EQ(out.size(), QSize(64, 64));
}

// First call with no dirty-rect hint emits a full-screen keyframe; every
// tile decodes back to its source pixels (RLE/RAW are lossless).
TEST(TileEncoder, KeyFrameEmitsAllTiles) {
    const int T = 64;
    QImage frame(2 * T, 2 * T, QImage::Format_RGB32);
    frame.fill(Qt::black);
    QPainter p(&frame);
    p.fillRect(0, 0, T, T, Qt::red);
    p.fillRect(T, T, T, T, Qt::blue);
    p.end();

    TileEncoder enc;
    enc.setTileSize(T);
    QList<ScreenTile> tiles = enc.buildTiles(frame, {});
    ASSERT_EQ(tiles.size(), 4); // 2x2 grid, all changed on a keyframe

    for (const ScreenTile& t : tiles) {
        EXPECT_GT(t.data.size(), 0u);
        QImage patch = TileEncoder::decodeTilePixels(t.data, static_cast<TileEncoding>(t.encoding),
                                                     static_cast<int>(t.w), static_cast<int>(t.h));
        ASSERT_FALSE(patch.isNull());
        // Copy the matching quadrant out of the source frame and compare.
        QImage src = frame.copy(static_cast<int>(t.x), static_cast<int>(t.y),
                                static_cast<int>(t.w), static_cast<int>(t.h));
        EXPECT_TRUE(imagesIdentical(src, patch)) << "tile at " << t.x << "," << t.y;
        // Integrity hash must match what the controller will verify.
        EXPECT_EQ(QCryptographicHash::hash(t.data, QCryptographicHash::Md5), t.hash);
    }
}

// After a keyframe, an unchanged frame with no dirty hint produces nothing
// (hash-based skip), so bandwidth stays at zero for static screens.
TEST(TileEncoder, UnchangedFrameSkipsAllTiles) {
    const int T = 64;
    QImage frame(2 * T, 2 * T, QImage::Format_RGB32);
    frame.fill(Qt::darkGray);

    TileEncoder enc;
    enc.setTileSize(T);
    EXPECT_EQ(enc.buildTiles(frame, {}).size(), 4); // keyframe
    EXPECT_EQ(enc.buildTiles(frame, {}).size(), 0); // nothing changed
}

// A dirty rectangle limits emission to the tiles it touches.
TEST(TileEncoder, DirtyRectLimitsToIntersectingTiles) {
    const int T = 64;
    QImage frame(2 * T, 2 * T, QImage::Format_RGB32);
    frame.fill(Qt::black);

    TileEncoder enc;
    enc.setTileSize(T);
    enc.buildTiles(frame, {}); // keyframe establishes baseline hashes

    frame.fill(Qt::green); // change everything...
    QList<QRect> dirty;
    dirty.append(QRect(0, 0, T, T)); // ...but only report the top-left tile
    QList<ScreenTile> tiles = enc.buildTiles(frame, dirty);
    ASSERT_EQ(tiles.size(), 1);
    EXPECT_EQ(tiles.first().x, 0u);
    EXPECT_EQ(tiles.first().y, 0u);
}

// Per-frame budget: a low cap spills remaining tiles into pending and they are
// re-emitted (carry-over) once the budget rises again.
TEST(TileEncoder, BudgetCarryOver) {
    const int T = 64;
    QImage frame(4 * T, 4 * T, QImage::Format_RGB32); // 4x4 = 16 tiles
    frame.fill(Qt::black);

    TileEncoder enc;
    enc.setTileSize(T);
    enc.setMaxTilesPerFrame(4); // cap well below the full keyframe
    enc.requestKeyFrame();

    QList<ScreenTile> first = enc.buildTiles(frame, {});
    EXPECT_EQ(first.size(), 4);
    EXPECT_GT(enc.pendingTileCount(), 0); // rest deferred

    enc.setMaxTilesPerFrame(0); // unlimited -> drain the rest next frame
    QList<ScreenTile> second = enc.buildTiles(frame, {});
    EXPECT_GT(second.size(), 0u);
    EXPECT_EQ(enc.pendingTileCount(), 0);
}

// ScreenTile survives a protocol encode/decode round trip with all fields
// (including the full-frame size the client uses to size its canvas).
TEST(TileEncoder, ScreenTileProtocolRoundTrip) {
    ScreenTile in;
    in.x = 128;
    in.y = 256;
    in.w = 64;
    in.h = 64;
    in.seq = 42;
    in.frameWidth = 1920;
    in.frameHeight = 1080;
    in.encoding = static_cast<uint8_t>(TileEncoding::RLE);
    in.timestamp = 987654321;
    in.data = QByteArrayLiteral("fake-tile-payload");
    in.hash = QCryptographicHash::hash(in.data, QCryptographicHash::Md5);

    QByteArray wire = ProtocolManager::encodeScreenTile(in);
    ASSERT_FALSE(wire.isEmpty());
    ScreenTile out = ProtocolManager::decodeScreenTile(wire);

    EXPECT_EQ(out.x, in.x);
    EXPECT_EQ(out.y, in.y);
    EXPECT_EQ(out.w, in.w);
    EXPECT_EQ(out.h, in.h);
    EXPECT_EQ(out.seq, in.seq);
    EXPECT_EQ(out.frameWidth, in.frameWidth);
    EXPECT_EQ(out.frameHeight, in.frameHeight);
    EXPECT_EQ(out.encoding, in.encoding);
    EXPECT_EQ(out.timestamp, in.timestamp);
    EXPECT_EQ(out.data, in.data);
    EXPECT_EQ(out.hash, in.hash);
}

// A tampered tile fails the MD5 check the controller applies, so it is dropped
// instead of painting corrupt pixels (mirrors RemoteController::applyTile).
TEST(TileEncoder, TamperedTileFailsIntegrity) {
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(Qt::magenta);
    TileEncoding enc = TileEncoding::RLE;
    QByteArray data = TileEncoder::encodeTilePixels(img, 70, &enc);
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Md5);

    EXPECT_EQ(QCryptographicHash::hash(data, QCryptographicHash::Md5), hash);
    data[0] ^= 0xFF; // corrupt one byte
    EXPECT_NE(QCryptographicHash::hash(data, QCryptographicHash::Md5), hash);
}

} // namespace xrk
