#pragma once

#include <QByteArray>
#include <QImage>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace xrk {

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    bool initialize();
    void shutdown();

    QImage decode(const QByteArray& data);
    bool isInitialized() const;

private:
    bool m_initialized = false;
    AVCodecContext* m_codecCtx = nullptr;
    SwsContext* m_swsCtx = nullptr;
    AVFrame* m_frame = nullptr;
    AVPacket* m_packet = nullptr;

    int m_width = 0;
    int m_height = 0;
};

} // namespace xrk
