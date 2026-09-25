#pragma once

// 局域网只读 Web 面板。
// Agent 主机保存一个有界事件环，远端设备通过浏览器查看候选帧、分析结果与工具结果。

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent/agent.hpp"
#include "video/candidate_frame.hpp"

namespace web {

struct DashboardConfig {
    std::string bind_address = "0.0.0.0";
    uint16_t port = 0;              // 0 表示不启动 Web 服务
    std::size_t max_events = 20;    // 有界历史，避免无人值守运行时持续增长
};

class DashboardState {
public:
    explicit DashboardState(std::size_t max_events = 20);

    void set_run_state(std::string state, std::string detail = {});
    uint64_t publish_candidate(const video::CandidateFrame& frame);
    void publish_observation(uint64_t id, std::string observation);
    bool publish_push(uint64_t id, std::string summary);
    void discard(uint64_t id);
    void publish_result(uint64_t id, const agent::AgentResult& result, double latency_seconds);

    nlohmann::ordered_json snapshot_json() const;
    bool frame_bmp(uint64_t id, std::vector<uint8_t>& out) const;

private:
    struct Event {
        uint64_t id = 0;
        double video_timestamp = 0.0;
        float change_score = 0.0f;
        int width = 0;
        int height = 0;
        std::string status = "analyzing";
        std::string observation;
        std::string analysis;
        std::string reason;
        bool pushed = false;
        double latency_seconds = 0.0;
        nlohmann::ordered_json tool_results = nlohmann::ordered_json::array();
        std::vector<uint8_t> bmp;
    };

    static std::vector<uint8_t> rgb_to_bmp(const video::CandidateFrame& frame);

    mutable std::mutex mutex_;
    std::deque<Event> events_;
    std::size_t max_events_;
    uint64_t next_id_ = 1;
    std::string run_state_ = "starting";
    std::string run_detail_;
};

class DashboardServer {
public:
    DashboardServer(DashboardConfig config, DashboardState& state);
    ~DashboardServer();

    DashboardServer(const DashboardServer&) = delete;
    DashboardServer& operator=(const DashboardServer&) = delete;

    bool start(std::string& error);
    void stop();
    bool running() const;
    std::string display_url() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace web
