#pragma once

// 低成本筛帧层：
// 第一层：时间采样（每 sample_interval_ms 才评估一帧）；
// 第二层：把 Y plane 降采样到 small_w x small_h，与上一评估帧做平均绝对差，
//         归一化到 [0,1] 作为 change_score；达到阈值才判定为 Candidate。
// 只使用亮度平面，不做 RGB 转换；RGB 转换发生在候选帧确定之后。

#include <cstdint>
#include <vector>

#include "video/frame.hpp"

namespace filter {

struct FrameFilterConfig {
    int64_t sample_interval_ms = 2000;  // 时间采样间隔
    int small_width = 160;
    int small_height = 90;
    float change_threshold = 0.15f;     // 进入队列的分数阈值
};

struct FilterResult {
    bool should_analyze = false;
    bool was_evaluated = false;  // false 表示被时间门控跳过
    float score = 0.0f;
};

class FrameFilter {
public:
    explicit FrameFilter(const FrameFilterConfig& config = {});

    // 输入一帧（YUV 平面视图），返回筛选结果。内部只保留一份小图状态。
    FilterResult analyze(const video::Frame& frame);

private:
    void downsample_y(const video::Frame& frame);

    FrameFilterConfig cfg_;
    std::vector<uint8_t> small_;      // 当前小图
    std::vector<uint8_t> prev_small_; // 上一次评估的小图
    double last_eval_ts_ = -1e18;     // 上次评估的时间戳
    bool has_prev_ = false;
};

}  // namespace filter
