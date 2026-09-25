#pragma once

// 完整并行管线：Video Worker 与 VLM/Agent Worker 通过
// BoundedQueue<CandidateFrame>（满时 drop_oldest）解耦。
// - Video Worker 按 PTS 节奏产出候选帧，绝不等待 VLM；
// - VLM Worker 慢只导致队列溢出淘汰，不阻塞视频管线；
// - 管线内存 O(1)：仅当前解码帧 + 一份小图 + 队列内候选。

#include <atomic>
#include <cstdint>
#include <string>

#include "filter/frame_filter.hpp"
#include "log.hpp"
#include "model/model.hpp"
#include "queue/bounded_queue.hpp"
#include "video/candidate_frame.hpp"
#include "web/dashboard.hpp"

namespace app {

// 将视觉模型的事实描述规范为统一文本。只接受非空、非空场景的描述。
bool normalize_visual_checklist(const std::string& content, std::string& normalized);

struct PipelineConfig {
    std::string video_path;
    bool realtime_pacing = true;
    double max_seconds = 0.0;  // >0 时读到该视频时间即停（演示用）

    filter::FrameFilterConfig filter;   // sample_interval_ms / threshold 等
    std::size_t queue_capacity = 6;     // 4~8
    int analysis_width = 448;           // 候选帧送入 VLM 前的降采样宽度
};

struct PipelineStats {
    std::atomic<bool> video_failed{false};  // 视频源打开/读取失败（区别于正常 EOF）
    std::atomic<int64_t> frames_read{0};
    std::atomic<int64_t> frames_evaluated{0};
    std::atomic<int64_t> candidates_pushed{0};
    std::atomic<int64_t> candidates_dropped{0};  // 队列满被淘汰
    std::atomic<int64_t> candidates_analyzed{0};
    std::atomic<int64_t> frames_pushed{0};
    std::atomic<int64_t> agent_finals{0};
    std::atomic<int64_t> agent_step_limits{0};
    std::atomic<double> vlm_latency_sum{0.0};
    std::atomic<int64_t> vlm_latency_count{0};

    void log_summary(logging::JsonlLogger& logger) const {
        logger.event("summary", {
            {"frames_read", frames_read.load()},
            {"frames_evaluated", frames_evaluated.load()},
            {"candidates_pushed", candidates_pushed.load()},
            {"candidates_dropped", candidates_dropped.load()},
            {"candidates_analyzed", candidates_analyzed.load()},
            {"frames_pushed", frames_pushed.load()},
            {"agent_finals", agent_finals.load()},
            {"agent_step_limits", agent_step_limits.load()},
            {"vlm_latency_avg_s",
             vlm_latency_count.load() ? vlm_latency_sum.load() / (double)vlm_latency_count.load()
                                      : 0.0},
        });
    }
};

// 视频工作线程体：读帧 → 时间采样/帧差筛选 → 候选转 RGB → 入队。
// stop 被请求时尽快退出；到达 max_seconds 或 EOF 时关闭队列。
void video_worker(std::stop_token st, const PipelineConfig& cfg,
                  logging::JsonlLogger& logger,
                  queue::BoundedQueue<video::CandidateFrame>& out_queue,
                  PipelineStats& stats);

struct VlmWorkerParams {
    model::Model* vision_model = nullptr;   // InternVL：做图像感知并生成描述
    logging::JsonlLogger* logger = nullptr;
    PipelineStats* stats = nullptr;
    web::DashboardState* dashboard = nullptr;  // 可选：向远程面板发布帧与结果
};

// VLM 工作线程体：出队 → 视觉描述 → 直接推送帧图片和文本到客户端。
void vlm_worker(std::stop_token st, queue::BoundedQueue<video::CandidateFrame>& in_queue,
                VlmWorkerParams params);

}  // namespace app
