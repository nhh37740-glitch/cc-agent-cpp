#pragma once

// 工具执行结果。成功、失败、被拒绝都统一表示为 ToolResult，
// 并在 Agent Loop 中回填进模型上下文。

#include <nlohmann/json.hpp>
#include <string>

namespace agent {

struct ToolResult {
    bool success = false;
    std::string tool;

    nlohmann::json data = nullptr;  // 成功时的返回数据
    std::string error_code;         // 失败时的错误码（TOOL_NOT_ALLOWED / INVALID_ARGUMENTS / OUTPUT_ERROR / EXECUTION_ERROR）
    std::string error_message;

    static ToolResult ok(const std::string& tool, nlohmann::json data) {
        ToolResult r;
        r.success = true;
        r.tool = tool;
        r.data = std::move(data);
        return r;
    }

    static ToolResult fail(const std::string& tool, std::string code, std::string message) {
        ToolResult r;
        r.success = false;
        r.tool = tool;
        r.error_code = std::move(code);
        r.error_message = std::move(message);
        return r;
    }

    // 序列化为回填模型的 JSON 形态
    nlohmann::ordered_json to_json() const {
        nlohmann::ordered_json j;
        j["type"] = "tool_result";
        j["tool"] = tool;
        j["success"] = success;
        if (success) {
            j["data"] = data;
        } else {
            nlohmann::ordered_json e;
            e["code"] = error_code;
            e["message"] = error_message;
            j["error"] = e;
        }
        return j;
    }
};

}  // namespace agent
