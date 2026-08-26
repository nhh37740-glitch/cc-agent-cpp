#pragma once

// Agent Loop：最多 max_generations 次模型生成（第一版为 3）。
// 语义（AGENTS.md 已锁定）：
// - 只有仍保留至少一次后续生成机会时才执行新的工具调用；
//   否则返回 STEP_LIMIT_REACHED，记录最后一个未执行的 tool_call 与
//   reason=no_followup_generation_budget，不伪造 ToolResult；
// - 每个已执行或已拒绝的工具调用的 ToolResult，
//   必须在下一次模型生成前加入上下文。

#include <string>
#include <vector>

#include "agent/structured_output.hpp"
#include "agent/tool_registry.hpp"
#include "agent/tool_result.hpp"
#include "model/model.hpp"

namespace agent {

enum class AgentStatus {
    CompletedFinal,     // 模型给出 final
    StepLimitReached,   // 生成预算用尽（或最后一次输出无效且无预算重试）
};

struct Observation {
    std::string system_prompt;
    double frame_timestamp = 0.0;
    float change_score = 0.0f;

    const uint8_t* rgb = nullptr;  // 可为空：纯文本轮次（如工具结果续跑）
    int width = 0;
    int height = 0;
};

struct AgentResult {
    AgentStatus status = AgentStatus::StepLimitReached;

    // CompletedFinal 时的最终答复
    std::string content;

    // 本轮所有已执行或已拒绝的工具调用产生的 ToolResult
    std::vector<ToolResult> tool_results;

    // StepLimitReached 时：最后一个未执行的 tool_call 名称；无则为空
    std::string unexecuted_tool;

    // 终止细节：no_followup_generation_budget / invalid_output_no_budget
    std::string reason;
};

class Agent {
public:
    explicit Agent(model::Model& model, const ToolRegistry& tools,
                   int max_generations = 3)
        : model_(model), tools_(tools), max_generations_(max_generations) {}

    AgentResult run(const Observation& obs);

private:
    model::Model& model_;
    const ToolRegistry& tools_;
    int max_generations_;
};

}  // namespace agent
