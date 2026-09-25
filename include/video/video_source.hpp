#pragma once

// VideoSource 接口：单向顺序读取。实现不得 seek、不得回退。
// 未来可扩展 FFmpegCameraSource / FFmpegRTSPSource。

#include <string>

#include "video/frame.hpp"

namespace video {

enum class ReadStatus {
    Frame,     // 成功取到一帧
    Eof,       // 正常流结束（含 decoder flush 完毕）
    Error,     // 打开失败或读包/解码错误（诊断见 last_error）
    Stopped,   // 等待被 stop_token 取消（pacing 实现；不是 EOF 也不是错误）
};

class VideoSource {
public:
    virtual ~VideoSource() = default;

    // 读取下一帧。返回 Frame 时填充 frame（内容在下次 read 前有效）。
    virtual ReadStatus read(Frame& frame) = 0;

    // 源是否成功打开
    virtual bool is_open() const = 0;

    // 最近一次错误的诊断信息（无错误时为空）
    virtual const std::string& last_error() const = 0;
};

}  // namespace video
