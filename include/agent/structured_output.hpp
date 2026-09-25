#pragma once

// 模型结构化输出解析。
// 协议：模型必须输出单个 JSON 对象：
//   {"type":"tool_call","name":"...","arguments":{...}}
//   {"type":"final","content":"..."}
// 解析规则（全部安全失败，绝不抛出异常逃逸）：
// - 整个输入（去除首尾空白）必须恰好是一个 JSON 对象；
//   任何前缀/尾随自由文本、多个对象都判定 INVALID；
// - 唯一宽容项：完整输入恰好是一对 Markdown 围栏（```json ... ```），
//   且围栏内恰好一个对象——这是小模型常见且无害的包装；
// - 字段级严格校验：未知键、缺失键、类型错误均判定为 INVALID；
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

ParsedOutput parse_model_output(const std::string& raw_text);

}  // namespace agent
