#include <cstdio>
#include <vector>

#include "web/dashboard.hpp"

static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { std::fprintf(stderr, "[FAIL] %s (line %d)\n", m, __LINE__); ++failures; } } while (0)

int main() {
    web::DashboardState state(2);
    state.set_run_state("running", "test");
    video::CandidateFrame frame;
    frame.timestamp = 1.25; frame.change_score = 0.5f; frame.width = 2; frame.height = 2;
    frame.rgb = {255,0,0, 0,255,0, 0,0,255, 255,255,255};
    std::vector<uint8_t> bmp;
    const uint64_t first = state.publish_candidate(frame);
    state.publish_observation(first, "a visible door");
    CHECK(state.snapshot_json()["events"].empty(), "未执行工具的候选帧不可见");
    state.discard(first);
    CHECK(!state.frame_bmp(first, bmp), "未推送候选可被立即丢弃");
    const uint64_t pushed = state.publish_candidate(frame);
    state.publish_observation(pushed, "a visible door");
    CHECK(state.publish_push(pushed, "door opened"), "push marks event visible");
    agent::AgentResult result; result.status = agent::AgentStatus::CompletedFinal; result.content = "door opened";
    result.tool_results.push_back(agent::ToolResult::ok("push_frame", {{"pushed", true}}));
    state.publish_result(pushed, result, 0.25);
    auto json = state.snapshot_json();
    CHECK(json["run_state"] == "running", "run state");
    CHECK(json["events"].size() == 1, "one event");
    CHECK(json["events"][0]["analysis"] == "door opened", "analysis visible");
    CHECK(json["events"][0]["observation"] == "a visible door", "observation visible");
    CHECK(json["events"][0]["tool_results"].size() == 1, "tool result visible");
    CHECK(state.frame_bmp(pushed, bmp), "frame exists");
    CHECK(bmp.size() >= 54 && bmp[0] == 'B' && bmp[1] == 'M', "valid BMP header");
    const auto second = state.publish_candidate(frame); state.publish_push(second, "second");
    const auto third = state.publish_candidate(frame); state.publish_push(third, "third");
    CHECK(state.snapshot_json()["events"].size() == 2, "pushed history remains bounded");
    CHECK(!state.frame_bmp(pushed, bmp), "evicted frame is not retained");
    const auto pending = state.publish_candidate(frame);
    CHECK(state.snapshot_json()["events"].size() == 2,
          "pending candidate does not evict pushed history");
    state.discard(pending);
    CHECK(state.snapshot_json()["events"].size() == 2,
          "discarded candidate leaves pushed history intact");
    if (!failures) { std::fprintf(stderr, "[PASS] dashboard_test\n"); return 0; }
    return 1;
}
