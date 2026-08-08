#include "tile_encoder.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>

#include <algorithm>
#include <cstring>

namespace xrk {

namespace {

// Number of distinct colours above which a tile is treated as photographic and
// sent lossy. Below it the tile is UI/text and stays lossless.
constexpr int kLosslessColorLimit = 64;
// At or below this many colours the tile is a flat fill / simple shape and
// plain run-length coding beats deflate.
constexpr int kRleColorLimit = 4;

constexpr quint64 kFnvOffset = 1469598103934665603ULL;
constexpr quint64 kFnvPrime = 1099511628211ULL;

// Content hash of a tile-sized region, read straight out of the frame so that
// unchanged tiles cost nothing but this scan (no copy, no encode).
quint64 hashArea(const QImage& image, const QRect& area) {
    quint64 hash = kFnvOffset;
    const int bytes = area.width() * 4;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        const uchar* row = image.constScanLine(y) + area.left() * 4;
        int i = 0;
        for (; i + 8 <= bytes; i += 8) {
            quint64 word;
            std::memcpy(&word, row + i, sizeof(word));
            hash = (hash ^ word) * kFnvPrime;
        }
        for (; i < bytes; ++i) {
            hash = (hash ^ row[i]) * kFnvPrime;
        }
    }
    return hash;
}

// Counts distinct colours, bailing out as soon as `limit` is exceeded so that
// photographic tiles cost only a handful of pixel reads.
int countDistinctColors(const QImage& tile, int limit) {
    constexpr int kSlots = 512;
    QRgb table[kSlots];
    bool used[kSlots] = {};
    int distinct = 0;

    for (int y = 0; y < tile.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(tile.constScanLine(y));
        for (int x = 0; x < tile.width(); ++x) {
            const QRgb px = row[x];
            unsigned slot = (static_cast<unsigned>(px) * 2654435761u) % kSlots;
            while (used[slot] && table[slot] != px) {
                slot = (slot + 1) % kSlots;
            }
            if (!used[slot]) {
                used[slot] = true;
                table[slot] = px;
                if (++distinct > limit) {
                    return distinct;
                }
            }
        }
    }
    return distinct;
}

QByteArray rawPixels(const QImage& tile) {
    QByteArray raw;
    raw.reserve(tile.width() * tile.height() * 4);
    for (int y = 0; y < tile.height(); ++y) {
        raw.append(reinterpret_cast<const char*>(tile.constScanLine(y)), tile.width() * 4);
    }
    return raw;
}

// (runLength:uint32, colour:uint32) pairs over the tile in row-major order.
QByteArray runLengthEncode(const QImage& tile) {
    QByteArray out;
    QDataStream stream(&out, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    quint32 runColor = 0;
    quint32 runLength = 0;
    bool started = false;

    for (int y = 0; y < tile.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(tile.constScanLine(y));
        for (int x = 0; x < tile.width(); ++x) {
            const quint32 px = static_cast<quint32>(row[x]);
            if (started && px == runColor) {
                ++runLength;
                continue;
            }
            if (started) {
                stream << runLength << runColor;
            }
            runColor = px;
            runLength = 1;
            started = true;
        }
    }
    if (started) {
        stream << runLength << runColor;
    }
    return out;
}

} // namespace

TileEncoder::TileEncoder() = default;

void TileEncoder::setTileSize(int size) {
    const int clamped = qBound(16, size, 256);
    if (clamped == m_tileSize) {
        return;
    }
    m_tileSize = clamped;
    m_frameSize = QSize();   // forces a grid rebuild, which forces a keyframe
    reset();
}

void TileEncoder::setQuality(int quality) {
    m_quality = qBound(1, quality, 100);
}

void TileEncoder::setMaxTilesPerFrame(int maxTiles) {
    m_maxTilesPerFrame = qMax(0, maxTiles);
}

void TileEncoder::reset() {
    for (TileState& state : m_tiles) {
        state.valid = false;
    }
    m_carryDirty.clear();
    m_pendingTiles = 0;
    m_forceKeyFrame = true;
}

void TileEncoder::resizeGrid(const QSize& frameSize) {
    if (frameSize == m_frameSize) {
        return;
    }
    m_frameSize = frameSize;
    m_columns = (frameSize.width() + m_tileSize - 1) / m_tileSize;
    m_rows = (frameSize.height() + m_tileSize - 1) / m_tileSize;
    m_tiles.fill(TileState{}, static_cast<qsizetype>(m_columns) * m_rows);
    m_carryDirty.clear();
    m_forceKeyFrame = true;
}

QByteArray TileEncoder::encodeTilePixels(const QImage& tile, int quality, TileEncoding* encodingOut) {
    const int distinct = countDistinctColors(tile, kLosslessColorLimit);

    if (distinct <= kRleColorLimit) {
        if (encodingOut) *encodingOut = TileEncoding::RLE;
        return runLengthEncode(tile);
    }

    if (distinct <= kLosslessColorLimit) {
        // UI / text: deflate the raw pixels so glyph edges stay crisp. Level 1
        // keeps the encoder cheap enough to run on every changed tile at 30fps.
        QByteArray packed = qCompress(rawPixels(tile), 1);
        // Only worth it while it stays clearly smaller than a JPEG would be.
        if (packed.size() <= tile.width() * tile.height()) {
            if (encodingOut) *encodingOut = TileEncoding::RAW;
            return packed;
        }
    }

    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    tile.save(&buffer, "JPEG", quality);
    buffer.close();
    if (encodingOut) *encodingOut = TileEncoding::JPEG;
    return jpeg;
}

QImage TileEncoder::decodeTilePixels(const QByteArray& data, TileEncoding encoding,
                                     int width, int height) {
    if (data.isEmpty() || width <= 0 || height <= 0) {
        return QImage();
    }

    switch (encoding) {
    case TileEncoding::JPEG: {
        QImage image;
        if (!image.loadFromData(data, "JPEG")) {
            return QImage();
        }
        return image;
    }
    case TileEncoding::RAW: {
        const QByteArray raw = qUncompress(data);
        const qsizetype expected = static_cast<qsizetype>(width) * height * 4;
        if (raw.size() != expected) {
            return QImage();
        }
        QImage image(width, height, QImage::Format_RGB32);
        for (int y = 0; y < height; ++y) {
            std::memcpy(image.scanLine(y), raw.constData() + static_cast<qsizetype>(y) * width * 4,
                        static_cast<size_t>(width) * 4);
        }
        return image;
    }
    case TileEncoding::RLE: {
        QImage image(width, height, QImage::Format_RGB32);
        QDataStream stream(data);
        stream.setByteOrder(QDataStream::BigEndian);

        qsizetype written = 0;
        const qsizetype total = static_cast<qsizetype>(width) * height;
        while (written < total && !stream.atEnd()) {
            quint32 runLength = 0;
            quint32 color = 0;
            stream >> runLength >> color;
            if (stream.status() != QDataStream::Ok || runLength == 0) {
                return QImage();
            }
            const qsizetype run = qMin<qsizetype>(runLength, total - written);
            for (qsizetype i = 0; i < run; ++i) {
                const qsizetype pos = written + i;
                reinterpret_cast<QRgb*>(image.scanLine(static_cast<int>(pos / width)))[pos % width] =
                    static_cast<QRgb>(color);
            }
            written += run;
        }
        if (written != total) {
            return QImage();
        }
        return image;
    }
    }
    return QImage();
}

QList<ScreenTile> TileEncoder::buildTiles(const QImage& frame, const QList<QRect>& dirtyRects) {
    QList<ScreenTile> result;
    m_lastFrameBytes = 0;
    m_pendingTiles = 0;

    if (frame.isNull()) {
        return result;
    }

    const QImage source = (frame.format() == QImage::Format_RGB32 ||
                           frame.format() == QImage::Format_ARGB32)
                              ? frame
                              : frame.convertToFormat(QImage::Format_RGB32);

    resizeGrid(source.size());
    if (m_tiles.isEmpty()) {
        return result;
    }

    const bool keyFrame = m_forceKeyFrame;

    // Candidate tiles: everything on a keyframe or when the capture backend
    // gave no dirty hint, otherwise the tiles touched by the dirty rectangles
    // plus whatever a previous frame could not fit into its budget.
    QSet<int> candidates;
    if (keyFrame || dirtyRects.isEmpty()) {
        candidates.reserve(m_tiles.size());
        for (int i = 0; i < m_tiles.size(); ++i) {
            candidates.insert(i);
        }
    } else {
        candidates = m_carryDirty;
        const QRect bounds(QPoint(0, 0), source.size());
        for (const QRect& rect : dirtyRects) {
            const QRect clipped = rect.intersected(bounds);
            if (clipped.isEmpty()) {
                continue;
            }
            const int col0 = clipped.left() / m_tileSize;
            const int col1 = clipped.right() / m_tileSize;
            const int row0 = clipped.top() / m_tileSize;
            const int row1 = clipped.bottom() / m_tileSize;
            for (int row = row0; row <= row1; ++row) {
                for (int col = col0; col <= col1; ++col) {
                    candidates.insert(row * m_columns + col);
                }
            }
        }
    }
    m_carryDirty.clear();

    QList<int> ordered(candidates.begin(), candidates.end());
    std::sort(ordered.begin(), ordered.end());

    const uint64_t timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
    const int budget = m_maxTilesPerFrame > 0 ? m_maxTilesPerFrame : ordered.size();

    for (int index : ordered) {
        const int col = index % m_columns;
        const int row = index / m_columns;
        const QRect area(col * m_tileSize,
                         row * m_tileSize,
                         qMin(m_tileSize, source.width() - col * m_tileSize),
                         qMin(m_tileSize, source.height() - row * m_tileSize));
        if (area.isEmpty()) {
            continue;
        }

        const quint64 pixelHash = hashArea(source, area);

        TileState& state = m_tiles[index];
        // On a keyframe we must force a full repaint: the client asked for a
        // clean reference frame (first connect, or after it detected a dropped /
        // corrupt tile), so every tile is (re)sent regardless of the cached
        // hash. Otherwise a lost tile in an unchanged region would never be
        // repaired, because its hash still matches the cache.
        if (!keyFrame && state.valid && state.hash == pixelHash) {
            continue;   // unchanged, the client's cached tile is still correct
        }

        if (result.size() >= budget) {
            // Out of budget for this frame; keep it dirty for the next one so
            // a truncated update never leaves stale pixels on the client.
            m_carryDirty.insert(index);
            ++m_pendingTiles;
            continue;
        }

        ScreenTile tile;
        tile.x = static_cast<uint32_t>(area.x());
        tile.y = static_cast<uint32_t>(area.y());
        tile.w = static_cast<uint32_t>(area.width());
        tile.h = static_cast<uint32_t>(area.height());
        tile.frameWidth = static_cast<uint32_t>(source.width());
        tile.frameHeight = static_cast<uint32_t>(source.height());
        tile.isKeyFrame = keyFrame ? 1 : 0;
        tile.timestamp = timestamp;

        TileEncoding encoding = TileEncoding::JPEG;
        tile.data = encodeTilePixels(source.copy(area), m_quality, &encoding);
        if (tile.data.isEmpty()) {
            continue;
        }
        tile.encoding = static_cast<uint8_t>(encoding);
        tile.hash = QCryptographicHash::hash(tile.data, QCryptographicHash::Md5);
        tile.seq = ++state.version;

        state.hash = pixelHash;
        state.valid = true;

        m_lastFrameBytes += static_cast<quint64>(tile.data.size());
        result.append(tile);
    }

    // A budget-truncated keyframe is not a keyframe yet: stay in keyframe mode
    // until the whole screen has actually gone out.
    if (keyFrame && m_pendingTiles == 0) {
        m_forceKeyFrame = false;
    }

    return result;
}

} // namespace xrk
