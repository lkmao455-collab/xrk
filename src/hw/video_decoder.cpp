#include "video_decoder.h"
#include "core/logger.h"
#include <cstring>

namespace xrk {

VideoDecoder::VideoDecoder() {}

VideoDecoder::~VideoDecoder() {
    shutdown();
}

bool VideoDecoder::initialize() {
    if (m_initialized) {
        shutdown();
    }

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        LOG_ERROR("H264 decoder not found - libavcodec may not have H264 decoder support");
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        LOG_ERROR("Failed to allocate decoder context");
        return false;
    }

    m_codecCtx->thread_count = 1;
    m_codecCtx->thread_type = FF_THREAD_SLICE;

    int ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LOG_ERROR("Failed to open H264 decoder: " + QString::number(ret) + " (" + QString(errbuf) + ")");
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    m_frame = av_frame_alloc();
    m_packet = av_packet_alloc();

    if (!m_frame || !m_packet) {
        LOG_ERROR("Failed to allocate decoder frame/packet");
        av_frame_free(&m_frame);
        av_packet_free(&m_packet);
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    m_initialized = true;
    LOG_INFO("H264 decoder initialized (codec: " + QString(codec->name) + ")");
    return true;
}

void VideoDecoder::shutdown() {
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    if (m_packet) {
        av_packet_free(&m_packet);
    }
    if (m_frame) {
        av_frame_free(&m_frame);
    }
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
    }
    m_initialized = false;
}

QImage VideoDecoder::decode(const QByteArray& data) {
    if (!m_initialized || data.isEmpty()) {
        return QImage();
    }

    m_packet->data = reinterpret_cast<uint8_t*>(const_cast<char*>(data.constData()));
    m_packet->size = data.size();

    int ret = avcodec_send_packet(m_codecCtx, m_packet);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LOG_WARNING("VideoDecoder: avcodec_send_packet failed: " + QString::number(ret) + " (" + QString(errbuf) + ")");
        return QImage();
    }

    QImage result;
    while (true) {
        int rcv = avcodec_receive_frame(m_codecCtx, m_frame);
        if (rcv == AVERROR(EAGAIN) || rcv == AVERROR_EOF) {
            break;
        }
        if (rcv < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(rcv, errbuf, sizeof(errbuf));
            LOG_WARNING("VideoDecoder: avcodec_receive_frame failed: " + QString::number(rcv) + " (" + QString(errbuf) + ")");
            break;
        }

        int width = m_frame->width;
        int height = m_frame->height;

        if (width <= 0 || height <= 0) {
            LOG_WARNING("VideoDecoder: invalid frame dimensions " + QString::number(width) + "x" + QString::number(height));
            continue;
        }

        if (m_width != width || m_height != height) {
            m_width = width;
            m_height = height;
            if (m_swsCtx) {
                sws_freeContext(m_swsCtx);
                m_swsCtx = nullptr;
            }
        }

        if (!m_swsCtx) {
            m_swsCtx = sws_getContext(
                width, height, static_cast<AVPixelFormat>(m_frame->format),
                width, height, AV_PIX_FMT_BGRA,
                SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
            );
            if (!m_swsCtx) {
                LOG_ERROR("VideoDecoder: failed to create SwsContext for " + QString::number(width) + "x" + QString::number(height));
                return QImage();
            }
        }

        QImage image(width, height, QImage::Format_RGB32);
        uint8_t* dstSlice[1] = { image.bits() };
        int dstStride[1] = { static_cast<int>(image.bytesPerLine()) };

        sws_scale(m_swsCtx, m_frame->data, m_frame->linesize, 0, height, dstSlice, dstStride);

        result = image;

        // Only return the first complete frame (real-time streaming)
        break;
    }

return result;
    }

    bool VideoDecoder::isInitialized() const {
    return m_initialized;
}

} // namespace xrk
