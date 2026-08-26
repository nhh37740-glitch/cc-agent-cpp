#pragma once

// YUV → RGB24 转换。约束：只对被选为 Candidate 的帧调用，
// 普通筛选路径不发生 RGB 转换。

#include <cstdint>
#include <vector>

#include "video/frame.hpp"

namespace video {

// 把 frame 的画面转换为连续 RGB24（每像素 3 字节）。
// target_width>0 时按比例缩放到该宽度（用于候选帧送入 VLM 前的降采样，
// 控制视觉 token 数与队列内存）。out_w/out_h 非空时返回实际输出尺寸。
bool convert_to_rgb(const Frame& frame, std::vector<uint8_t>& out_rgb,
                    int target_width = 0, int* out_w = nullptr, int* out_h = nullptr);

}  // namespace video
