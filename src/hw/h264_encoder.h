#pragma once

#include "video_encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace xrk {

class H264Encoder : public VideoEncoder {
public:
    H264Encoder();
    ~H264Encoder() override;

    bool initialize(int width, int height, int fps = 30) override;
    void shutdown() override;

    QByteArray encode(const QImage& frame) override;
    bool isInitialized() const override;

    EncoderType type() const override;
    int bitrate() const override;
    void setBitrate(int bps) override;

    void setKeyframeInterval(int interval);
    void setPreset(const char* preset);
    void setTrueColor(bool enable);

private:
    bool m_initialized = false;
    int m_width = 0;
    int m_height = 0;
    int m_fps = 30;
    int m_bitrate = 2000000;
    int m_keyframeInterval = 60;
    bool m_trueColor = false;

    AVCodecContext* m_codecCtx = nullptr;
    SwsContext* m_swsCtx = nullptr;
    AVFrame* m_frame = nullptr;
    AVPacket* m_packet = nullptr;

    bool sendFrame(const QImage& image);
};

} // namespace xrk
