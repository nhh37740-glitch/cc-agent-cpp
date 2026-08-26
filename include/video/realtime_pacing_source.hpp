#pragma once

// 按 PTS 节奏输出帧，使本地文件表现得像持续摄像头。
// 以第一次 read() 的墙钟时刻为起点：第 n 帧在 start + pts 时刻交付。
// 若解码落后于节奏（目标时刻已过），立即交付不等待，不追赶也不丢弃。

#include "video/ffmpeg_video_source.hpp"

#include <chrono>
#include <thread>

namespace video {

class RealtimePacingSource : public VideoSource {
public:
    explicit RealtimePacingSource(const char* path) : source_(path, true) {}

    bool read(Frame& frame) override {
        if (!started_) {
            start_ = std::chrono::steady_clock::now();
            started_ = true;
        }
        if (!source_.read(frame)) return false;

        if (frame.timestamp >= 0.0) {
            const auto target =
                start_ + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                             std::chrono::duration<double>(frame.timestamp));
            std::this_thread::sleep_until(target);
        }
        return true;
    }

    double fps() const { return source_.fps(); }

private:
    FFmpegFileSource source_;
    bool started_ = false;
    std::chrono::steady_clock::time_point start_;
};

}  // namespace video
