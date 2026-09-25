#include "agent/agent.hpp"

#include <unordered_set>

namespace agent {

static model::Message make_message(model::Role role, const std::string& text) {
    model::Message m;
    m.role = role;
    m.text = text;
    return m;
}

AgentResult Agent::run(const Observation& obs) {
    AgentResult result;

    std::vector<model::Message> messages;
    std::unordered_set<std::string> used_side_effect_tools;

    // 系统提示（Skill + 协议）
    model::Message sys;
    sys.role = model::Role::System;
    sys.text = obs.system_prompt;
    messages.push_back(sys);

    // 首条用户消息：优先使用独立感知阶段给出的可见事实；兼容测试和直接调用时
    // 仍可把图像交给 Agent。决策阶段不从自然语言猜工具，只让模型显式选择协议。
    model::Message first;
    first.role = model::Role::User;
    if (!obs.visual_description.empty()) {
        first.text = "Visual checklist:\n" + obs.visual_description +
                     "\nApply the Skill and output the decision JSON now.";
        first.has_image = false;
    } else {
        first.text = "No visual checklist was supplied. Output final JSON.";
        first.has_image = true;
    }
    messages.push_back(first);

    for (int gen = 0; gen < max_generations_; ++gen) {
        const bool has_followup_budget = (gen + 1 < max_generations_);

        model::ModelResponse resp =
            model_.generate(messages, obs.rgb, obs.width, obs.height);

        // 模型的原始输出必须先进入上下文（assistant 角色），
        // 后续的 ToolResult / 纠错反馈才能形成正确的对话顺序
        messages.push_back(make_message(model::Role::Assistant, resp.text));

        ParsedOutput parsed = parse_model_output(resp.text);

        if (parsed.type == OutputType::Final) {
            result.status = AgentStatus::CompletedFinal;
            result.content = parsed.content;
            return result;
        }

        if (parsed.type == OutputType::Invalid) {
            if (!has_followup_budget) {
                // 最后一次生成交了无效输出，没有预算再纠正
                result.status = AgentStatus::StepLimitReached;
                result.reason = "invalid_output_no_budget";
                return result;
            }
            // 把解析错误回填，给模型一次自我修正的机会
            messages.push_back(make_message(
                model::Role::User,
                "Your previous output was invalid (" + parsed.error +
                "). Respond again with EXACTLY ONE JSON object following the "
                "protocol: {\"type\":\"tool_call\",...} or {\"type\":\"final\",...}"));
            continue;
        }

        // 工具调用：先检查是否还有后续生成预算
        if (!has_followup_budget) {
            // 不执行无法回填结果的新工具调用
            result.status = AgentStatus::StepLimitReached;
            result.unexecuted_tool = parsed.tool_name;
            result.reason = "no_followup_generation_budget";
            return result;
        }

        ToolResult tr;
        const bool is_side_effect_tool = parsed.tool_name == "push_frame";
        if (is_side_effect_tool && used_side_effect_tools.contains(parsed.tool_name)) {
            // 单帧 Agent 运行内，同一种副作用最多执行一次。模型可以得到明确拒绝结果，
            // 但不能因为续轮生成而重复推送同一帧。
            tr = ToolResult::fail(parsed.tool_name, "DUPLICATE_TOOL_CALL",
                                  "This side-effect tool was already executed for this image");
        } else {
            tr = tools_.execute(parsed.tool_name, parsed.arguments);
            if (is_side_effect_tool) used_side_effect_tools.insert(parsed.tool_name);
        }
        result.tool_results.push_back(tr);

        // ToolResult 必须在下一次生成前进入上下文；
        // 序列：assistant(tool_call) -> tool(tool_result) -> assistant(...)
        messages.push_back(make_message(
            model::Role::Tool,
            tr.to_json().dump() +
            "\nContinue: call another tool or respond with final."));
    }

    result.status = AgentStatus::StepLimitReached;
    if (result.reason.empty()) result.reason = "no_followup_generation_budget";
    return result;
}

}  // namespace agent
