#pragma once

// 模型结构化输出解析。
// 协议：模型必须输出单个 JSON 对象：
//   {"type":"tool_call","name":"...","arguments":{...}}
//   {"type":"final","content":"..."}
// 解析规则（全部安全失败，绝不抛出异常逃逸）：
// - 先剥掉 Markdown 代码围栏等包装，提取首个平衡的 JSON 对象；
// - 严格校验字段：未知键、缺失键、类型错误均判定为 INVALID；
// - 解析结果只用于受白名单控制的工具执行。

#include <nlohmann/json.hpp>
#include <string>

namespace agent {

enum class OutputType { ToolCall, Final, Invalid };

struct ParsedOutput {
    OutputType type = OutputType::Invalid;

    // Final
    std::string content;

    // ToolCall
    std::string tool_name;
    nlohmann::json arguments = nlohmann::json::object();

    // Invalid 时给出原因（用于日志与回填模型的错误反馈）
    std::string error;
};

// 从原始文本中提取首个平衡的 {...} 子串；失败返回空串。
std::string extract_json_object(const std::string& text);

ParsedOutput parse_model_output(const std::string& raw_text);

}  // namespace agent
