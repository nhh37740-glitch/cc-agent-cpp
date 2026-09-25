#include "agent/builtin_tools.hpp"

namespace agent {

// 参数校验：要求对象中存在 string 类型的键
static bool get_string_arg(const nlohmann::json& args, const char* key, std::string& out) {
    if (!args.is_object() || !args.contains(key) || !args.at(key).is_string()) return false;
    out = args.at(key).get<std::string>();
    return true;
}

ToolRegistry make_default_registry(ToolContext ctx) {
    ToolRegistry reg;
    reg.register_tool("push_frame", [ctx](const nlohmann::json& args) {
        std::string summary;
        if (!get_string_arg(args, "summary", summary) || summary.empty()) {
            return ToolResult::fail("push_frame", "INVALID_ARGUMENTS",
                                    "arguments must contain non-empty string field 'summary'");
        }
        if (!ctx.push_frame_sink) {
            return ToolResult::fail("push_frame", "OUTPUT_ERROR",
                                    "push frame sink is not configured");
        }
        std::string error;
        if (!ctx.push_frame_sink(summary, error)) {
            return ToolResult::fail("push_frame", "OUTPUT_ERROR",
                                    error.empty() ? "push frame failed" : error);
        }
        return ToolResult::ok("push_frame", nlohmann::json{{"pushed", true},
                                                             {"summary", summary}});
    });

    return reg;
}

}  // namespace agent
