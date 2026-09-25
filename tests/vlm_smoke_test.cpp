// Phase 6 验收：mtmd 单图推理。
// 用真实模型对视频 fixture 的候选帧做实际推理：
// - 加载模型与 mmproj；
// - 取 3 帧，转 RGB，逐张生成描述；
// - 输出非空、连续推理无崩溃；记录每张耗时。

#include <chrono>
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
            const std::string check_message = (msg);                      \
            std::fprintf(stderr, "[FAIL] %s (line %d)\n",                \
                         check_message.c_str(), __LINE__);                \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: vlm_smoke_test <model.gguf> <mmproj.gguf> <video.mp4>\n");
        return 2;
    }

    model::VLMParams p;
    p.model_path = argv[1];
    p.mmproj_path = argv[2];
    p.n_ctx = 2048;
    p.max_tokens = 64;

    const auto load_start = std::chrono::steady_clock::now();
    model::LlamaVLM vlm(p);
    const double load_secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - load_start).count();
    if (!vlm.is_loaded()) {
        std::fprintf(stderr, "[FAIL] 模型加载失败: %s\n", vlm.last_error().c_str());
        return 1;
    }
    std::fprintf(stderr, "model loaded in %.1fs\n", load_secs);

    // 取前 3 帧做推理
    video::FFmpegFileSource source(argv[3]);
    video::Frame f;
    std::vector<uint8_t> rgb;
    int tested = 0;
    for (int i = 0; i < 3; ++i) {
        if (source.read(f) != video::ReadStatus::Frame) break;
        if (!video::convert_to_rgb(f, rgb)) {
            std::fprintf(stderr, "[FAIL] RGB 转换失败\n");
            return 1;
        }

        std::vector<model::Message> messages;
        model::Message sys;
        sys.role = model::Role::System;
        sys.text = "You are a helpful vision assistant. Answer briefly.";
        model::Message user;
        user.role = model::Role::User;
        user.text = "<__media__>\nDescribe this image in one short sentence.";
        user.has_image = true;
        messages.push_back(sys);
        messages.push_back(user);

        const auto t0 = std::chrono::steady_clock::now();
        model::ModelResponse resp =
            vlm.generate(messages, rgb.data(), f.width, f.height);
        const double secs =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        std::fprintf(stderr, "frame[%d] %.1fs tokens=%d: %s\n",
                     i, secs, resp.n_tokens,
                     resp.text.substr(0, 120).c_str());
        CHECK(!resp.text.empty(), "第 " + std::to_string(i) + " 帧应产生非空输出");
        ++tested;
    }
    CHECK(tested == 3, "应完成 3 帧推理");

    if (failures == 0) {
        std::fprintf(stderr, "[PASS] vlm_smoke_test\n");
        return 0;
    }
    return 1;
}
