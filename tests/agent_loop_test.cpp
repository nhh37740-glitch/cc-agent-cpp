// Phase 9 验收：Agent Loop。
// 用脚本化 MockModel 验证：
// 1) 成功：工具执行，ToolResult 在下一次生成前进入上下文；
// 2) 失败：OUTPUT_ERROR 结果回填模型；
// 3) 拒绝：白名单外工具返回 TOOL_NOT_ALLOWED 并回填；
// 4) 非法 JSON：错误反馈给模型并自我修正；
// 5) 步数上限：第 3 次生成的 tool_call 不执行、不伪造 ToolResult，
//    返回 STEP_LIMIT_REACHED 并记录 unexecuted_tool 与 reason。

#include <cstdio>
#include <string>
#include <vector>

#include "agent/agent.hpp"
#include "agent/builtin_tools.hpp"
#include "skill/skill_loader.hpp"

static int failures = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "[FAIL] %s (line %d)\n", msg, __LINE__); \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

namespace {

// 脚本化模型：按预设依次输出；记录每次调用收到的完整上下文
class MockModel : public model::Model {
public:
    std::vector<std::string> script;
    std::vector<std::vector<model::Message>> contexts;

    model::ModelResponse generate(const std::vector<model::Message>& messages,
                                  const uint8_t*, int, int) override {
        contexts.push_back(messages);
        model::ModelResponse r;
        r.text = script.at(calls_++);
        return r;
    }
    int calls() const { return calls_; }

private:
    int calls_ = 0;
};

std::string context_dump(const std::vector<model::Message>& msgs) {
    std::string all;
    for (const auto& m : msgs) all += m.text + "\n";
    return all;
}

}  // namespace

int main() {
    const char* skill_path = "skills/door-camera.md";
    skill::Skill sk;
    std::string err;
    if (!skill::load_skill(skill_path, sk, err)) {
        // 测试工作目录可能是 build/，尝试上级
        if (!skill::load_skill("../skills/door-camera.md", sk, err)) {
            std::fprintf(stderr, "[FAIL] 无法加载 skill: %s\n", err.c_str());
            return 2;
        }
    }
    const std::string sys_prompt =
        skill::build_system_prompt(sk, {"notify(text)", "save_event(event, description)",
                                        "get_time()", "speak(text)"});

    agent::ToolContext tctx;
    tctx.events_log = (std::filesystem::temp_directory_path() / "edge_agent_test" /
                       "agent_events.jsonl")
                          .string();
    bool notify_fail = false;
    tctx.notify_sink = [&](const std::string&) { return !notify_fail; };
    agent::ToolRegistry tools = agent::make_default_registry(tctx);

    agent::Observation obs;
    obs.system_prompt = sys_prompt;
    obs.frame_timestamp = 12.5;
    obs.change_score = 0.42f;

    // ---- 1) 成功路径 ----
    {
        MockModel model;
        model.script = {
            R"({"type":"tool_call","name":"notify","arguments":{"text":"package!"}})",
            R"({"type":"final","content":"notified"})"};
        agent::Agent agent(model, tools);
        auto r = agent.run(obs);
        CHECK(r.status == agent::AgentStatus::CompletedFinal, "成功路径应 CompletedFinal");
        CHECK(r.content == "notified", "final 内容应保留");
        CHECK(r.tool_results.size() == 1 && r.tool_results[0].success, "notify 应执行成功");
        CHECK(model.calls() == 2, "应恰好两次模型生成");
        CHECK(context_dump(model.contexts[1]).find("\"success\": true") !=
                      std::string::npos ||
                  context_dump(model.contexts[1]).find("\"success\":true") != std::string::npos,
              "第二次生成前应看到 success=true 的 ToolResult");
    }

    // ---- 2) 失败路径：notify 输出失败 ----
    {
        MockModel model;
        model.script = {
            R"({"type":"tool_call","name":"notify","arguments":{"text":"x"}})",
            R"({"type":"final","content":"retry later"})"};
        agent::Agent agent(model, tools);
        notify_fail = true;
        auto r = agent.run(obs);
        notify_fail = false;
        CHECK(r.status == agent::AgentStatus::CompletedFinal, "失败路径也应正常终止");
        CHECK(r.tool_results.size() == 1 && !r.tool_results[0].success,
              "失败的 ToolResult 应被记录");
        CHECK(r.tool_results[0].error_code == "OUTPUT_ERROR", "错误码应为 OUTPUT_ERROR");
        CHECK(context_dump(model.contexts[1]).find("OUTPUT_ERROR") != std::string::npos,
              "失败结果必须在下一次生成前回填");
    }

    // ---- 3) 拒绝路径：白名单外工具 ----
    {
        MockModel model;
        model.script = {
            R"({"type":"tool_call","name":"delete_file","arguments":{"path":"C:/"}})",
            R"({"type":"final","content":"ok"})"};
        agent::Agent agent(model, tools);
        auto r = agent.run(obs);
        CHECK(r.status == agent::AgentStatus::CompletedFinal, "拒绝路径应以 final 结束");
        CHECK(r.tool_results.size() == 1 && !r.tool_results[0].success &&
                  r.tool_results[0].error_code == "TOOL_NOT_ALLOWED",
              "白名单外工具应产生 TOOL_NOT_ALLOWED ToolResult");
        CHECK(context_dump(model.contexts[1]).find("TOOL_NOT_ALLOWED") != std::string::npos,
              "拒绝结果必须回填模型");
    }

    // ---- 4) 非法 JSON 后自我修正 ----
    {
        MockModel model;
        model.script = {"I want to call the notify tool please.",
                        R"({"type":"final","content":"fixed"})"};
        agent::Agent agent(model, tools);
        auto r = agent.run(obs);
        CHECK(r.status == agent::AgentStatus::CompletedFinal, "修正后应以 final 结束");
        CHECK(context_dump(model.contexts[1]).find("invalid") != std::string::npos,
              "解析错误应反馈给模型");
    }

    // ---- 5) 步数上限语义 ----
    {
        MockModel model;
        model.script = {
            R"({"type":"tool_call","name":"get_time","arguments":{}})",
            R"({"type":"tool_call","name":"save_event","arguments":{"event":"a","description":"b"}})",
            R"({"type":"tool_call","name":"notify","arguments":{"text":"third"}})"};
        agent::Agent agent(model, tools);
        auto r = agent.run(obs);
        CHECK(r.status == agent::AgentStatus::StepLimitReached,
              "第三次仍请求工具应 STEP_LIMIT_REACHED");
        CHECK(model.calls() == 3, "最多三次模型生成");
        CHECK(r.unexecuted_tool == "notify", "最后一个未执行的 tool_call 应为 notify");
        CHECK(r.reason == "no_followup_generation_budget", "原因应为 no_followup_generation_budget");
        CHECK(r.tool_results.size() == 2,
              "只有前两个工具被执行并有 ToolResult（不伪造第三个）");
        CHECK(r.tool_results[0].tool == "get_time" && r.tool_results[1].tool == "save_event",
              "已执行工具顺序正确");
    }

    // ---- 6) 最后一次生成交无效输出且无预算 ----
    {
        MockModel model;
        model.script = {
            R"({"type":"tool_call","name":"get_time","arguments":{}})",
            "not json",
            "still not json"};  // 第 3 次（最后一次）生成仍无效且无预算
        agent::Agent agent(model, tools);
        auto r = agent.run(obs);
        CHECK(r.status == agent::AgentStatus::StepLimitReached,
              "末轮无效输出应 STEP_LIMIT_REACHED");
        CHECK(r.reason == "invalid_output_no_budget", "原因应为 invalid_output_no_budget");
        CHECK(r.tool_results.size() == 1, "仅第一个工具有 ToolResult");
    }

    if (failures == 0) {
        std::fprintf(stderr, "[PASS] agent_loop_test\n");
        return 0;
    }
    return 1;
}
