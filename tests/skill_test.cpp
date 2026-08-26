// Phase 7 验收：Skill 加载与行为切换。
// - 从文件加载两个不同 Skill，C++ 代码不变；
// - 同一帧画面 + 不同 Skill，模型输出应不同（行为受 Skill 影响）；
// - 两种情况下都遵守 JSON 输出协议（type 为 tool_call 或 final）。

#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "agent/structured_output.hpp"
#include "model/llama_vlm.hpp"
#include "skill/skill_loader.hpp"
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

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
                     "usage: skill_test <model> <mmproj> <video.mp4> "
                     "<skill_a.md> <skill_b.md>\n");
        return 2;
    }

    skill::Skill a, b;
    std::string err;
    CHECK(skill::load_skill(argv[4], a, err), ("加载 A 失败: " + err).c_str());
    CHECK(skill::load_skill(argv[5], b, err), ("加载 B 失败: " + err).c_str());
    if (failures) return 1;

    // 系统提示包含工具与协议
    const std::vector<std::string> tools = {"notify(text)", "save_event(event, description)",
                                            "get_time()", "speak(text)"};
    const std::string sys_a = skill::build_system_prompt(a, tools);
    const std::string sys_b = skill::build_system_prompt(b, tools);
    CHECK(sys_a.find("tool_call") != std::string::npos, "系统提示应含输出协议");
    CHECK(sys_b.find("pet") != std::string::npos || sys_b.find("Pet") != std::string::npos,
          "Skill B 内容应进入系统提示");

    model::VLMParams p;
    p.model_path = argv[1];
    p.mmproj_path = argv[2];
    p.n_ctx = 2048;
    p.max_tokens = 96;
    model::LlamaVLM vlm(p);
    if (!vlm.is_loaded()) {
        std::fprintf(stderr, "[FAIL] 模型加载失败\n");
        return 1;
    }

    video::FFmpegFileSource source(argv[3]);
    video::Frame f;
    if (!source.read(f)) return 1;  // 取第一帧
    std::vector<uint8_t> rgb;
    if (!video::convert_to_rgb(f, rgb)) return 1;

    auto run_with_skill = [&](const std::string& sys) {
        std::vector<model::Message> msgs;
        model::Message s;
        s.role = model::Role::System;
        s.text = sys;
        model::Message u;
        u.role = model::Role::User;
        u.text = "<__media__>\nAnalyze this frame according to your task.";
        u.has_image = true;
        msgs.push_back(s);
        msgs.push_back(u);
        return vlm.generate(msgs, rgb.data(), f.width, f.height);
    };

    // 协议合规：剥离包装后可解析为 JSON 且 type 合法；小模型不稳定，最多重试 2 次
    auto is_valid_protocol = [](const std::string& text) {
        return agent::parse_model_output(text).type != agent::OutputType::Invalid;
    };
    auto run_valid = [&](const std::string& sys) {
        auto r = run_with_skill(sys);
        for (int attempt = 0; attempt < 2 && !is_valid_protocol(r.text); ++attempt) {
            std::fprintf(stderr, "retry (attempt %d): %s\n", attempt + 1,
                         r.text.substr(0, 80).c_str());
            r = run_with_skill(sys);
        }
        return r;
    };

    const auto ra = run_valid(sys_a);
    const auto rb = run_valid(sys_b);
    std::fprintf(stderr, "skill_a -> %s\n", ra.text.substr(0, 150).c_str());
    std::fprintf(stderr, "skill_b -> %s\n", rb.text.substr(0, 150).c_str());

    CHECK(is_valid_protocol(ra.text), "Skill A 下应输出合法协议 JSON");
    CHECK(is_valid_protocol(rb.text), "Skill B 下应输出合法协议 JSON");
    CHECK(ra.text != rb.text, "不同 Skill 应导致不同输出");

    if (failures == 0) {
        std::fprintf(stderr, "[PASS] skill_test\n");
        return 0;
    }
    return 1;
}
