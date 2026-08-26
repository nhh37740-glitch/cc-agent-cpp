#include "agent/builtin_tools.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace agent {

static std::string default_iso8601_now() {
    using clock = std::chrono::system_clock;
    const std::time_t t = clock::to_time_t(clock::now());
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S%z");
    return ss.str();
}

// 参数校验：要求对象中存在 string 类型的键
static bool get_string_arg(const nlohmann::json& args, const char* key, std::string& out) {
    if (!args.is_object() || !args.contains(key) || !args.at(key).is_string()) return false;
    out = args.at(key).get<std::string>();
    return true;
}

ToolRegistry make_default_registry(ToolContext ctx) {
    if (!ctx.notify_sink) {
        ctx.notify_sink = [](const std::string& line) {
            return std::printf("%s\n", line.c_str()) >= 0;
        };
    }
    if (!ctx.speak_sink) {
        ctx.speak_sink = [](const std::string& line) {
            return std::printf("%s\n", line.c_str()) >= 0;
        };
    }
    if (!ctx.clock) {
        ctx.clock = default_iso8601_now;
    }

    ToolRegistry reg;

    reg.register_tool("notify", [ctx](const nlohmann::json& args) {
        std::string text;
        if (!get_string_arg(args, "text", text)) {
            return ToolResult::fail("notify", "INVALID_ARGUMENTS",
                                    "arguments must contain string field 'text'");
        }
        if (!ctx.notify_sink("[NOTIFY] " + text)) {
            return ToolResult::fail("notify", "OUTPUT_ERROR", "Failed to write to stdout");
        }
        return ToolResult::ok("notify", nlohmann::json{{"message", "Notification printed"}});
    });

    reg.register_tool("speak", [ctx](const nlohmann::json& args) {
        std::string text;
        if (!get_string_arg(args, "text", text)) {
            return ToolResult::fail("speak", "INVALID_ARGUMENTS",
                                    "arguments must contain string field 'text'");
        }
        if (!ctx.speak_sink("[SPEAK] " + text)) {
            return ToolResult::fail("speak", "OUTPUT_ERROR", "Failed to write to stdout");
        }
        return ToolResult::ok("speak", nlohmann::json{{"message", "Spoken"}});
    });

    reg.register_tool("save_event", [ctx](const nlohmann::json& args) {
        std::string event, description;
        if (!get_string_arg(args, "event", event) ||
            !get_string_arg(args, "description", description)) {
            return ToolResult::fail("save_event", "INVALID_ARGUMENTS",
                                    "arguments must contain string fields "
                                    "'event' and 'description'");
        }
        nlohmann::ordered_json record;
        record["timestamp"] = ctx.clock();
        record["event"] = event;
        record["description"] = description;

        // 写入失败（目录不可写等）以 OUTPUT_ERROR 表达
        try {
            if (ctx.events_log.has_parent_path()) {
                std::filesystem::create_directories(ctx.events_log.parent_path());
            }
            std::ofstream file(ctx.events_log, std::ios::app);
            if (!file.is_open()) {
                return ToolResult::fail("save_event", "OUTPUT_ERROR",
                                        "Failed to open events log");
            }
            file << record.dump() << '\n';
            if (!file.good()) {
                return ToolResult::fail("save_event", "OUTPUT_ERROR",
                                        "Failed to write events log");
            }
        } catch (const std::exception& e) {
            return ToolResult::fail("save_event", "OUTPUT_ERROR", e.what());
        }
        return ToolResult::ok("save_event", nlohmann::json{{"saved", true}});
    });

    reg.register_tool("get_time", [ctx](const nlohmann::json&) {
        return ToolResult::ok("get_time", nlohmann::json{{"time", ctx.clock()}});
    });

    return reg;
}

}  // namespace agent
