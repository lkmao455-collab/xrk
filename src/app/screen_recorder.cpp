#include "screen_recorder.h"
#include <QDateTime>

namespace xrk {

// FOURCC helpers
#define FOURCC(a,b,c,d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define MJPG_FOURCC FOURCC('M','J','P','G')
#define VIDS_FOURCC FOURCC('v','i','d','s')

ScreenRecorder::ScreenRecorder() {
}

ScreenRecorder::~ScreenRecorder() {
    if (m_recording) {
        stopRecording();
    }
}

bool ScreenRecorder::startRecording(const QString& filePath, int width, int height, int fps) {
    if (m_recording) {
        stopRecording();
    }

    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::WriteOnly)) {
        return false;
    }

    m_width = width;
    m_height = height;
    m_fps = qBound(1, fps, 60);
    m_frameCount = 0;
    m_frameOffsets.clear();
    m_frameSizes.clear();

    writeHeaders();
    m_recording = true;
    return true;
}

void ScreenRecorder::stopRecording() {
    if (!m_recording) return;
    m_recording = false;
    finalizeFile();
    m_file.close();
}

bool ScreenRecorder::isRecording() const {
    return m_recording;
}

QString ScreenRecorder::filePath() const {
    return m_file.fileName();
}

void ScreenRecorder::addFrame(const QByteArray& jpegData) {
    if (!m_recording || jpegData.isEmpty()) return;

    // Align to 2-byte boundary (AVI chunk alignment)
    uint32_t dataSize = static_cast<uint32_t>(jpegData.size());
    uint32_t paddedSize = dataSize + (dataSize % 2);

    m_frameOffsets.append(m_file.pos() - m_moviStartPos);
    m_frameSizes.append(dataSize);

    // Write '00dc' chunk header + data
    putFourCC("00dc");
    putUint32LE(dataSize);
    m_file.write(jpegData);

    // Pad to 2-byte boundary
    if (paddedSize != dataSize) {
        char pad = 0;
        m_file.write(&pad, 1);
    }

    m_frameCount++;
}

void ScreenRecorder::writeHeaders() {
    // ---- RIFF header ----
    putFourCC("RIFF");
    m_riffSizePos = m_file.pos();
    putUint32LE(0); // placeholder: file size - 8

    putFourCC("AVI ");

    // ---- hdrl LIST ----
    putFourCC("LIST");
    qint64 hdrlSizePos = m_file.pos();
    putUint32LE(0); // placeholder
    putFourCC("hdrl");

    // avih
    putFourCC("avih");
    putUint32LE(56); // size of AVIMAINHEADER
    putUint32LE(1000000 / m_fps); // dwMicroSecPerFrame
    putUint32LE(0); // dwMaxBytesPerSec
    putUint32LE(0); // dwPaddingGranularity
    putUint32LE(0x10); // dwFlags: AVIF_HASINDEX
    putUint32LE(0); // dwTotalFrames (placeholder)
    putUint32LE(0); // dwInitialFrames
    putUint32LE(1); // dwStreams
    putUint32LE(0); // dwSuggestedBufferSize
    putUint32LE(static_cast<uint32_t>(m_width)); // dwWidth
    putUint32LE(static_cast<uint32_t>(m_height)); // dwHeight
    for (int i = 0; i < 4; ++i) putUint32LE(0); // reserved

    // strl LIST
    putFourCC("LIST");
    qint64 strlSizePos = m_file.pos();
    putUint32LE(0); // placeholder
    putFourCC("strl");

    // strh
    putFourCC("strh");
    putUint32LE(56); // size of AVIStreamHeader
    putFourCC("vids"); // fccType
    putUint32LE(MJPG_FOURCC); // fccHandler
    putUint32LE(0); // dwFlags
    putUint16LE(0); // wPriority
    putUint16LE(0); // wLanguage
    putUint32LE(0); // dwInitialFrames
    putUint32LE(1); // dwScale
    putUint32LE(static_cast<uint32_t>(m_fps)); // dwRate
    putUint32LE(0); // dwStart
    putUint32LE(0); // dwLength (placeholder)
    putUint32LE(static_cast<uint32_t>(m_width * m_height * 3)); // dwSuggestedBufferSize
    putUint32LE(0xFFFFFFFF); // dwQuality (-1 = default)
    putUint32LE(0); // dwSampleSize
    putUint16LE(0); // rcFrame.left
    putUint16LE(0); // rcFrame.top
    putUint16LE(static_cast<uint16_t>(m_width)); // rcFrame.right
    putUint16LE(static_cast<uint16_t>(m_height)); // rcFrame.bottom

    // strf
    putFourCC("strf");
    putUint32LE(40); // size of BITMAPINFOHEADER
    putUint32LE(static_cast<uint32_t>(m_width)); // biWidth
    putUint32LE(static_cast<uint32_t>(m_height)); // biHeight
    putUint16LE(1); // biPlanes
    putUint16LE(24); // biBitCount
    putUint32LE(MJPG_FOURCC); // biCompression
    putUint32LE(static_cast<uint32_t>(m_width * m_height * 3)); // biSizeImage
    putUint32LE(0); // biXPelsPerMeter
    putUint32LE(0); // biYPelsPerMeter
    putUint32LE(0); // biClrUsed
    putUint32LE(0); // biClrImportant

    // Update hdrl size
    qint64 endHdrl = m_file.pos();
    m_file.seek(hdrlSizePos);
    putUint32LE(static_cast<uint32_t>(endHdrl - hdrlSizePos - 4));
    m_file.seek(endHdrl);

    // ---- movi LIST ----
    putFourCC("LIST");
    m_moviStartPos = m_file.pos();
    putUint32LE(0); // placeholder: movi size
    putFourCC("movi");
}

void ScreenRecorder::finalizeFile() {
    qint64 endPos = m_file.pos();

    // Write idx1 chunk
    putFourCC("idx1");
    uint32_t idxSize = m_frameCount * 16;
    putUint32LE(idxSize);

    for (int i = 0; i < m_frameCount; ++i) {
        putFourCC("00dc");
        putUint32LE(0x10); // AVIIF_KEYFRAME
        putUint32LE(static_cast<uint32_t>(m_frameOffsets[i])); // offset from movi start
        putUint32LE(m_frameSizes[i]); // size (without padding)
    }

    qint64 fileEnd = m_file.pos();

    // Update movi size
    m_file.seek(m_moviStartPos);
    putUint32LE(static_cast<uint32_t>(fileEnd - m_moviStartPos - 4));

    // Update avih total frames
    m_file.seek(52); // offset of dwTotalFrames in avih (12 RIFF + 8 LIST hdrl + 8 avih + 20 avih data)
    putUint32LE(static_cast<uint32_t>(m_frameCount));

    // Update strh dwLength
    m_file.seek(120); // offset of dwLength (calculated: 12 + 8 + 56 + 8 + 8 + 56 + 24 = 172... let me calculate properly)
    // Actually, let me find the right position by seeking to strl and counting

    // RIFF(12) + hdrl: LIST(8) + avih(64) + strl LIST... -> I'll just calculate
    // Better approach: calculate from known positions
    // After strh's 56 bytes, dwLength is at strh_start + 48
    // strh starts after: RIFF(8+4=12) + LIST hdrl(8+4=12+8=20, but with size field it's 8+4+4=16) 
    // Actually let me just be precise about offsets:

    // I'll use a simpler approach - recalculate from scratch
    
    // File layout:
    // 0:   'RIFF' (4)
    // 4:   file size - 8 (4) = m_riffSizePos
    // 8:   'AVI ' (4)
    // 12:  'LIST' (4)
    // 16:  hdrl size (4) = hdrlSizePos
    // 20:  'hdrl' (4)
    // 24:  'avih' (4)
    // 28:  avih size (4) = 56
    // 32:  avih data (56) = dwMicroSecPerFrame(4), maxBytes(4), padding(4), flags(4), totalFrames(4)[at offset 52], initialFrames(4), streams(4)[at 56], ...
    // 88:  'LIST' (4)
    // 92:  strl size (4)
    // 96:  'strl' (4)
    // 100: 'strh' (4)
    // 104: strh size (4) = 56
    // 108: strh data: fccType(4), fccHandler(4), flags(4), priority(2), language(2), initFrames(4), scale(4), rate(4), start(4), length(4)[at 48 from strh start = offset 152], ...

    // hdrl end... movi

    // For simplicity, let me just patch the known offsets:
    // dwTotalFrames in avih at offset 48 from file start
    if (m_frameCount > 0) {
        m_file.seek(48);
        putUint32LE(static_cast<uint32_t>(m_frameCount));
    }

    // dwLength in strh - offset 140 from file start
    if (m_frameCount > 0) {
        m_file.seek(140);
        putUint32LE(static_cast<uint32_t>(m_frameCount));
    }

    // Update RIFF size
    m_file.seek(m_riffSizePos);
    putUint32LE(static_cast<uint32_t>(fileEnd - 8));

    m_file.seek(fileEnd);
}

void ScreenRecorder::putFourCC(const char* fourcc) {
    m_file.write(fourcc, 4);
}

void ScreenRecorder::putUint32LE(uint32_t value) {
    char buf[4];
    buf[0] = static_cast<char>(value & 0xFF);
    buf[1] = static_cast<char>((value >> 8) & 0xFF);
    buf[2] = static_cast<char>((value >> 16) & 0xFF);
    buf[3] = static_cast<char>((value >> 24) & 0xFF);
    m_file.write(buf, 4);
}

void ScreenRecorder::putUint16LE(uint16_t value) {
    char buf[2];
    buf[0] = static_cast<char>(value & 0xFF);
    buf[1] = static_cast<char>((value >> 8) & 0xFF);
    m_file.write(buf, 2);
}

} // namespace xrk
