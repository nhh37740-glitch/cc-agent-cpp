// R-030 验收：sampler 状态不跨生成残留。
// 固定 seed 下，同一输入在"新实例首跑"与"同实例复用多次后的第三次"
// 必须产生相同输出（KV 已清空 + sampler 已重置，二者不应有差别）。

#include <cstdio>
#include <string>
#include <vector>

#include "model/llama_vlm.hpp"
#include "video/ffmpeg_video_source.hpp"
#include "video/yuv_to_rgb.hpp"

static int failures = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "[FAIL] %s (line %d)\n", msg, __LINE__); \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

static model::ModelResponse ask(model::LlamaVLM& vlm, const video::Frame& f,
                                const std::vector<uint8_t>& rgb,
                                const std::string& question) {
    std::vector<model::Message> msgs;
    model::Message sys;
    sys.role = model::Role::System;
    sys.text = "You are a helpful vision assistant. Answer briefly.";
    model::Message user;
    user.role = model::Role::User;
    user.text = "<__media__>\n" + question;
    user.has_image = true;
    msgs.push_back(sys);
    msgs.push_back(user);
    return vlm.generate(msgs, rgb.data(), f.width, f.height);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: determinism_test <model.gguf> <mmproj.gguf> <video.mp4>\n");
        return 2;
    }

    model::VLMParams p;
    p.model_path = argv[1];
    p.mmproj_path = argv[2];
    p.n_ctx = 2048;
    p.max_tokens = 48;
    p.temp = 0.0f;   // 贪心：dist 在 temp=0 下退化为确定选取
    p.seed = 1234;   // 固定 seed

    model::LlamaVLM vlm(p);
    if (!vlm.is_loaded()) {
        std::fprintf(stderr, "[FAIL] 模型加载失败: %s\n", vlm.last_error().c_str());
        return 1;
    }

    // 取一帧固定输入
    video::FFmpegFileSource source(argv[3]);
    video::Frame f;
    CHECK(source.read(f) == video::ReadStatus::Frame, "应能读取 fixture 帧");
    std::vector<uint8_t> rgb;
    CHECK(video::convert_to_rgb(f, rgb, 448), "RGB 转换应成功");

    const char* q1 = "Describe this image in one short sentence.";
    const char* q2 = "List the main colors you see.";

    // 第一次：等价于"新实例首跑"
    const auto first = ask(vlm, f, rgb, q1);
    // 中间插入一次不同生成，制造 sampler/KV 历史差异
    (void)ask(vlm, f, rgb, q2);
    (void)ask(vlm, f, rgb, q2);
    // 第三次：同样的独立输入
    const auto again = ask(vlm, f, rgb, q1);

    std::fprintf(stderr, "first: %s\n", first.text.c_str());
    std::fprintf(stderr, "again: %s\n", again.text.c_str());
    CHECK(!first.text.empty(), "第一次生成应非空");
    CHECK(first.text == again.text,
          "复用实例上重复的独立输入必须产生相同输出（KV+sampler 均已重置）");

    if (failures == 0) {
        std::fprintf(stderr, "[PASS] determinism_test\n");
        return 0;
    }
    return 1;
}
