// Phase 8（ToolRegistry 部分）验收：
// - 4 个工具注册成功；
// - 正确工具执行；白名单外工具拒绝（TOOL_NOT_ALLOWED）且不执行；
// - 非法参数（缺失/类型错误）返回 INVALID_ARGUMENTS；
// - 失败路径可注入（OUTPUT_ERROR）；
// - save_event 产生可解析的 JSONL 记录。

#include <cstdio>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "agent/builtin_tools.hpp"

static int failures = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "[FAIL] %s (line %d)\n", msg, __LINE__); \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

int main() {
    const std::string log_path =
        (std::filesystem::temp_directory_path() / "edge_agent_test" / "events.jsonl").string();
    std::filesystem::remove(log_path);

    agent::ToolContext ctx;
    ctx.events_log = log_path;

    std::vector<std::string> notified;
    ctx.notify_sink = [&](const std::string& line) {
        notified.push_back(line);
        return true;
    };
    bool speak_fail = false;
    ctx.speak_sink = [&](const std::string& line) { return !speak_fail; };

    const std::string fixed_time = "2026-08-26T18:00:00+0800";
    ctx.clock = [&] { return fixed_time; };

    agent::ToolRegistry reg = agent::make_default_registry(ctx);

    // 注册数量与名称
    CHECK(reg.names().size() == 4, "应恰好注册 4 个工具");
    for (const char* n : {"notify", "save_event", "get_time", "speak"}) {
        CHECK(reg.has(n), (std::string("缺少工具: ") + n).c_str());
    }

    // notify 成功
    {
        auto r = reg.execute("notify", nlohmann::json{{"text", "门口出现包裹"}});
        CHECK(r.success, "notify 应成功");
        CHECK(notified.size() == 1 && notified[0] == "[NOTIFY] 门口出现包裹",
              "notify 输出内容不正确");
    }

    // notify 参数错误
    CHECK(!reg.execute("notify", nlohmann::json{}).success, "缺参数应失败");
    CHECK(reg.execute("notify", nlohmann::json{{"text", 123}}).error_code ==
              "INVALID_ARGUMENTS",
          "类型错误应为 INVALID_ARGUMENTS");

    // 白名单外工具：拒绝且不执行任何副作用
    {
        auto r = reg.execute("delete_file", nlohmann::json{{"path", "C:/"}});
        CHECK(!r.success && r.error_code == "TOOL_NOT_ALLOWED",
              "白名单外应 TOOL_NOT_ALLOWED");
    }

    // get_time
    {
        auto r = reg.execute("get_time", nlohmann::json::object());
        CHECK(r.success && r.data["time"] == fixed_time, "get_time 应返回注入时间");
    }

    // save_event 成功并写 JSONL
    {
        auto r = reg.execute(
            "save_event",
            nlohmann::json{{"event", "package_detected"}, {"description", "A package appeared"}});
        CHECK(r.success, "save_event 应成功");
        std::ifstream file(log_path);
        std::string line;
        CHECK((bool)std::getline(file, line), "应有 JSONL 行写入");
        auto j = nlohmann::json::parse(line);
        CHECK(j["event"] == "package_detected" && j["timestamp"] == fixed_time,
              "记录字段应完整");
    }
    // save_event 参数错误
    CHECK(reg.execute("save_event", nlohmann::json{{"event", "x"}}).error_code ==
              "INVALID_ARGUMENTS",
          "save_event 缺 description 应失败");

    // speak 失败路径
    {
        speak_fail = true;
        auto r = reg.execute("speak", nlohmann::json{{"text", "hello"}});
        CHECK(!r.success && r.error_code == "OUTPUT_ERROR", "输出失败应 OUTPUT_ERROR");
        speak_fail = false;
        CHECK(reg.execute("speak", nlohmann::json{{"text", "hello"}}).success,
              "恢复后 speak 应成功");
    }

    // ToolResult 序列化形态
    {
        auto r = reg.execute("notify", nlohmann::json{{"text", "t"}});
        auto j = r.to_json();
        CHECK(j["type"] == "tool_result" && j["tool"] == "notify" && j["success"] == true,
              "to_json 形态正确");
        auto bad = reg.execute("hack", nlohmann::json::object());
        auto jb = bad.to_json();
        CHECK(jb["error"]["code"] == "TOOL_NOT_ALLOWED", "失败 to_json 含 error.code");
    }

    if (failures == 0) {
        std::fprintf(stderr, "[PASS] tool_registry_test\n");
        return 0;
    }
    return 1;
}
