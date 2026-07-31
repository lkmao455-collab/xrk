#include "jpeg_encoder.h"

namespace xrk {

JpegEncoder::JpegEncoder() {}

JpegEncoder::~JpegEncoder() {
    shutdown();
}

bool JpegEncoder::initialize(int width, int height, int fps) {
    Q_UNUSED(width);
    Q_UNUSED(height);
    Q_UNUSED(fps);
    m_initialized = true;
    return true;
}

void JpegEncoder::shutdown() {
    m_initialized = false;
}

QByteArray JpegEncoder::encode(const QImage& frame) {
    if (frame.isNull()) return QByteArray();

    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    frame.save(&buffer, "JPEG", m_quality);
    buffer.close();

    m_bitrate = data.size() * 8 * 30;
    return data;
}

bool JpegEncoder::isInitialized() const {
    return m_initialized;
}

EncoderType JpegEncoder::type() const {
    return EncoderType::JPEG;
}

int JpegEncoder::bitrate() const {
    return m_bitrate;
}

void JpegEncoder::setBitrate(int bps) {
    Q_UNUSED(bps);
}

int JpegEncoder::quality() const {
    return m_quality;
}

void JpegEncoder::setQuality(int q) {
    m_quality = qBound(1, q, 100);
}

} // namespace xrk
