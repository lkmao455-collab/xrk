#pragma once

#include <QByteArray>
#include <QImage>
#include <memory>

namespace xrk {

enum class EncoderType {
    JPEG,
    H264
};

class VideoEncoder {
public:
    virtual ~VideoEncoder() = default;

    virtual bool initialize(int width, int height, int fps = 30) = 0;
    virtual void shutdown() = 0;

    virtual QByteArray encode(const QImage& frame) = 0;
    virtual bool isInitialized() const = 0;

    virtual EncoderType type() const = 0;
    virtual int bitrate() const = 0;
    virtual void setBitrate(int bps) = 0;
    
    virtual void setTrueColor(bool) {}
    virtual void setJpegQuality(int) {}

    static std::unique_ptr<VideoEncoder> create(EncoderType type);
};

} // namespace xrk
