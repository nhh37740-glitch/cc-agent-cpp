#pragma once

// 按 PTS 节奏输出帧，使本地文件表现得像持续摄像头。
// - 以第一次 read() 的墙钟时刻为起点，目标时刻 = start + (pts - 首帧 PTS)，
//   因此非零甚至负的首帧 PTS 不会造成启动空等（R-025）；
// - 等待可通过 std::stop_token 中断：stop 后返回 ReadStatus::Stopped（R-026）；
// - 若解码落后于节奏（目标时刻已过），立即交付不等待，不追赶也不丢弃。

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

#include "video/ffmpeg_video_source.hpp"
#include "video/video_source.hpp"

namespace video {

class RealtimePacingSource : public VideoSource {
public:
    explicit RealtimePacingSource(const char* path)
        : source_(std::make_unique<FFmpegFileSource>(path)) {}

    // 允许包装任意 VideoSource（测试用合成源）
    explicit RealtimePacingSource(std::unique_ptr<VideoSource> source)
        : source_(std::move(source)) {}

    ~RealtimePacingSource() override = default;

    ReadStatus read(Frame& frame) override { return read(frame, std::stop_token()); }

    // 可停止版本：等待期间收到 stop 请求时返回 Stopped
    ReadStatus read(Frame& frame, std::stop_token st);

    bool is_open() const override { return source_->is_open(); }
    const std::string& last_error() const override { return source_->last_error(); }
    double fps() const {
        auto* ff = dynamic_cast<FFmpegFileSource*>(source_.get());
        return ff ? ff->fps() : 30.0;
    }

private:
    std::unique_ptr<VideoSource> source_;
    bool started_ = false;
    bool have_first_ts_ = false;
    double first_ts_ = 0.0;
    std::chrono::steady_clock::time_point start_;
    std::mutex wait_mutex_;
    std::condition_variable_any wait_cv_;
};

}  // namespace video
