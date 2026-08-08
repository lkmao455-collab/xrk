#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QVector>

#include "core/types.h"

namespace xrk {

// Turns a full desktop frame into the minimal set of changed tiles.
//
// The host keeps one TileEncoder per streaming session. For every captured
// frame it feeds in the image plus the dirty rectangles reported by the
// capture backend (empty = no hint, hash everything). Tiles whose content is
// unchanged since the version the client last confirmed are skipped entirely,
// which is what makes the stream survive a weak link: only the moving parts of
// the screen consume bandwidth.
//
// Each tile is encoded independently and classified by content:
//   * single flat colour        -> RAW (4 bytes)
//   * few colours (UI / text)   -> RLE, lossless, keeps text crisp
//   * photographic / video      -> JPEG at the adaptive quality
class TileEncoder {
public:
    TileEncoder();

    void setTileSize(int size);
    int tileSize() const { return m_tileSize; }

    // Lossy quality used for photographic tiles (1..100).
    void setQuality(int quality);
    int quality() const { return m_quality; }

    // Upper bound on how many tiles a single frame may emit. Under congestion
    // the host lowers this so one busy frame cannot monopolise the link; the
    // tiles that did not fit stay dirty and go out on a later frame.
    void setMaxTilesPerFrame(int maxTiles);
    int maxTilesPerFrame() const { return m_maxTilesPerFrame; }

    // Drops all cached tile hashes, so the next call re-sends the whole
    // screen as a keyframe. Call on client (re)connect or after a decode error.
    void reset();

    // True when the next buildTiles() will produce a full-screen keyframe.
    bool keyFramePending() const { return m_forceKeyFrame; }
    void requestKeyFrame() { m_forceKeyFrame = true; }

    // Builds the changed tiles for this frame. Returns an empty list when
    // nothing changed (the caller should then send nothing at all).
    QList<ScreenTile> buildTiles(const QImage& frame, const QList<QRect>& dirtyRects);

    // Tiles that were skipped because of the per-frame budget.
    int pendingTileCount() const { return m_pendingTiles; }

    // Statistics for the adaptive loop.
    quint64 lastFrameBytes() const { return m_lastFrameBytes; }

    // Exposed for tests and for the adaptive loop's bandwidth estimate.
    static QByteArray encodeTilePixels(const QImage& tile, int quality, TileEncoding* encodingOut);

    // Client side: turns a received tile back into pixels. Returns a null
    // image when the payload is corrupt or the encoding is unknown.
    static QImage decodeTilePixels(const QByteArray& data, TileEncoding encoding, int width, int height);

private:
    struct TileState {
        quint64 hash = 0;       // fast hash of the raw pixels, for change detection
        uint32_t version = 0;
        bool valid = false;
    };

    void resizeGrid(const QSize& frameSize);

    int m_tileSize = TILE_SIZE;
    int m_quality = 70;
    int m_maxTilesPerFrame = 0;   // 0 = unlimited
    bool m_forceKeyFrame = true;
    int m_pendingTiles = 0;
    quint64 m_lastFrameBytes = 0;

    QSize m_frameSize;
    int m_columns = 0;
    int m_rows = 0;
    QVector<TileState> m_tiles;
    // Tiles that were dropped by the per-frame budget. They are re-examined on
    // the next frame even if the capture backend no longer reports them dirty,
    // otherwise a truncated update would leave the client showing stale pixels.
    QSet<int> m_carryDirty;
};

} // namespace xrk
