// Live-network validation of the weak-link tiled transport (Phases C/D/E/F).
//
// This is an *interactive* loopback: it spins up a protocol-correct stub HOST
// that uses the REAL TileEncoder + ProtocolManager (the exact code the
// production Host uses to slice a frame into tiles, encrypt them and stream
// them) and connects the REAL RemoteController client over a real TCP socket on
// 127.0.0.1. The client decrypts, reassembles and composites the tiles exactly
// as it would against a real Host.
//
// Because the capture backend (DXGI/GDI) needs a live desktop session we cannot
// drive it here, so the stub host supplies frames + dirty-rectangles itself.
// That still exercises the whole network path that consumes those rectangles:
//   * capability handshake (controller -> SCREEN_KEYFRAME)
//   * tiled streaming (host -> SCREEN_TILE, encrypted)
//   * ack feedback (controller -> SCREEN_FRAME_ACK, every 1s)
//   * differential encoding: only changed tiles are emitted
//   * loss resilience: dropped tiles are repaired by the next keyframe
// No GUI required — runs under QCoreApplication with QT_QPA_PLATFORM=offscreen.
#include <gtest/gtest.h>

#include "app/remote_controller.h"
#include "app/session_manager.h"
#include "core/network_manager.h"
#include "core/protocol_manager.h"
#include "core/message_codec.h"
#include "core/encryption.h"
#include "hw/tile_encoder.h"
#include "core/types.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QCryptographicHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <algorithm>
#include <functional>

using namespace xrk;

namespace {

bool waitFor(std::function<bool()> predicate, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (!predicate() && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    return predicate();
}

bool imagesIdentical(const QImage& a, const QImage& b) {
    if (a.size() != b.size() || a.format() != b.format()) return false;
    const int h = a.height(), w = a.width();
    for (int y = 0; y < h; ++y) {
        if (memcmp(a.constScanLine(y), b.constScanLine(y),
                   static_cast<size_t>(w) * 4) != 0)
            return false;
    }
    return true;
}

// Like imagesIdentical but tolerant of small per-channel error (JPEG resends are
// lossy, so a tile repaired by a NACK may differ by a few levels from the
// reference even though it is visually the correct patch).
bool imagesClose(const QImage& a, const QImage& b, int tol = 32) {
    if (a.size() != b.size() || a.format() != b.format()) return false;
    const int h = a.height(), w = a.width();
    for (int y = 0; y < h; ++y) {
        const quint8* pa = a.constScanLine(y);
        const quint8* pb = b.constScanLine(y);
        for (int i = 0; i < w * 4; ++i) {
            if (qAbs(static_cast<int>(pa[i]) - static_cast<int>(pb[i])) > tol)
                return false;
        }
    }
    return true;
}

} // namespace

// Protocol-correct host that streams real tiles to a real RemoteController.
class TiledStubHost : public QObject {
    Q_OBJECT
public:
    explicit TiledStubHost(const QString& password, QObject* parent = nullptr)
        : QObject(parent), m_password(password) {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &TiledStubHost::onNewConnection);
        m_encoder = new TileEncoder();
        m_encryption = new Encryption();
        m_encryption->generateKey();
    }
    bool start(quint16 port) { return m_server->listen(QHostAddress::Any, port); }
    int port() const { return m_server->serverPort(); }
    ~TiledStubHost() override {
        m_server->close();
        delete m_encoder;
        delete m_encryption;
    }

    // Push one frame. `forceKeyframe` makes buildTiles emit the whole grid;
    // `drop` simulates a weak link by discarding every 3rd tile in transit.
    int pushFrame(const QImage& frame, const QList<QRect>& dirty,
                  bool forceKeyframe, bool drop) {
        if (!m_sock) return 0;
        m_lastFrame = frame.copy();
        if (forceKeyframe) m_encoder->requestKeyFrame();
        QList<ScreenTile> tiles = m_encoder->buildTiles(frame, dirty);
        // Mirror the production EncodeWorker: stream the tile under the cursor
        // first so the actively-used region repaints ahead of the rest.
        reorderByCursor(tiles);
        int sent = 0, idx = 0;
        for (const ScreenTile& t : tiles) {
            if (drop && (idx % 3 == 0)) { ++idx; continue; } // packet loss
            QByteArray enc = m_encryption->encrypt(ProtocolManager::encodeScreenTile(t));
            m_sock->write(ProtocolManager::encode(MessageType::SCREEN_TILE, enc));
            ++sent;
            ++idx;
        }
        m_sock->flush();
        m_lastSentTileCount = sent;
        return sent;
    }

    // Push one frame but, under a severe bandwidth limit, send ONLY the first
    // (highest-priority) tile and drop the rest. Used to prove the cursor tile
    // is emitted first and reaches the client before sibling changed tiles.
    // Returns the number of tiles sent (1 if a frame was produced).
    int pushCursorPriorityFrame(const QImage& frame, const QList<QRect>& dirty,
                                bool forceKeyframe) {
        if (!m_sock) return 0;
        m_lastFrame = frame.copy();
        if (forceKeyframe) m_encoder->requestKeyFrame();
        QList<ScreenTile> tiles = m_encoder->buildTiles(frame, dirty);
        reorderByCursor(tiles);
        if (tiles.isEmpty()) return 0;
        const ScreenTile& first = tiles.first();
        m_firstSentTile = QPoint(static_cast<int>(first.x), static_cast<int>(first.y));
        QByteArray enc = m_encryption->encrypt(ProtocolManager::encodeScreenTile(first));
        m_sock->write(ProtocolManager::encode(MessageType::SCREEN_TILE, enc));
        m_sock->flush();
        m_lastSentTileCount = 1;
        return 1;
    }

    // Record the cursor position (driven by MOUSE_EVENT from the controller).
    void setMousePos(const QPoint& p) { m_mousePos = p; }
    QPoint mousePos() const { return m_mousePos; }
    QPoint firstSentTile() const { return m_firstSentTile; }

    // Push a single tile from `frame` at pixel (tx,ty) but with its payload
    // corrupted on the wire (data flipped, hash left intact) so the client's
    // MD5 check fails and it issues a NACK for exactly that tile.
    void pushTamperedTile(const QImage& frame, int tx, int ty) {
        m_lastFrame = frame.copy();
        if (!m_sock) return;
        const int size = m_encoder->tileSize();
        const QRect area(tx, ty, qMin(size, frame.width() - tx),
                         qMin(size, frame.height() - ty));
        ScreenTile tile;
        tile.x = static_cast<uint32_t>(tx);
        tile.y = static_cast<uint32_t>(ty);
        tile.w = static_cast<uint32_t>(area.width());
        tile.h = static_cast<uint32_t>(area.height());
        tile.seq = ++m_seq;
        tile.frameWidth = static_cast<uint32_t>(frame.width());
        tile.frameHeight = static_cast<uint32_t>(frame.height());
        tile.encoding = static_cast<uint8_t>(TileEncoding::JPEG);
        tile.timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
        TileEncoding enc = TileEncoding::JPEG;
        tile.data = TileEncoder::encodeTilePixels(frame.copy(area), 80, &enc);
        tile.hash = QCryptographicHash::hash(tile.data, QCryptographicHash::Md5);
        // Corrupt the payload so the client's MD5 verification fails.
        if (!tile.data.isEmpty()) {
            tile.data[tile.data.size() - 1] ^= 0xFF;
        }
        QByteArray encBytes = m_encryption->encrypt(ProtocolManager::encodeScreenTile(tile));
        m_sock->write(ProtocolManager::encode(MessageType::SCREEN_TILE, encBytes));
        m_sock->flush();
    }

    // Re-stream the tiles named in a NACK request (same as EncodeWorker::resendTiles).
    void resendRequestedTiles(const ScreenTileRequest& req) {
        if (!m_sock || m_lastFrame.isNull()) return;
        const int size = m_encoder->tileSize();
        int sent = 0;
        for (const QPoint& p : req.tiles) {
            const int tx = (p.x() / size) * size;
            const int ty = (p.y() / size) * size;
            if (tx >= m_lastFrame.width() || ty >= m_lastFrame.height()) continue;
            const QRect area(tx, ty, qMin(size, m_lastFrame.width() - tx),
                             qMin(size, m_lastFrame.height() - ty));
            ScreenTile tile;
            tile.x = static_cast<uint32_t>(tx);
            tile.y = static_cast<uint32_t>(ty);
            tile.w = static_cast<uint32_t>(area.width());
            tile.h = static_cast<uint32_t>(area.height());
            tile.seq = ++m_seq;
            tile.frameWidth = static_cast<uint32_t>(m_lastFrame.width());
            tile.frameHeight = static_cast<uint32_t>(m_lastFrame.height());
            tile.encoding = static_cast<uint8_t>(TileEncoding::JPEG);
            tile.timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
            TileEncoding enc = TileEncoding::JPEG;
            tile.data = TileEncoder::encodeTilePixels(m_lastFrame.copy(area), 80, &enc);
            tile.encoding = static_cast<uint8_t>(enc);
            tile.hash = QCryptographicHash::hash(tile.data, QCryptographicHash::Md5);
            QByteArray encBytes = m_encryption->encrypt(ProtocolManager::encodeScreenTile(tile));
            m_sock->write(ProtocolManager::encode(MessageType::SCREEN_TILE, encBytes));
            ++sent;
        }
        m_sock->flush();
        m_nackCount += sent;
    }

    bool tileCapable() const { return m_tileCapable; }
    int ackCount() const { return m_ackCount; }
    int lastSentTileCount() const { return m_lastSentTileCount; }
    int nackCount() const { return m_nackCount; }

private:
    // Same cursor-aware reordering the production EncodeWorker applies.
    void reorderByCursor(QList<ScreenTile>& tiles) {
        const QPoint mp = m_mousePos;
        if (mp.isNull() || tiles.size() <= 1) return;
        std::stable_partition(tiles.begin(), tiles.end(),
            [&](const ScreenTile& t) {
                const int px = mp.x(), py = mp.y();
                return px >= static_cast<int>(t.x) && px < static_cast<int>(t.x + t.w) &&
                       py >= static_cast<int>(t.y) && py < static_cast<int>(t.y + t.h);
            });
    }

private slots:
    void onNewConnection() {
        QTcpSocket* sock = m_server->nextPendingConnection();
        if (!sock) return;
        m_sock = sock;
        m_buffers.insert(sock, QByteArray());
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() { onReady(sock); });
        connect(sock, &QTcpSocket::disconnected, this, [sock]() { sock->deleteLater(); });
    }

    void onReady(QTcpSocket* sock) {
        m_buffers[sock].append(sock->readAll());
        QByteArray& buf = m_buffers[sock];
        while (true) {
            MessageHeader header;
            uint32_t sidLen = 0;
            if (!MessageCodec::parseHeader(buf, header, sidLen)) break;
            size_t total = MessageCodec::HEADER_FIXED_SIZE + 4 + sidLen + header.length
                           + MessageCodec::CHECKSUM_SIZE;
            if (static_cast<size_t>(buf.size()) < total) break;
            QByteArray frame = buf.left(static_cast<int>(total));
            buf = buf.mid(static_cast<int>(total));
            MessageType type{};
            QByteArray payload;
            QString sid;
            if (MessageCodec::decode(frame, type, payload, sid)) handle(type, payload);
        }
    }

    void handle(MessageType type, const QByteArray& payload) {
        if (type == MessageType::AUTH_REQ) {
            QByteArray resp("FAILED");
            if (QString::fromUtf8(payload) == m_password) {
                // "OK" + 32-byte AES key + 16-byte IV + 1 pad (51 bytes total so
                // the client actually initializes its encryption context).
                resp = QByteArray("OK") + m_encryption->key() + m_encryption->iv()
                       + QByteArray(1, 'X');
            }
            m_sock->write(ProtocolManager::encode(MessageType::AUTH_RESP, resp));
        } else if (type == MessageType::SCREEN_KEYFRAME) {
            // Controller declares it can consume tiles -> host switches to the
            // tiled transport and re-sends the whole screen.
            m_tileCapable = true;
            m_encoder->requestKeyFrame();
        } else if (type == MessageType::SCREEN_FRAME_ACK) {
            ScreenAck ack = ProtocolManager::decodeScreenAck(payload);
            (void)ack; // in a real host this drives the AIMD tile budget
            ++m_ackCount;
        } else if (type == MessageType::SCREEN_TILE_REQUEST) {
            ScreenTileRequest req = ProtocolManager::decodeScreenTileRequest(payload);
            resendRequestedTiles(req);
        } else if (type == MessageType::MOUSE_EVENT) {
            MouseEvent ev = ProtocolManager::decodeMouseEvent(payload);
            m_mousePos = QPoint(ev.x, ev.y);
        }
    }

private:
    QString m_password;
    QTcpServer* m_server;
    QTcpSocket* m_sock = nullptr;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    TileEncoder* m_encoder;
    Encryption* m_encryption;
    bool m_tileCapable = false;
    int m_ackCount = 0;
    int m_lastSentTileCount = 0;
    int m_nackCount = 0;
    uint32_t m_seq = 0;
    QImage m_lastFrame;
    QPoint m_mousePos;
    QPoint m_firstSentTile;
};

class TiledTransportTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_host = new TiledStubHost(kPassword, nullptr);
        ASSERT_TRUE(m_host->start(0)) << "Stub host failed to listen";
        m_port = m_host->port();
        m_network = new NetworkManager();
        ASSERT_TRUE(m_network->initialize(0)) << "NetworkManager init failed";
        m_session = new SessionManager();
        m_controller = new RemoteController(m_network, m_session);

        QObject::connect(m_controller, &RemoteController::screenImageReceived,
                         m_controller, [this](const QImage& img) { onImage(img); });
    }

    void TearDown() override {
        if (m_controller) m_controller->stopRemote();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        QThread::msleep(50);
        delete m_controller;
        delete m_session;
        m_network->shutdown();
        delete m_network;
        delete m_host;
    }

    void onImage(const QImage& img) {
        QMutexLocker lock(&m_imgMutex);
        m_lastImage = img.copy();
    }

    QImage latestImage() {
        QMutexLocker lock(&m_imgMutex);
        return m_lastImage.copy();
    }

    void connectAndAuth() {
        bool authOk = false;
        QObject::connect(m_controller, &RemoteController::authSuccess, [&]() { authOk = true; });
        ASSERT_TRUE(m_controller->startRemote("127.0.0.1", m_port, kPassword));
        ASSERT_TRUE(waitFor([&]() { return authOk; }, 5000)) << "auth did not succeed";
        // Controller sends SCREEN_KEYFRAME on authSuccess; host must flip to tiled.
        ASSERT_TRUE(waitFor([this]() { return m_host->tileCapable(); }, 5000))
            << "host never received SCREEN_KEYFRAME capability handshake";
    }

    TiledStubHost* m_host = nullptr;
    NetworkManager* m_network = nullptr;
    SessionManager* m_session = nullptr;
    RemoteController* m_controller = nullptr;
    int m_port = 0;

    QMutex m_imgMutex;
    QImage m_lastImage;

    static constexpr char kPassword[] = "tiled123";
};

// 1) Capability handshake: controller declares tile support, host enables tiling.
TEST_F(TiledTransportTest, CapabilityHandshakeEnablesTiling) {
    connectAndAuth();
    EXPECT_TRUE(m_host->tileCapable());
}

// 2) Differential transport: a frame that only changes one tile emits exactly
//    that tile (the dirty-rect output of Phase A is honoured downstream).
TEST_F(TiledTransportTest, OnlyChangedTileIsStreamed) {
    connectAndAuth();

    const int S = 256, T = 64; // 4x4 grid of 64px tiles
    QImage frame(S, S, QImage::Format_RGB32);
    frame.fill(Qt::darkGray);

    // Full keyframe first so the encoder caches the baseline.
    ASSERT_EQ(m_host->pushFrame(frame, {}, true, false), 16);
    ASSERT_TRUE(waitFor([this, &frame]() { return imagesIdentical(latestImage(), frame); }, 5000))
        << "client did not converge to the keyframe";

    // Now change only the top-left tile and report it as dirty.
    QPainter p(&frame);
    p.fillRect(0, 0, T, T, Qt::red);
    p.end();
    int sent = m_host->pushFrame(frame, {QRect(0, 0, T, T)}, false, false);
    EXPECT_EQ(sent, 1) << "differential transport should stream only the changed tile";

    ASSERT_TRUE(waitFor([this, &frame]() { return imagesIdentical(latestImage(), frame); }, 5000))
        << "client did not converge to the incrementally-updated frame";
}

// 3) Loss resilience: dropped tiles are repaired by the next clean keyframe
//    (the simplified, keyframe-based reliable transport of Phase E).
TEST_F(TiledTransportTest, LostTilesRepairedByKeyframe) {
    connectAndAuth();

    const int S = 256;
    QImage frame(S, S, QImage::Format_RGB32);
    frame.fill(Qt::blue);

    // A keyframe with ~1/3 of tiles lost in transit: client gets a partial view.
    m_host->pushFrame(frame, {}, true, true);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 500);

    // A clean keyframe must fully repair the screen.
    m_host->pushFrame(frame, {}, true, false);
    ASSERT_TRUE(waitFor([this, &frame]() { return imagesIdentical(latestImage(), frame); }, 5000))
        << "client did not recover from packet loss after a clean keyframe";

    // The controller's ackTimer fires every 1s; wait for it so we can confirm the
    // SCREEN_FRAME_ACK feedback path (which drives the host's adaptive budget).
    ASSERT_TRUE(waitFor([this]() { return m_host->ackCount() > 0; }, 4000))
        << "controller never sent SCREEN_FRAME_ACK (ack feedback path broken)";
}

// 4) Targeted NACK: a corrupt tile (bad MD5) is dropped by the client, which
//    then asks the host to resend JUST that tile. The host's resend repairs the
//    patch precisely — no full keyframe required (Phase F per-tile repair).
TEST_F(TiledTransportTest, NackRepairsSpecificTile) {
    connectAndAuth();

    const int S = 256, T = 64;
    QImage frame(S, S, QImage::Format_RGB32);
    frame.fill(Qt::darkGray);

    // Baseline keyframe so the encoder caches it and the client converges.
    ASSERT_EQ(m_host->pushFrame(frame, {}, true, false), 16);
    ASSERT_TRUE(waitFor([this, &frame]() { return imagesIdentical(latestImage(), frame); }, 5000));

    // Frame B: only the top-left tile turns red.
    QImage frameB = frame.copy();
    QPainter p(&frameB);
    p.fillRect(0, 0, T, T, Qt::red);
    p.end();

    // Stream B but with the red tile corrupted on the wire -> client drops it.
    m_host->pushTamperedTile(frameB, 0, 0);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 300);
    // Client must have scheduled a NACK (drops the corrupt tile -> pending list).
    ASSERT_TRUE(waitFor([this]() { return m_host->nackCount() > 0; }, 4000))
        << "host never received a SCREEN_TILE_REQUEST for the corrupt tile";

    // The targeted resend must repair just that patch; full frame converges to B
    // (tolerant comparison — the repaired tile is a lossy JPEG re-encode).
    ASSERT_TRUE(waitFor([this, &frameB]() { return imagesClose(latestImage(), frameB); }, 5000))
        << "NACK resend did not repair the corrupt tile";
}

// 5) Cursor priority: the tile under the pointer is streamed first. With a
//    severe bandwidth cap that drops every tile after the first, only the
//    pointer-covered tile reaches the client — proving the host honours the
//    reorder before the wire (TCP order => client receive order).
TEST_F(TiledTransportTest, CursorTileSentFirst) {
    connectAndAuth();

    const int S = 256, T = 64; // 4x4 grid of 64px tiles
    QImage frame(S, S, QImage::Format_RGB32);
    frame.fill(Qt::darkGray);

    // Baseline keyframe so the client canvas is entirely darkGray.
    ASSERT_EQ(m_host->pushFrame(frame, {}, true, false), 16);
    ASSERT_TRUE(waitFor([this, &frame]() { return imagesIdentical(latestImage(), frame); }, 5000));

    // Move the cursor into tile (col=2, row=1) -> pixel (148, 84).
    const QPoint cursor(148, 84);
    const QPoint cursorTile(2 * T, 1 * T); // (128, 64)
    MouseEvent me;
    me.x = cursor.x();
    me.y = cursor.y();
    me.action = MouseAction::MOVE;
    m_controller->sendMouseEvent(me);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    ASSERT_EQ(m_host->mousePos(), cursor) << "host never received the MOUSE_EVENT";

    // Frame B changes TWO tiles: the cursor tile (red) and a far tile (blue).
    QImage frameB = frame.copy();
    {
        QPainter p(&frameB);
        p.fillRect(cursorTile.x(), cursorTile.y(), T, T, Qt::red);
        p.fillRect(0, 0, T, T, Qt::blue);
        p.end();
    }

    // Stream only the first (highest-priority) tile; the rest are dropped.
    int sent = m_host->pushCursorPriorityFrame(
        frameB, {QRect(cursorTile.x(), cursorTile.y(), T, T), QRect(0, 0, T, T)}, true);
    ASSERT_EQ(sent, 1);

    // The host must have chosen the cursor tile as the first (only) one sent.
    ASSERT_EQ(m_host->firstSentTile(), cursorTile)
        << "cursor-covered tile was not streamed first";

    // The client must have received and applied just that one tile: the cursor
    // region is now red, but the blue tile was dropped and is still the darkGray
    // baseline (proving the priority ordering actually reached the client).
    const QRgb baseline = latestImage().pixel(32, 32); // darkGray at the blue tile
    ASSERT_TRUE(waitFor([this, &cursor]() {
        QImage img = latestImage();
        if (img.isNull()) return false;
        const QRgb c = img.pixel(cursor.x(), cursor.y());
        return qRed(c) > 150 && qGreen(c) < 80 && qBlue(c) < 80;
    }, 5000)) << "cursor tile did not reach the client first";

    const QRgb blueTile = latestImage().pixel(32, 32);
    EXPECT_TRUE(qAbs(qRed(blueTile) - qRed(baseline)) < 20 &&
                qAbs(qGreen(blueTile) - qGreen(baseline)) < 20 &&
                qAbs(qBlue(blueTile) - qBlue(baseline)) < 20)
        << "dropped (non-priority) tile should still be the darkGray baseline, "
           "got (" << qRed(blueTile) << "," << qGreen(blueTile) << "," << qBlue(blueTile)
           << ") expected ~(" << qRed(baseline) << "," << qGreen(baseline) << "," << qBlue(baseline) << ")";
}

#include "test_tiled_transport.moc"
