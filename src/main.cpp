// edge_agent 主程序：完整并行系统入口。
// 用法示例：
//   edge_agent --video <mp4> --model models/x.gguf --mmproj models/mmproj.gguf
// 可选：--skill --log --no-pacing --max-seconds --sample-interval-ms
//       --threshold --queue-capacity --no-vlm

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "agent/builtin_tools.hpp"
#include "app/pipeline.hpp"
#include "model/llama_vlm.hpp"
#include "video/realtime_pacing_source.hpp"

namespace {

struct Args {
    std::string video;
    std::string model;
    std::string mmproj;
    std::string skill_path = "skills/door-camera.md";
    std::string log_path = "logs/events.jsonl";
    bool no_pacing = false;
    bool no_vlm = false;
    double max_seconds = 0.0;
    int64_t sample_interval_ms = 2000;
    float threshold = 0.15f;
    std::size_t queue_capacity = 6;
};

bool parse_args(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : nullptr; };
        if (arg == "--video" && next()) a.video = argv[i];
        else if (arg == "--model" && next()) a.model = argv[i];
        else if (arg == "--mmproj" && next()) a.mmproj = argv[i];
        else if (arg == "--skill" && next()) a.skill_path = argv[i];
        else if (arg == "--log" && next()) a.log_path = argv[i];
        else if (arg == "--no-pacing") a.no_pacing = true;
        else if (arg == "--no-vlm") a.no_vlm = true;
        else if (arg == "--max-seconds" && next()) a.max_seconds = atof(argv[i]);
        else if (arg == "--sample-interval-ms" && next())
            a.sample_interval_ms = atoll(argv[i]);
        else if (arg == "--threshold" && next()) a.threshold = (float)atof(argv[i]);
        else if (arg == "--queue-capacity" && next())
            a.queue_capacity = (std::size_t)atoll(argv[i]);
        else return false;
    }
    return !a.video.empty();
}

void print_usage() {
    std::printf(
        "usage: edge_agent --video <mp4> [--model m.gguf --mmproj mm.gguf]\n"
        "                  [--skill s.md] [--log l.jsonl] [--no-pacing] [--no-vlm]\n"
        "                  [--max-seconds s] [--sample-interval-ms ms] "
        "[--threshold f] [--queue-capacity n]\n");
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        print_usage();
        return 2;
    }

    logging::JsonlLogger logger(args.log_path);
    if (!logger.is_open()) {
        std::fprintf(stderr, "[FATAL] 无法打开日志文件: %s\n", args.log_path.c_str());
        return 1;
    }
    logger.event("startup", {{"video", args.video},
                             {"realtime_pacing", !args.no_pacing},
                             {"vlm", !args.no_vlm}});

    // ---- 配置 ----
    app::PipelineConfig cfg;
    cfg.video_path = args.video;
    cfg.realtime_pacing = !args.no_pacing;
    cfg.max_seconds = args.max_seconds;
    cfg.filter.sample_interval_ms = args.sample_interval_ms;
    cfg.filter.change_threshold = args.threshold;
    cfg.queue_capacity = args.queue_capacity;

    // ---- Skill 与工具（C++ 白名单强制） ----
    skill::Skill sk;
    std::string err;
    if (!args.no_vlm && !skill::load_skill(args.skill_path, sk, err)) {
        std::fprintf(stderr, "[FATAL] %s\n", err.c_str());
        return 1;
    }
    agent::ToolContext tctx;
    tctx.events_log = args.log_path;  // save_event 与运行日志同写一个 JSONL
    agent::ToolRegistry tools = agent::make_default_registry(tctx);

    // ---- 模型加载（在 VLM 线程启动前完成，单线程内安全） ----
    std::unique_ptr<model::LlamaVLM> vlm;
    if (!args.no_vlm) {
        if (args.model.empty() || args.mmproj.empty()) {
            std::fprintf(stderr, "[FATAL] 启用 VLM 时必须提供 --model 与 --mmproj\n");
            return 2;
        }
        model::VLMParams p;
        p.model_path = args.model;
        p.mmproj_path = args.mmproj;
        vlm = std::make_unique<model::LlamaVLM>(p);
        if (!vlm->is_loaded()) {
            std::fprintf(stderr, "[FATAL] %s\n", vlm->last_error().c_str());
            return 1;
        }
    }

    // ---- 队列与两个 Worker ----
    app::PipelineStats stats;
    queue::BoundedQueue<video::CandidateFrame> q(cfg.queue_capacity);

    const auto t0 = std::chrono::steady_clock::now();
    std::jthread video_thread(app::video_worker, std::cref(cfg), std::ref(logger),
                              std::ref(q), std::ref(stats));

    std::jthread vlm_thread;
    if (vlm) {
        app::VlmWorkerParams vp;
        vp.model = vlm.get();
        vp.skill = sk;
        vp.tools = &tools;
        vp.logger = &logger;
        vp.stats = &stats;
        vlm_thread = std::jthread(app::vlm_worker, std::ref(q), vp);
    }

    video_thread.join();
    if (vlm_thread.joinable()) vlm_thread.join();

    const double total_secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    stats.candidates_dropped = q.dropped_full();
    stats.log_summary(logger);

    std::printf("== edge_agent done in %.1fs ==\n", total_secs);
    std::printf("frames_read      : %lld\n", (long long)stats.frames_read.load());
    std::printf("frames_evaluated : %lld\n", (long long)stats.frames_evaluated.load());
    std::printf("candidates_pushed: %lld\n", (long long)stats.candidates_pushed.load());
    std::printf("candidates_dropped: %lld\n", (long long)stats.candidates_dropped.load());
    if (vlm) {
        std::printf("candidates_analyzed: %lld\n",
                    (long long)stats.candidates_analyzed.load());
        std::printf("agent_finals    : %lld\n", (long long)stats.agent_finals.load());
        std::printf("agent_step_limits: %lld\n", (long long)stats.agent_step_limits.load());
        if (stats.vlm_latency_count.load() > 0)
            std::printf("vlm_latency_avg : %.2fs\n",
                        stats.vlm_latency_sum.load() / (double)stats.vlm_latency_count.load());
    }
    std::printf("log              : %s\n", args.log_path.c_str());
    return 0;
}
