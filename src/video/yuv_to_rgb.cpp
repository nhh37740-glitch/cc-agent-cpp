// YUV → RGB24 转换实现（libswscale）。仅对候选帧调用。

#include "video/yuv_to_rgb.hpp"

extern "C" {
#include "libswscale/swscale.h"
#include "libavutil/pixfmt.h"
}

namespace video {

bool convert_to_rgb(const Frame& frame, std::vector<uint8_t>& out_rgb) {
    if (!frame.y_plane || frame.width <= 0 || frame.height <= 0 || frame.pix_fmt < 0)
        return false;

    SwsContext* sws = sws_getContext(
        frame.width, frame.height, (AVPixelFormat)frame.pix_fmt,
        frame.width, frame.height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) return false;

    out_rgb.resize((size_t)frame.width * frame.height * 3);
    uint8_t* dst_data[1] = {out_rgb.data()};
    int dst_linesize[1] = {frame.width * 3};

    const uint8_t* src_data[4] = {frame.y_plane, frame.u_plane, frame.v_plane, nullptr};
    const int src_linesize[4] = {frame.y_stride, frame.u_stride, frame.v_stride, 0};

    int ret = sws_scale(sws, src_data, src_linesize, 0, frame.height,
                        dst_data, dst_linesize);
    sws_freeContext(sws);
    return ret == frame.height;
}

}  // namespace video
