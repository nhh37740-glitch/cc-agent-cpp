#pragma once

// YUV → RGB24 转换。约束：只对被选为 Candidate 的帧调用，
// 普通筛选路径不发生 RGB 转换。

#include <cstdint>
#include <vector>

#include "video/frame.hpp"

namespace video {

// 把 frame 的画面转换为连续 RGB24（每像素 3 字节，行对齐 = width*3）。
// 成功返回 true；不支持的像素格式或参数非法返回 false。
bool convert_to_rgb(const Frame& frame, std::vector<uint8_t>& out_rgb);

}  // namespace video
