#include "video_encoder.h"
#include "jpeg_encoder.h"
#include "core/logger.h"

#ifdef XRK_FFMPEG_AVAILABLE
#include "h264_encoder.h"
#endif

namespace xrk {

std::unique_ptr<VideoEncoder> VideoEncoder::create(EncoderType type) {
    switch (type) {
    case EncoderType::JPEG:
        return std::make_unique<JpegEncoder>();
    case EncoderType::H264:
#ifdef XRK_FFMPEG_AVAILABLE
        return std::make_unique<H264Encoder>();
#else
        LOG_WARNING("H264 encoder not available (FFmpeg not found)");
        return nullptr;
#endif
    }
    return nullptr;
}

} // namespace xrk
