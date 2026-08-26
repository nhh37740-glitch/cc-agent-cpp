#include "agent/agent.hpp"

namespace agent {

static model::Message make_user_message(const std::string& text) {
    model::Message m;
    m.role = model::Role::User;
    m.text = text;
    return m;
}

AgentResult Agent::run(const Observation& obs) {
    AgentResult result;

    std::vector<model::Message> messages;

    // 系统提示（Skill + 协议）
    model::Message sys;
    sys.role = model::Role::System;
    sys.text = obs.system_prompt;
    messages.push_back(sys);

    // 首条用户消息：帧 + 元信息
    char meta[160];
    std::snprintf(meta, sizeof(meta),
                  "<__media__>\nFrame captured at t=%.2fs (change score %.2f). "
                  "Analyze this frame according to your task.",
                  obs.frame_timestamp, obs.change_score);
    model::Message first = make_user_message(meta);
    first.has_image = true;
    messages.push_back(first);

    for (int gen = 0; gen < max_generations_; ++gen) {
        const bool has_followup_budget = (gen + 1 < max_generations_);

        model::ModelResponse resp =
            model_.generate(messages, obs.rgb, obs.width, obs.height);

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
            messages.push_back(make_user_message(
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

        ToolResult tr = tools_.execute(parsed.tool_name, parsed.arguments);
        result.tool_results.push_back(tr);

        // ToolResult 必须在下一次生成前进入上下文
        messages.push_back(make_user_message(
            "Tool result:\n" + tr.to_json().dump() +
            "\nContinue: call another tool or respond with final."));
    }

    result.status = AgentStatus::StepLimitReached;
    if (result.reason.empty()) result.reason = "no_followup_generation_budget";
    return result;
}

}  // namespace agent
