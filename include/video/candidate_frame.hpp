#pragma once

// 候选帧：通过 FrameFilter 筛选后产生的待分析帧。
// 只有候选帧才持有 RGB 数据；普通解码帧分析完立即释放。

#include <cstdint>
#include <vector>

namespace video {

struct CandidateFrame {
    int64_t pts = -1;
    double timestamp = 0.0;
    float change_score = 0.0f;

    int width = 0;
    int height = 0;

    std::vector<uint8_t> rgb;  // RGB24，width*height*3
};

}  // namespace video
