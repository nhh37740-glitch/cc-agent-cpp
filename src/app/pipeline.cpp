// 并行管线实现。线程模型：
//   Video Worker（jthread A）：单向读流，按 PTS pacing，产出候选帧入队。
//   VLM Worker  （jthread B）：出队候选帧，运行 Agent Loop 与工具。
// 队列满时 drop_oldest：摄像头语义下"现在"比"30 秒前"更重要。
//
// 结束语义：
// - 正常 EOF 或达到 max_seconds → 记录 video_done；
// - 视频源错误 → 记录 error 并置 stats.video_failed（主程序返回非 0）；
// - stop 请求中断 pacing 等待 → 记录 video_stopped，不算 EOF 也不算错误。

#include "app/pipeline.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "video/ffmpeg_video_source.hpp"
#include "video/realtime_pacing_source.hpp"
#include "video/yuv_to_rgb.hpp"

namespace app {

void video_worker(std::stop_token st, const PipelineConfig& cfg,
                  logging::JsonlLogger& logger,
                  queue::BoundedQueue<video::CandidateFrame>& out_queue,
                  PipelineStats& stats) {
    std::unique_ptr<video::VideoSource> source;
    video::RealtimePacingSource* pacing = nullptr;
    if (cfg.realtime_pacing) {
        auto paced = std::make_unique<video::RealtimePacingSource>(cfg.video_path.c_str());
        pacing = paced.get();
        source = std::move(paced);
    } else {
        source = std::make_unique<video::FFmpegFileSource>(cfg.video_path.c_str());
    }

    // 打开失败：明确报错并传播，不当作正常结束
    if (!source->is_open()) {
        stats.video_failed.store(true);
        logger.event("error", {{"where", "open_video"},
                               {"path", cfg.video_path},
                               {"detail", source->last_error()}});
        out_queue.close();
        return;
    }

    filter::FrameFilter frame_filter(cfg.filter);
    std::vector<uint8_t> rgb;

    double first_ts = 0.0;
    bool have_first_ts = false;
    bool stopped = false;
    bool failed = false;

    video::Frame f;
    while (!st.stop_requested()) {
        // pacing 模式下等待可被 stop 中断
        video::ReadStatus s = pacing ? pacing->read(f, st) : source->read(f);
        if (s == video::ReadStatus::Stopped) {
            stopped = true;  // 取消不是 EOF 也不是错误
            break;
        }
        if (s == video::ReadStatus::Error) {
            failed = true;
            logger.event("error", {{"where", "read_frame"},
                                   {"path", cfg.video_path},
                                   {"detail", source->last_error()}});
            break;
        }
        if (s != video::ReadStatus::Frame) break;  // 正常 EOF

        stats.frames_read.fetch_add(1);
        if (!have_first_ts) {
            first_ts = f.timestamp;  // 相对首帧时间：非零首 PTS 不影响限时
            have_first_ts = true;
        }

        // 限时检查必须在任何 continue 之前：限制的是读取的视频时长本身
        if (cfg.max_seconds > 0 && (f.timestamp - first_ts) >= cfg.max_seconds) break;

        const filter::FilterResult r = frame_filter.analyze(f);
        if (!r.was_evaluated) continue;                    // 时间门控跳过
        stats.frames_evaluated.fetch_add(1);
        if (!r.should_analyze) continue;                   // 变化不足

        // 候选确定后才做 RGB 转换，并降采样到分析宽度（控制视觉 token 数）
        int cw = 0, ch = 0;
        if (!video::convert_to_rgb(f, rgb, cfg.analysis_width, &cw, &ch)) {
            logger.event("error", {{"where", "convert_to_rgb"},
                                   {"pts", (double)f.pts}});
            continue;
        }

        video::CandidateFrame cand;
        cand.pts = f.pts;
        cand.timestamp = f.timestamp;
        cand.change_score = r.score;
        cand.width = cw;
        cand.height = ch;
        cand.rgb = rgb;

        if (out_queue.push(std::move(cand))) {
            stats.candidates_pushed.fetch_add(1);
            logger.event("candidate",
                         {{"pts", (double)f.pts},
                          {"timestamp", f.timestamp},
                          {"score", r.score}});
        } else {
            break;  // 队列已关闭（消费端提前退出）
        }
    }

    if (failed) {
        stats.video_failed.store(true);
    } else if (stopped) {
        logger.event("video_stopped", {});
    } else {
        logger.event("video_done", {});
    }
    out_queue.close();  // 让 VLM Worker 排空后退出
}

namespace {

std::string trim_copy(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

bool contains_any(const std::string& value, std::initializer_list<const char*> terms) {
    return std::any_of(terms.begin(), terms.end(), [&](const char* term) {
        return value.find(term) != std::string::npos;
    });
}

}  // namespace

bool normalize_visual_checklist(const std::string& content, std::string& normalized) {
    // 当前运行路径接受自然语言视觉描述；仅空白与明确的空场景标记不推送。
    normalized = trim_copy(content);
    if (normalized.empty()) return false;
    std::string lower = normalized;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "scene: empty" || lower == "scene: empty." ||
        lower == "empty" || lower == "empty.") return false;
    return true;
}

namespace {

bool perceive_visible_facts(model::Model& model, const video::CandidateFrame& cand,
                            std::string& description, std::string& error) {
    std::vector<model::Message> messages;
    model::Message system;
    system.role = model::Role::System;
    system.text = "请用中文回答。描述这张图片里你看到的内容。";
    messages.push_back(std::move(system));
    model::Message user;
    user.role = model::Role::User;
    user.text = "<__media__>\n这张图片里有什么？";
    user.has_image = true;
    messages.push_back(std::move(user));

    std::string last_error;
    std::string last_output;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const model::ModelResponse response =
            model.generate(messages, cand.rgb.data(), cand.width, cand.height);
        last_output = response.text;
        const agent::ParsedOutput parsed = agent::parse_model_output(response.text);
        std::string normalized;
        if (parsed.type == agent::OutputType::Final &&
            normalize_visual_checklist(parsed.content, normalized)) {
            description = std::move(normalized);
            return true;
        }
        // VLM 也可能输出纯文本。只对非结构化文本使用回退，避免把空场景
        // 的 JSON 或错误的 tool_call 原文当成视觉事实。
        if (parsed.type == agent::OutputType::Invalid &&
            normalize_visual_checklist(last_output, normalized)) {
            description = std::move(normalized);
            return true;
        }
        last_error = "empty_visual_observation";
        if (attempt == 0) {
            model::Message assistant;
            assistant.role = model::Role::Assistant;
            assistant.text = response.text;
            messages.push_back(std::move(assistant));
            model::Message correction;
            correction.role = model::Role::User;
            correction.text = "描述你看到的内容。";
            messages.push_back(std::move(correction));
        }
    }
    if (last_output.size() > 160) last_output.resize(160);
    error = last_error + ":raw=" + last_output;
    return false;
}

}  // namespace

void vlm_worker(std::stop_token st, queue::BoundedQueue<video::CandidateFrame>& in_queue,
                VlmWorkerParams params) {
    video::CandidateFrame cand;
    while (!st.stop_requested() && in_queue.pop(cand, st)) {
        params.stats->candidates_analyzed.fetch_add(1);
        const uint64_t dashboard_id =
            params.dashboard ? params.dashboard->publish_candidate(cand) : 0;

        const auto t0 = std::chrono::steady_clock::now();
        std::string visual_description;
        std::string perception_error;
        if (!perceive_visible_facts(*params.vision_model, cand, visual_description,
                                    perception_error)) {
            params.stats->vlm_latency_sum.fetch_add(
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            params.stats->vlm_latency_count.fetch_add(1);
            params.logger->event("agent_result",
                                 {{"timestamp", cand.timestamp},
                                  {"status", "perception_failed"},
                                  {"reason", perception_error},
                                  {"tool_results", nlohmann::ordered_json::array()}});
            if (params.dashboard) params.dashboard->discard(dashboard_id);
            continue;
        }

        const double secs =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        params.stats->vlm_latency_sum.fetch_add(secs);
        params.stats->vlm_latency_count.fetch_add(1);

        // 边缘端只负责描述并推送：VLM 生成描述后直接推送帧图片和文本到客户端
        if (params.dashboard) {
            params.dashboard->publish_observation(dashboard_id, visual_description);
            params.dashboard->publish_push(dashboard_id, visual_description);
        }
        params.logger->event("frame_pushed", {{"timestamp", cand.timestamp},
                                              {"summary", visual_description},
                                              {"visual_observation", visual_description}});
        params.stats->frames_pushed.fetch_add(1);

        nlohmann::ordered_json ev;
        ev["timestamp"] = cand.timestamp;
        ev["visual_observation"] = visual_description;
        ev["latency_s"] = secs;
        ev["status"] = "pushed";
        ev["content"] = visual_description;
        nlohmann::ordered_json trs = nlohmann::ordered_json::array();
        trs.push_back({{"type", "tool_result"}, {"tool", "push_frame"},
                       {"success", true}, {"data", {{"pushed", true},
                                                    {"summary", visual_description}}}});
        ev["tool_results"] = trs;
        params.logger->event("agent_result", std::move(ev));
    }
    params.logger->event("vlm_done", {});
}

}  // namespace app
