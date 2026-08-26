#pragma once

// VideoSource 接口：单向顺序读取。实现不得 seek、不得回退。
// 未来可扩展 FFmpegCameraSource / FFmpegRTSPSource。

#include "video/frame.hpp"

namespace video {

class VideoSource {
public:
    virtual ~VideoSource() = default;

    // 读取下一帧；成功返回 true 并填充 frame（内容在下次 read 前有效）。
    // 流结束或发生致命错误返回 false。
    virtual bool read(Frame& frame) = 0;
};

}  // namespace video
