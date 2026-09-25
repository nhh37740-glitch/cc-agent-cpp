// Phase 9 验收：Agent Loop。
// 用脚本化 MockModel 验证：
// 1) 成功：工具执行，上下文序列为 assistant(tool_call) -> tool(tool_result)；
// 2) 失败：OUTPUT_ERROR 结果回填模型（同样保留 assistant 调用）；
// 3) 拒绝：白名单外工具返回 TOOL_NOT_ALLOWED 并回填；
// 4) 非法 JSON：assistant 原文 + 纠错反馈进入上下文并自我修正；
// 5) 步数上限：第 3 次生成的 tool_call 不执行、不伪造 ToolResult，
//    返回 STEP_LIMIT_REACHED 并记录 unexecuted_tool 与 reason。

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "agent/agent.hpp"
#include "agent/builtin_tools.hpp"
#include "log.hpp"
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

// 断言上下文中存在 assistant(call 原文) -> tool(result 关键字) 的相邻序列
void check_call_result_sequence(const std::vector<model::Message>& msgs,
                                const std::string& call_text,
                                const std::string& result_keyword, const char* tag) {
    bool ok = false;
    for (size_t i = 0; i + 1 < msgs.size(); ++i) {
        if (msgs[i].role == model::Role::Assistant && msgs[i].text == call_text &&
            msgs[i + 1].role == model::Role::Tool &&
            msgs[i + 1].text.find(result_keyword) != std::string::npos) {
            ok = true;
            break;
        }
    }
    CHECK(ok, tag);
}

}  // namespace

int main() {
    skill::Skill sk;
    std::string err;
    // CTest 以仓库根目录为工作目录
    if (!skill::load_skill("skills/door-camera.md", sk, err)) {
        std::fprintf(stderr, "[FAIL] load skill: %s\n", err.c_str());
        return 2;
    }
    const std::string sys_prompt =
        skill::build_system_prompt(sk, {"push_frame"});

    agent::ToolContext tctx;
    bool push_fail = false;
    tctx.push_frame_sink = [&](const std::string&, std::string& error) {
        if (push_fail) error = "failed";
        return !push_fail;
    };
    agent::ToolRegistry tools = agent::make_default_registry(tctx);

    agent::Observation obs;
    obs.system_prompt = sys_prompt;
    obs.frame_timestamp = 12.5;
    obs.change_score = 0.42f;
    obs.visual_description = "entrance: not visible; ground_object: not visible; person: not visible; hazard: not visible";

    {
        MockModel model;
        model.script = {R"({"type":"final","content":"ok"})"};
        agent::Agent agent(model, tools);
        (void)agent.run(obs);
        CHECK(model.contexts[0].size() >= 2 &&
                  model.contexts[0][1].text.find("decision JSON") !=
                      std::string::npos,
              "图像用户消息必须在最近位置强调命中触发条件时选择工具");
    }

    // ---- 1) 成功路径 ----
    {
        MockModel model;
        const std::string call =
            R"({"type":"tool_call","name":"push_frame","arguments":{"summary":"package!"}})";
        model.script = {call, R"({"type":"final","content":"notified"})"};
        agent::Agent agent(model, tools);
        auto r = agent.run(obs);
        CHECK(r.status == agent::AgentStatus::CompletedFinal, "success path");
        CHECK(r.content == "notified", "final content");
        CHECK(r.tool_results.size() == 1 && r.tool_results[0].success, "push_frame ok");
        CHECK(model.calls() == 2, "two generations");
        check_call_result_sequence(model.contexts[1], call, "\"success\":true",
                                   "gen2 context: assistant(tool_call)->tool(success)");
    }

    // ---- 2) 失败路径：push_frame 输出失败 ----
    {
        MockModel model;
        const std::string call =
            R"({"type":"tool_call","name":"push_frame","arguments":{"summary":"x"}})";
        model.script = {call, R"({"type":"final","content":"retry later"})"};
        agent::Agent agent(model, tools);
        push_fail = true;
        auto r = agent.run(obs);
        push_fail = false;
        CHECK(r.status == agent::AgentStatus::CompletedFinal, "failure path final");
        CHECK(r.tool_results.size() == 1 && !r.tool_results[0].success,
              "failed ToolResult recorded");
        CHECK(r.tool_results[0].error_code == "OUTPUT_ERROR", "OUTPUT_ERROR code");
        check_call_result_sequence(model.contexts[1], call, "OUTPUT_ERROR",
                                   "gen2 context: assistant->tool(OUTPUT_ERROR)");
    }

    // ---- 3) 拒绝路径：白名单外工具 ----
    {
        MockModel model;
        const std::string call =
            R"({"type":"tool_call","name":"delete_file","arguments":{"path":"C:/"}})";
        model.script = {call, R"({"type":"final","content":"ok"})"};
        agent::Agent agent(model, tools);
        auto r = agent.run(obs);
        CHECK(r.status == agent::AgentStatus::CompletedFinal, "reject path final");
        CHECK(r.tool_results.size() == 1 && !r.tool_results[0].success &&
                  r.tool_results[0].error_code == "TOOL_NOT_ALLOWED",
              "TOOL_NOT_ALLOWED result");
        check_call_result_sequence(model.contexts[1], call, "TOOL_NOT_ALLOWED",
                                   "gen2 context: assistant->tool(TOOL_NOT_ALLOWED)");
    }

    // ---- 4) 非法 JSON 后自我修正 ----
    {
        MockModel model;
        const std::string garbage = "I want to call the notify tool please.";
        model.script = {garbage, R"({"type":"final","content":"fixed"})"};
        agent::Agent agent(model, tools);
        auto r = agent.run(obs);
        CHECK(r.status == agent::AgentStatus::CompletedFinal, "recovered final");
        // 上下文顺序：assistant(原文) -> user(纠错)
        const auto& ctx = model.contexts[1];
        bool ok = false;
        for (size_t i = 0; i + 1 < ctx.size(); ++i) {
            if (ctx[i].role == model::Role::Assistant && ctx[i].text == garbage &&
                ctx[i + 1].role == model::Role::User &&
                ctx[i + 1].text.find("invalid") != std::string::npos) {
                ok = true;
            }
        }
        CHECK(ok, "correction feedback follows assistant text");
    }

    // ---- 5) 步数上限语义 ----
    {
        MockModel model;
        const std::string c1 =
            R"({"type":"tool_call","name":"push_frame","arguments":{"summary":"first"}})";
        model.script = {
            c1,
            R"({"type":"tool_call","name":"push_frame","arguments":{"summary":"again"}})",
            R"({"type":"tool_call","name":"push_frame","arguments":{"summary":"third"}})"};
        agent::Agent agent(model, tools);
        auto r = agent.run(obs);
        CHECK(r.status == agent::AgentStatus::StepLimitReached, "step limit reached");
        CHECK(model.calls() == 3, "max three generations");
        CHECK(r.unexecuted_tool == "push_frame", "unexecuted_tool=push_frame");
        CHECK(r.reason == "no_followup_generation_budget",
              "reason=no_followup_generation_budget");
        CHECK(r.tool_results.size() == 2, "only two executed results");
        CHECK(r.tool_results[0].tool == "push_frame" && r.tool_results[1].tool == "push_frame",
              "executed order");
        check_call_result_sequence(model.contexts[1], c1, "\"success\":true",
                                   "step-limit scenario also pairs assistant+result");
    }

    // ---- 6) 同一帧不得重复执行同一种副作用工具 ----
    {
        int push_count = 0;
        agent::ToolContext duplicate_ctx;
        duplicate_ctx.push_frame_sink = [&](const std::string&, std::string&) {
            ++push_count;
            return true;
        };
        agent::ToolRegistry duplicate_tools = agent::make_default_registry(duplicate_ctx);
        MockModel model;
        const std::string first =
            R"({"type":"tool_call","name":"push_frame","arguments":{"summary":"first"}})";
        const std::string again =
            R"({"type":"tool_call","name":"push_frame","arguments":{"summary":"again"}})";
        model.script = {first, again, R"({"type":"final","content":"done"})"};
        agent::Agent agent(model, duplicate_tools);
        auto r = agent.run(obs);
        CHECK(r.status == agent::AgentStatus::CompletedFinal,
              "duplicate side effect can recover to final");
        CHECK(push_count == 1, "push_frame has only one actual side effect per frame");
        CHECK(r.tool_results.size() == 2,
              "executed and duplicate-rejected calls both produce ToolResult");
        CHECK(r.tool_results[0].success, "first push succeeds");
        CHECK(!r.tool_results[1].success &&
                  r.tool_results[1].error_code == "DUPLICATE_TOOL_CALL",
              "duplicate push rejected with stable code");
        check_call_result_sequence(model.contexts[2], again, "DUPLICATE_TOOL_CALL",
                                   "duplicate rejection is visible to next generation");
    }

    // ---- 7) 最后一次生成交无效输出且无预算 ----
    {
        MockModel model;
        model.script = {
            R"({"type":"tool_call","name":"push_frame","arguments":{"summary":"x"}})",
            "not json",
            "still not json"};  // 第 3 次（最后一次）生成仍无效且无预算
        agent::Agent agent(model, tools);
        auto r = agent.run(obs);
        CHECK(r.status == agent::AgentStatus::StepLimitReached,
              "invalid output on last generation");
        CHECK(r.reason == "invalid_output_no_budget", "invalid_output_no_budget");
        CHECK(r.tool_results.size() == 1, "only first tool has result");
    }

    if (failures == 0) {
        std::fprintf(stderr, "[PASS] agent_loop_test\n");
        return 0;
    }
    return 1;
}
