#pragma once

// 视频帧视图：指向 VideoSource 内部解码缓冲，仅在下次 read() 前有效。
// 不拥有内存；候选帧需要的数据在 FrameFilter 判定后另行拷贝。

#include <cstdint>

namespace video {

struct Frame {
    int64_t pts = -1;        // 原始 PTS（流 time_base 下的整数）
    double timestamp = 0.0;  // 秒
    int width = 0;
    int height = 0;

    // YUV 平面指针与行距（指向内部缓冲，勿释放）
    const uint8_t* y_plane = nullptr;
    int y_stride = 0;
    const uint8_t* u_plane = nullptr;
    int u_stride = 0;
    const uint8_t* v_plane = nullptr;
    int v_stride = 0;
};

}  // namespace video
