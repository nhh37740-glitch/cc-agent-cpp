// Qwen 文本决策模型真实验收：相同工具/Skill 下，明确正例调用 push_frame，负例 final。
#include <cstdio>
#include <string>

#include "agent/agent.hpp"
#include "agent/builtin_tools.hpp"
#include "model/llama_text.hpp"
#include "model/tool_decision_model.hpp"
#include "skill/skill_loader.hpp"

static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { std::fprintf(stderr, "[FAIL] %s (line %d)\n", m, __LINE__); ++failures; } } while (0)

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: decision_model_test <qwen.gguf> <skill.md>\n"); return 2; }
    model::TextModelParams p; p.model_path = argv[1]; p.n_ctx = 4096; p.max_tokens = 256;
    model::LlamaText model(p);
    if (!model.is_loaded()) { std::fprintf(stderr, "[FAIL] %s\n", model.last_error().c_str()); return 1; }
    skill::Skill sk; std::string error;
    if (!skill::load_skill(argv[2], sk, error)) { std::fprintf(stderr, "[FAIL] %s\n", error.c_str()); return 1; }
    int pushed = 0; std::string summary;
    agent::ToolContext tc;
    tc.push_frame_sink = [&](const std::string& s, std::string&) { ++pushed; summary = s; return true; };
    auto tools = agent::make_default_registry(tc);
    model::ToolDecisionModel router(model, sk.content);
    auto run = [&](const std::string& facts) {
        agent::Observation o;
        o.system_prompt = skill::build_system_prompt(sk, tools.names());
        o.visual_description = facts;
        o.frame_timestamp = 1.0;
        return agent::Agent(router, tools).run(o);
    };
    const auto positive = run(
        "entrance: visible, a closed front door fills the center; "
        "ground_object: visible, one sealed brown cardboard shipping box rests on the ground "
        "directly in front of the door; person: visible, a courier stands beside the box; "
        "hazard: not visible");
    std::fprintf(stderr, "positive status=%d content=%s reason=%s tools=%zu\n",
                 (int)positive.status, positive.content.c_str(), positive.reason.c_str(),
                 positive.tool_results.size());
    CHECK(pushed == 1, "明确门口包裹必须执行一次 push_frame");
    CHECK(!summary.empty(), "push_frame 必须包含 summary");
    CHECK(!positive.tool_results.empty() && positive.tool_results[0].success,
          "正例必须记录成功 ToolResult");

    const int before = pushed;
    const auto negative = run(
        "entrance: visible, an empty closed front door; ground_object: not visible; "
        "person: not visible; hazard: not visible");
    std::fprintf(stderr, "negative status=%d content=%s reason=%s tools=%zu\n",
                 (int)negative.status, negative.content.c_str(), negative.reason.c_str(),
                 negative.tool_results.size());
    CHECK(pushed == before, "空门口负例不得推送");
    CHECK(negative.status == agent::AgentStatus::CompletedFinal, "负例必须 final");

    const auto package_without_person = run(
        "entrance: visible; ground_object: visible, cardboard box; person: not visible; "
        "hazard: not visible");
    CHECK(pushed == before + 1, "人离开后仍明确可见的门口纸箱必须推送");
    CHECK(!package_without_person.tool_results.empty(), "纸箱正例必须产生 ToolResult");

    const int after_package = pushed;
    const auto negation = run(
        "entrance: visible; ground_object: not visible; person: visible; hazard: not visible");
    CHECK(pushed == after_package, "not visible 不能因包含 visible 子串而触发推送");
    CHECK(negation.status == agent::AgentStatus::CompletedFinal, "否定词负例必须 final");
    if (!failures) { std::fprintf(stderr, "[PASS] decision_model_test\n"); return 0; }
    return 1;
}
