#pragma once

#include "video_encoder.h"
#include <QBuffer>
#include <atomic>

namespace xrk {

class JpegEncoder : public VideoEncoder {
public:
    JpegEncoder();
    ~JpegEncoder() override;

    bool initialize(int width, int height, int fps = 30) override;
    void shutdown() override;

    QByteArray encode(const QImage& frame) override;
    bool isInitialized() const override;

    EncoderType type() const override;
    int bitrate() const override;
    void setBitrate(int bps) override;

    int quality() const;
    void setQuality(int q);
    void setJpegQuality(int q) override { setQuality(q); }

private:
    bool m_initialized = false;
    std::atomic<int> m_quality{70};
    int m_bitrate = 0;
};

} // namespace xrk
