// edge_agent 主程序：边缘端视频感知入口。
// 用法：edge_agent --video <mp4> [--model vision.gguf --mmproj mm.gguf] [其他参数]
// 退出码：0 成功；1 运行失败（视频/模型/日志错误）；2 参数错误。

#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <cmath>
#include <memory>
#include <string>
#include <thread>

#include "app/pipeline.hpp"
#include "model/llama_vlm.hpp"
#include "web/dashboard.hpp"

namespace {

struct Args {
    std::string video;
    std::string model;
    std::string mmproj;
    std::string log_path = "logs/events.jsonl";
    bool no_pacing = false;
    bool no_vlm = false;
    double max_seconds = 0.0;
    int64_t sample_interval_ms = 2000;
    float threshold = 0.15f;
    int64_t queue_capacity = 6;
    int analysis_width = 448;
    std::string web_bind = "0.0.0.0";
    int64_t web_port = 0;
    int64_t web_hold_seconds = 0;  // -1 表示完成后持续提供面板，直到进程被停止
    int64_t web_max_events = 20;
    bool unattended = false;
    bool web_idle = false;
};

void print_usage() {
    std::printf(
        "usage: edge_agent --video <mp4> [--model vision.gguf --mmproj mm.gguf]\n"
        "       edge_agent --web-idle --web-port <n> [--web-bind ip]\n"
        "                  [--log l.jsonl] [--no-pacing] [--no-vlm]\n"
        "                  [--max-seconds s] [--sample-interval-ms ms]\n"
        "                  [--threshold f] [--queue-capacity n] [--analysis-width px]\n"
        "                  [--web-port n] [--web-bind ip] [--web-hold-seconds n] [--unattended]\n"
        "\n"
        "options:\n"
        "  --video <mp4>              输入视频（模拟实时摄像头），必填\n"
        "  --model <gguf>             InternVL3 视觉模型权重（启用 VLM 时必填）\n"
        "  --mmproj <gguf>            视觉投影器权重（启用 VLM 时必填）\n"
        "  --log <jsonl>              JSONL 日志路径（默认 logs/events.jsonl）\n"
        "  --no-pacing                不按 PTS 节奏，尽快解码\n"
        "  --no-vlm                   只跑视频管线，不加载模型\n"
        "  --max-seconds <s>          只读取前 N 秒视频时间（0=到 EOF）\n"
        "  --sample-interval-ms <ms>  筛帧时间采样间隔（默认 2000）\n"
        "  --threshold <f>            变化分数阈值（默认 0.15）\n"
        "  --queue-capacity <n>       候选队列容量，必须为 4~8（默认 6）\n"
        "  --analysis-width <px>      候选帧送入 VLM 前的降采样宽度（默认 448）\n"
        "  --web-port <n>             启动远程只读面板（如 8080；默认关闭）\n"
        "  --web-bind <ip>            面板监听地址（默认 0.0.0.0，供局域网访问）\n"
        "  --web-hold-seconds <n>     视频结束后保留面板 N 秒；-1 一直保留\n"
        "  --web-max-events <n>       面板保留的候选事件数（1~100，默认 20）\n"
        "  --web-idle                 仅提供空面板并显示等待配置；不读取视频或加载模型\n"
        "  --unattended               无人值守：等价于 --web-hold-seconds -1\n");
}

// 严格整数解析：拒绝空串、尾随字符、溢出
bool parse_int64(const char* s, int64_t& out) {
    if (!s || !*s) return false;
    errno = 0;
    char* end = nullptr;
    const long long v = std::strtoll(s, &end, 10);
    if (errno == ERANGE || end == s || *end != '\0') return false;
    out = v;
    return true;
}

bool parse_double(const char* s, double& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    const double v = std::strtod(s, &end);
    if (errno == ERANGE || end == s || *end != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

// 解析并完整校验；失败时向 stderr 写明原因
bool parse_args(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : nullptr; };
        auto need_value = [&](const char* name) -> const char* {
            const char* v = next();
            if (!v) std::fprintf(stderr, "[ARG ERROR] %s 缺少参数值\n", name);
            return v;
        };
        if (arg == "--video") { const char* v = need_value("--video"); if (!v) return false; a.video = v; }
        else if (arg == "--model") { const char* v = need_value("--model"); if (!v) return false; a.model = v; }
        else if (arg == "--mmproj") { const char* v = need_value("--mmproj"); if (!v) return false; a.mmproj = v; }
        else if (arg == "--log") { const char* v = need_value("--log"); if (!v) return false; a.log_path = v; }
        else if (arg == "--no-pacing") a.no_pacing = true;
        else if (arg == "--no-vlm") a.no_vlm = true;
        else if (arg == "--web-idle") a.web_idle = true;
        else if (arg == "--unattended") a.unattended = true;
        else if (arg == "--max-seconds") {
            const char* v = need_value("--max-seconds"); if (!v) return false;
            if (!parse_double(v, a.max_seconds)) {
                std::fprintf(stderr, "[ARG ERROR] --max-seconds 不是合法数字: %s\n", v);
                return false;
            }
            if (a.max_seconds < 0) {
                std::fprintf(stderr, "[ARG ERROR] --max-seconds 不能为负\n");
                return false;
            }
        }
        else if (arg == "--sample-interval-ms") {
            const char* v = need_value("--sample-interval-ms"); if (!v) return false;
            if (!parse_int64(v, a.sample_interval_ms) || a.sample_interval_ms <= 0) {
                std::fprintf(stderr, "[ARG ERROR] --sample-interval-ms 必须为正整数: %s\n", v);
                return false;
            }
        }
        else if (arg == "--threshold") {
            const char* v = need_value("--threshold"); if (!v) return false;
            double t = 0.0;
            if (!parse_double(v, t)) {
                std::fprintf(stderr, "[ARG ERROR] --threshold 不是合法数字: %s\n", v);
                return false;
            }
            a.threshold = (float)t;
        }
        else if (arg == "--queue-capacity") {
            const char* v = need_value("--queue-capacity"); if (!v) return false;
            // 先按可检查的整数解析（拒绝负数/溢出/尾随字符），再做范围校验
            if (!parse_int64(v, a.queue_capacity)) {
                std::fprintf(stderr, "[ARG ERROR] --queue-capacity 不是合法整数: %s\n", v);
                return false;
            }
        }
        else if (arg == "--analysis-width") {
            const char* v = need_value("--analysis-width"); if (!v) return false;
            int64_t w = 0;
            if (!parse_int64(v, w) || w < 32 || w > 4096) {
                std::fprintf(stderr, "[ARG ERROR] --analysis-width 必须在 [32,4096]: %s\n", v);
                return false;
            }
            a.analysis_width = (int)w;
        }
        else if (arg == "--web-bind") {
            const char* v = need_value("--web-bind"); if (!v) return false;
            a.web_bind = v;
        }
        else if (arg == "--web-port") {
            const char* v = need_value("--web-port"); if (!v) return false;
            if (!parse_int64(v, a.web_port) || a.web_port < 1 || a.web_port > 65535) {
                std::fprintf(stderr, "[ARG ERROR] --web-port 必须在 [1,65535]: %s\n", v);
                return false;
            }
        }
        else if (arg == "--web-hold-seconds") {
            const char* v = need_value("--web-hold-seconds"); if (!v) return false;
            if (!parse_int64(v, a.web_hold_seconds) || a.web_hold_seconds < -1) {
                std::fprintf(stderr, "[ARG ERROR] --web-hold-seconds 必须为 -1 或非负整数: %s\n", v);
                return false;
            }
        }
        else if (arg == "--web-max-events") {
            const char* v = need_value("--web-max-events"); if (!v) return false;
            if (!parse_int64(v, a.web_max_events) || a.web_max_events < 1 || a.web_max_events > 100) {
                std::fprintf(stderr, "[ARG ERROR] --web-max-events 必须在 [1,100]: %s\n", v);
                return false;
            }
        }
        else {
            std::fprintf(stderr, "[ARG ERROR] 未知参数: %s\n", arg.c_str());
            return false;
        }
    }

    // 范围校验：架构约束要求队列容量为 4~8
    if (a.queue_capacity < 4 || a.queue_capacity > 8) {
        std::fprintf(stderr,
                     "[ARG ERROR] --queue-capacity must be in [4,8], got %lld\n",
                     (long long)a.queue_capacity);
        return false;
    }

    if (!a.web_idle && a.video.empty()) {
        std::fprintf(stderr, "[ARG ERROR] 必须提供 --video\n");
        return false;
    }
    if (a.web_idle && a.web_port == 0) {
        std::fprintf(stderr, "[ARG ERROR] --web-idle 必须同时提供 --web-port\n");
        return false;
    }
    if (a.web_idle && (!a.video.empty() || !a.model.empty() || !a.mmproj.empty())) {
        std::fprintf(stderr,
                     "[ARG ERROR] --web-idle 仅允许空配置；不得同时指定视频或模型\n");
        return false;
    }
    if (a.unattended) {
        a.web_hold_seconds = -1;
        if (a.web_port == 0) {
            std::fprintf(stderr, "[ARG ERROR] --unattended 必须同时提供 --web-port\n");
            return false;
        }
    }
    if (!a.web_idle && !a.no_vlm && (a.model.empty() || a.mmproj.empty())) {
        std::fprintf(stderr,
                     "[ARG ERROR] 启用 VLM 时必须提供 --model 与 --mmproj\n");
        return false;
    }
    return true;
}

void hold_web_if_requested(const Args& args, web::DashboardServer* server) {
    if (!server || args.web_hold_seconds == 0) return;
    if (args.web_hold_seconds < 0) {
        std::printf("无人值守面板持续运行；按 Ctrl+C 停止进程。\n");
        for (;;) std::this_thread::sleep_for(std::chrono::hours(24));
    }
    std::printf("面板将在 %lld 秒后关闭。\n", (long long)args.web_hold_seconds);
    std::this_thread::sleep_for(std::chrono::seconds(args.web_hold_seconds));
}

}  // namespace

int main(int argc, char** argv) {
    // --help / -h：打印用法并以 0 退出
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    Args args;
    if (!parse_args(argc, argv, args)) {
        print_usage();
        return 2;
    }

    // ---- 局域网远程面板（可选）----
    web::DashboardState dashboard((std::size_t)args.web_max_events);
    std::unique_ptr<web::DashboardServer> dashboard_server;
    if (args.web_port > 0) {
        web::DashboardConfig web_cfg;
        web_cfg.bind_address = args.web_bind;
        web_cfg.port = (uint16_t)args.web_port;
        web_cfg.max_events = (std::size_t)args.web_max_events;
        dashboard_server = std::make_unique<web::DashboardServer>(web_cfg, dashboard);
        std::string web_error;
        if (!dashboard_server->start(web_error)) {
            std::fprintf(stderr, "[FATAL] %s\n", web_error.c_str());
            return 1;
        }
        if (args.web_idle) {
            dashboard.set_run_state(
                "waiting_config",
                "等待配置真实视频、模型权重与 Skill；当前未加载视频，也未运行推理");
        } else {
            dashboard.set_run_state("loading", "正在加载模型与视频管线");
        }
        std::printf("Web dashboard: %s\n", dashboard_server->display_url().c_str());
    }

    if (args.web_idle) {
        std::printf("等待配置模式：只提供空面板，不读取视频或加载模型；按停止容器结束。\n");
        for (;;) std::this_thread::sleep_for(std::chrono::hours(24));
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
    cfg.filter.change_threshold = (float)args.threshold;
    cfg.queue_capacity = (std::size_t)args.queue_capacity;
    cfg.analysis_width = args.analysis_width;

    // ---- 视觉模型加载（VLM 只做图像描述） ----
    std::unique_ptr<model::LlamaVLM> vlm;
    if (!args.no_vlm) {
        model::VLMParams p;
        p.model_path = args.model;
        p.mmproj_path = args.mmproj;
        vlm = std::make_unique<model::LlamaVLM>(p);
        if (!vlm->is_loaded()) {
            std::fprintf(stderr, "[FATAL] %s\n", vlm->last_error().c_str());
            return 1;
        }
    }
    if (dashboard_server) dashboard.set_run_state("running", "正在分析候选帧");

    // ---- 队列与两个 Worker ----
    app::PipelineStats stats;
    queue::BoundedQueue<video::CandidateFrame> q(cfg.queue_capacity);

    const auto t0 = std::chrono::steady_clock::now();
    std::jthread video_thread(app::video_worker, std::cref(cfg), std::ref(logger),
                              std::ref(q), std::ref(stats));

    std::jthread vlm_thread;
    if (vlm) {
        app::VlmWorkerParams vp;
        vp.vision_model = vlm.get();
        vp.logger = &logger;
        vp.stats = &stats;
        vp.dashboard = dashboard_server ? &dashboard : nullptr;
        vlm_thread = std::jthread(app::vlm_worker, std::ref(q), vp);
    }

    video_thread.join();
    if (vlm_thread.joinable()) vlm_thread.join();

    // 视频源失败不是正常结束：以非零退出码报告
    if (stats.video_failed.load()) {
        if (dashboard_server) dashboard.set_run_state("error", "视频处理失败，请查看主机日志");
        std::fprintf(stderr, "[FATAL] 视频处理失败，详见日志: %s\n", args.log_path.c_str());
        hold_web_if_requested(args, dashboard_server.get());
        return 1;
    }

    const double total_secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    stats.candidates_dropped = q.dropped_full();
    stats.log_summary(logger);
    if (dashboard_server) dashboard.set_run_state("complete", "视频处理完成，结果保留在此页面");

    std::printf("== edge_agent done in %.1fs ==\n", total_secs);
    std::printf("frames_read      : %lld\n", (long long)stats.frames_read.load());
    std::printf("frames_evaluated : %lld\n", (long long)stats.frames_evaluated.load());
    std::printf("candidates_pushed: %lld\n", (long long)stats.candidates_pushed.load());
    std::printf("candidates_dropped: %lld\n", (long long)stats.candidates_dropped.load());
    if (vlm) {
        std::printf("candidates_analyzed: %lld\n",
                    (long long)stats.candidates_analyzed.load());
        std::printf("frames_pushed    : %lld\n", (long long)stats.frames_pushed.load());
        std::printf("agent_finals    : %lld\n", (long long)stats.agent_finals.load());
        std::printf("agent_step_limits: %lld\n", (long long)stats.agent_step_limits.load());
        if (stats.vlm_latency_count.load() > 0)
            std::printf("vlm_latency_avg : %.2fs\n",
                        stats.vlm_latency_sum.load() / (double)stats.vlm_latency_count.load());
    }
    std::printf("log              : %s\n", args.log_path.c_str());
    hold_web_if_requested(args, dashboard_server.get());
    return 0;
}
