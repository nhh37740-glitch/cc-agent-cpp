#include "filter/frame_filter.hpp"

#include <cmath>

namespace filter {

FrameFilter::FrameFilter(const FrameFilterConfig& config) : cfg_(config) {
    small_.assign(static_cast<size_t>(cfg_.small_width) * cfg_.small_height, 0);
    prev_small_ = small_;
}

void FrameFilter::downsample_y(const video::Frame& frame) {
    const int sw = cfg_.small_width;
    const int sh = cfg_.small_height;
    const int fw = frame.width;
    const int fh = frame.height;
    for (int y = 0; y < sh; ++y) {
        // 源行按比例取整，避免除零
        const int sy = std::min(fh - 1, y * fh / sh);
        const uint8_t* src_row = frame.y_plane + static_cast<size_t>(sy) * frame.y_stride;
        uint8_t* dst_row = small_.data() + static_cast<size_t>(y) * sw;
        for (int x = 0; x < sw; ++x) {
            const int sx = std::min(fw - 1, x * fw / sw);
            dst_row[x] = src_row[sx];
        }
    }
}

FilterResult FrameFilter::analyze(const video::Frame& frame) {
    FilterResult result;

    // 第一层：时间门控
    const double ts_ms = frame.timestamp * 1000.0;
    if (has_prev_ || last_eval_ts_ > -1e17) {
        if (ts_ms - last_eval_ts_ < static_cast<double>(cfg_.sample_interval_ms)) {
            result.was_evaluated = false;
            return result;
        }
    }

    downsample_y(frame);

    float score = 1.0f;  // 第一次评估：视为候选，保证管线启动
    if (has_prev_) {
        // 第二层：平均绝对差（0~255）归一化到 [0,1]
        const size_t n = small_.size();
        uint64_t diff_sum = 0;
        for (size_t i = 0; i < n; ++i) {
            diff_sum += static_cast<uint64_t>(std::abs(small_[i] - prev_small_[i]));
        }
        score = static_cast<float>(static_cast<double>(diff_sum) /
                                   (static_cast<double>(n) * 255.0));
    }

    prev_small_.swap(small_);
    has_prev_ = true;
    last_eval_ts_ = ts_ms;

    result.was_evaluated = true;
    result.score = score;
    result.should_analyze = (score >= cfg_.change_threshold);
    return result;
}

}  // namespace filter
