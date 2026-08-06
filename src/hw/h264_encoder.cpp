#include "h264_encoder.h"
#include "core/logger.h"
#include <cstring>

namespace xrk {

H264Encoder::H264Encoder() {}

H264Encoder::~H264Encoder() {
    shutdown();
}

bool H264Encoder::initialize(int width, int height, int fps) {
    if (m_initialized) {
        shutdown();
    }

    m_width = width;
    m_height = height;
    m_fps = fps;

    // Prefer libx264 explicitly. On Windows avcodec_find_encoder(AV_CODEC_ID_H264)
    // often resolves to the Media Foundation encoder (h264_mf), which requires
    // per-sample timestamps and fails at encode time (MF_E_NO_SAMPLE_TIMESTAMP),
    // producing empty output and a black remote desktop. libx264 is the reliable,
    // low-latency choice we ship with.
    // 
    // CRITICAL: On Windows, Media Foundation H.264 encoder (h264_mf) is often
    // selected by default by FFmpeg. It requires COM to be in MTA mode and 
    // proper per-sample timestamps. If COM is initialized in STA mode (common
    // in Qt apps), h264_mf will fail with MF_E_NO_SAMPLE_TIMESTAMP at encode
    // time, producing empty output. This causes "mouse works but no frame" bug.
    // libx264 is the reliable, low-latency choice we ship with - it has no
    // COM dependencies and works in any thread mode.
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        // libx264 is the only encoder we ship that is safe under every COM
        // threading mode. The Media Foundation encoder (h264_mf) requires COM
        // to be in MTA mode and either returns empty frames (MF_E_NO_SAMPLE_
        // TIMESTAMP) or outright crashes inside the MFT when the host runs in
        // STA mode - which is the default for Qt GUI and worker threads. Under
        // repeated Host start/stop (or any STA COM churn) that crash is
        // non-deterministic and can take down the whole process. Refuse h264_mf
        // so the caller falls back to the rock-solid JPEG encoder instead of
        // producing a black desktop or crashing the host. Hardware encoders
        // (h264_nvenc / h264_qsv / h264_amf / ...) are still allowed.
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (codec && std::string(codec->name) == "h264_mf") {
            LOG_ERROR("H264: only the Media Foundation encoder (h264_mf) is available, but it "
                      "crashes under STA COM (the Qt default). Refusing H264; caller should use JPEG.");
            return false;
        }
    }
    if (!codec) {
        LOG_ERROR("H264: No usable H264 encoder found (libx264 missing and no safe alternative)");
        return false;
    }
    LOG_INFO("H264: using encoder '" + QString(codec->name) + "'");

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        LOG_ERROR("H264: Failed to allocate codec context");
        return false;
    }

    m_codecCtx->codec_id = AV_CODEC_ID_H264;
    m_codecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    m_codecCtx->pix_fmt = m_trueColor ? AV_PIX_FMT_YUV444P : AV_PIX_FMT_YUV420P;
    m_codecCtx->width = m_width;
    m_codecCtx->height = m_height;
    m_codecCtx->bit_rate = m_bitrate;
    m_codecCtx->time_base = {1, m_fps};
    m_codecCtx->framerate = {m_fps, 1};
    m_codecCtx->gop_size = m_keyframeInterval;
    m_codecCtx->max_b_frames = 0;

    // Disable frame-based threading. With frame threading x264 buffers several
    // frames before emitting output, so the first encode() calls return empty
    // (bad for a real-time screen stream) and, worse, the encoder holds a
    // reference to m_frame while we overwrite it on the next call (data race).
    // A single thread still uses slice threading and is plenty for one stream.
    m_codecCtx->thread_count = 1;

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "preset", "ultrafast", 0);
    av_dict_set(&opts, "tune", "zerolatency", 0);
    av_dict_set(&opts, "crf", "23", 0);
    if (m_trueColor) {
        av_dict_set(&opts, "profile", "high444", 0);
    }

    int ret = avcodec_open2(m_codecCtx, codec, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LOG_ERROR("H264: Failed to open codec: " + QString::number(ret) + " (" + QString(errbuf) + ")");
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    m_frame = av_frame_alloc();
    if (!m_frame) {
        LOG_ERROR("H264: Failed to allocate frame");
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    m_frame->format = m_codecCtx->pix_fmt;
    m_frame->width = m_width;
    m_frame->height = m_height;

    ret = av_frame_get_buffer(m_frame, 0);
    if (ret < 0) {
        LOG_ERROR("H264: Failed to allocate frame buffer: " + QString::number(ret));
        av_frame_free(&m_frame);
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    m_packet = av_packet_alloc();
    if (!m_packet) {
        LOG_ERROR("H264: Failed to allocate packet");
        av_frame_free(&m_frame);
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    AVPixelFormat dstFmt = m_trueColor ? AV_PIX_FMT_YUV444P : AV_PIX_FMT_YUV420P;
    m_swsCtx = sws_getContext(
        m_width, m_height, AV_PIX_FMT_BGRA,
        m_width, m_height, dstFmt,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!m_swsCtx) {
        LOG_ERROR("H264: Failed to create SwsContext");
        av_packet_free(&m_packet);
        av_frame_free(&m_frame);
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    m_initialized = true;
    LOG_INFO("H264 encoder initialized: " + QString::number(m_width) + "x" + QString::number(m_height) + "@" + QString::number(m_fps) + "fps, bitrate=" + QString::number(m_bitrate) + ", encoder=" + QString(codec->name));
    return true;
}

void H264Encoder::shutdown() {
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

bool H264Encoder::sendFrame(const QImage& image) {
    QImage converted = image.convertToFormat(QImage::Format_RGB32);
    if (converted.width() != m_width || converted.height() != m_height) {
        converted = converted.scaled(m_width, m_height, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

    const uint8_t* srcSlice[1] = { converted.constBits() };
    int srcStride[1] = { static_cast<int>(converted.bytesPerLine()) };

    sws_scale(m_swsCtx, srcSlice, srcStride, 0, m_height, m_frame->data, m_frame->linesize);

    m_frame->pts = m_frame->pts + 1;

    int ret = avcodec_send_frame(m_codecCtx, m_frame);
    if (ret < 0) {
        LOG_ERROR("H264: avcodec_send_frame failed: " + QString::number(ret));
        return false;
    }
    return true;
}

QByteArray H264Encoder::encode(const QImage& frame) {
    if (!m_initialized || frame.isNull()) {
        return QByteArray();
    }

    if (!sendFrame(frame)) {
        // sendFrame logs the specific error
        return QByteArray();
    }

    QByteArray result;
    char errbuf[AV_ERROR_MAX_STRING_SIZE];

    while (true) {
        int ret = avcodec_receive_packet(m_codecCtx, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            // Check for Media Foundation specific error
            // MF_E_NO_SAMPLE_TIMESTAMP (0xC00D36B4) = COM not in MTA mode
            if (ret == -1072873804) { // 0xC00D36B4 as signed int
                LOG_ERROR("H264: CRITICAL - Media Foundation MF_E_NO_SAMPLE_TIMESTAMP (0xC00D36B4). "
                          "COM is in STA mode! This causes 'mouse works but no frame' bug. "
                          "libx264 should be used instead of Media Foundation encoder. "
                          "Check that libx264 is available and preferred over h264_mf.");
            }
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG_ERROR("H264: avcodec_receive_packet failed: " + QString::number(ret) + " (" + QString(errbuf) + ")");
            break;
        }

        result.append(reinterpret_cast<const char*>(m_packet->data), m_packet->size);
        av_packet_unref(m_packet);
    }

    m_bitrate = result.size() * 8 * m_fps;
    return result;
}

bool H264Encoder::isInitialized() const {
    return m_initialized;
}

EncoderType H264Encoder::type() const {
    return EncoderType::H264;
}

int H264Encoder::bitrate() const {
    return m_bitrate;
}

void H264Encoder::setBitrate(int bps) {
    m_bitrate = bps;
    if (m_codecCtx) {
        m_codecCtx->bit_rate = bps;
    }
}

void H264Encoder::setKeyframeInterval(int interval) {
    m_keyframeInterval = interval;
    if (m_codecCtx) {
        m_codecCtx->gop_size = interval;
    }
}

void H264Encoder::setPreset(const char* preset) {
    if (m_codecCtx) {
        av_opt_set(m_codecCtx, "preset", preset, AV_OPT_SEARCH_CHILDREN);
    }
}

void H264Encoder::setTrueColor(bool enable) {
    m_trueColor = enable;
    if (m_initialized) {
        AVPixelFormat fmt = enable ? AV_PIX_FMT_YUV444P : AV_PIX_FMT_YUV420P;
        m_codecCtx->pix_fmt = fmt;
        if (m_swsCtx) {
            sws_freeContext(m_swsCtx);
        }
        m_swsCtx = sws_getContext(
            m_width, m_height, AV_PIX_FMT_BGRA,
            m_width, m_height, fmt,
            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
        );
    }
}

} // namespace xrk
