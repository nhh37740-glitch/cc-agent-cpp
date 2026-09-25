// RealtimePacingSource 的实现（见头文件注释）。

#include "video/realtime_pacing_source.hpp"

namespace video {

ReadStatus RealtimePacingSource::read(Frame& frame, std::stop_token st) {
    if (!started_) {
        start_ = std::chrono::steady_clock::now();
        started_ = true;
    }
    const ReadStatus s = source_->read(frame);
    if (s != ReadStatus::Frame) return s;

    if (!have_first_ts_) {
        first_ts_ = frame.timestamp;  // 非零/负首帧 PTS 不产生启动空等
        have_first_ts_ = true;
    }

    if (frame.timestamp >= first_ts_) {
        const auto target =
            start_ + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                         std::chrono::duration<double>(frame.timestamp - first_ts_));
        {
            // 可停止等待：stop 请求立即唤醒；否则等到目标时刻
            std::unique_lock lock(wait_mutex_);
            wait_cv_.wait_until(lock, st, target, [] { return false; });
        }
        if (st.stop_requested()) return ReadStatus::Stopped;
    }
    return ReadStatus::Frame;
}

}  // namespace video
