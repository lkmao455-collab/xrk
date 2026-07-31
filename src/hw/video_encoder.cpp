#include "video_encoder.h"
#include "jpeg_encoder.h"
#include "h264_encoder.h"

namespace xrk {

std::unique_ptr<VideoEncoder> VideoEncoder::create(EncoderType type) {
    switch (type) {
    case EncoderType::JPEG:
        return std::make_unique<JpegEncoder>();
    case EncoderType::H264:
        return std::make_unique<H264Encoder>();
    }
    return nullptr;
}

} // namespace xrk
