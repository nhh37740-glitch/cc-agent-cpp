// 并行管线实现。线程模型：
//   Video Worker（jthread A）：单向读流，按 PTS pacing，产出候选帧入队。
//   VLM Worker  （jthread B）：出队候选帧，运行 Agent Loop 与工具。
// 队列满时 drop_oldest：摄像头语义下"现在"比"30 秒前"更重要。

#include "app/pipeline.hpp"

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "agent/agent.hpp"
#include "video/ffmpeg_video_source.hpp"
#include "video/realtime_pacing_source.hpp"
#include "video/yuv_to_rgb.hpp"

namespace app {

void video_worker(std::stop_token st, const PipelineConfig& cfg,
                  logging::JsonlLogger& logger,
                  queue::BoundedQueue<video::CandidateFrame>& out_queue,
                  PipelineStats& stats) {
    std::unique_ptr<video::VideoSource> source;
    if (cfg.realtime_pacing) {
        source = std::make_unique<video::RealtimePacingSource>(cfg.video_path.c_str());
    } else {
        source = std::make_unique<video::FFmpegFileSource>(cfg.video_path.c_str());
    }

    filter::FrameFilter frame_filter(cfg.filter);
    std::vector<uint8_t> rgb;

    video::Frame f;
    while (!st.stop_requested()) {
        if (!source->read(f)) break;                       // EOF 或错误
        stats.frames_read.fetch_add(1);

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

        if (cfg.max_seconds > 0 && f.timestamp >= cfg.max_seconds) break;
    }
    out_queue.close();  // 让 VLM Worker 排空后退出
    logger.event("video_done", {});
}

void vlm_worker(std::stop_token st, queue::BoundedQueue<video::CandidateFrame>& in_queue,
                VlmWorkerParams params) {
    agent::Agent agent(*params.model, *params.tools);

    video::CandidateFrame cand;
    while (!st.stop_requested() && in_queue.pop(cand, st)) {
        params.stats->candidates_analyzed.fetch_add(1);

        agent::Observation obs;
        obs.system_prompt =
            skill::build_system_prompt(params.skill, params.tools->names());
        obs.frame_timestamp = cand.timestamp;
        obs.change_score = cand.change_score;
        obs.rgb = cand.rgb.data();
        obs.width = cand.width;
        obs.height = cand.height;

        const auto t0 = std::chrono::steady_clock::now();
        agent::AgentResult result = agent.run(obs);
        const double secs =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        params.stats->vlm_latency_sum.fetch_add(secs);
        params.stats->vlm_latency_count.fetch_add(1);

        nlohmann::ordered_json ev;
        ev["timestamp"] = cand.timestamp;
        ev["latency_s"] = secs;
        if (result.status == agent::AgentStatus::CompletedFinal) {
            params.stats->agent_finals.fetch_add(1);
            ev["status"] = "final";
            ev["content"] = result.content;
        } else {
            params.stats->agent_step_limits.fetch_add(1);
            ev["status"] = "step_limit_reached";
            ev["reason"] = result.reason;
            if (!result.unexecuted_tool.empty())
                ev["unexecuted_tool"] = result.unexecuted_tool;
        }
        nlohmann::ordered_json trs = nlohmann::ordered_json::array();
        for (const auto& tr : result.tool_results) trs.push_back(tr.to_json());
        ev["tool_results"] = trs;
        params.logger->event("agent_result", std::move(ev));
    }
    params.logger->event("vlm_done", {});
}

}  // namespace app
