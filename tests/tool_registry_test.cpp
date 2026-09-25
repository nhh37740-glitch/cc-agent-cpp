// 唯一工具 push_frame 的白名单、参数与失败路径验收。
#include <cstdio>
#include <string>

#include "agent/builtin_tools.hpp"

static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { std::fprintf(stderr, "[FAIL] %s (line %d)\n", m, __LINE__); ++failures; } } while (0)

int main() {
    int pushes = 0;
    std::string last_summary;
    bool fail = false;
    agent::ToolContext ctx;
    ctx.push_frame_sink = [&](const std::string& summary, std::string& error) {
        if (fail) { error = "dashboard unavailable"; return false; }
        ++pushes; last_summary = summary; return true;
    };
    auto reg = agent::make_default_registry(ctx);
    CHECK(reg.names().size() == 1 && reg.has("push_frame"), "应只注册 push_frame");
    auto ok = reg.execute("push_frame", {{"summary", "门口有包裹"}});
    CHECK(ok.success && pushes == 1 && last_summary == "门口有包裹", "push_frame 成功");
    CHECK(reg.execute("push_frame", {}).error_code == "INVALID_ARGUMENTS", "缺参数失败");
    CHECK(reg.execute("push_frame", {{"summary", 1}}).error_code == "INVALID_ARGUMENTS", "类型错误失败");
    CHECK(reg.execute("notify", {{"text", "x"}}).error_code == "TOOL_NOT_ALLOWED", "旧工具不在白名单");
    fail = true;
    CHECK(reg.execute("push_frame", {{"summary", "x"}}).error_code == "OUTPUT_ERROR", "下游失败可见");
    if (!failures) { std::fprintf(stderr, "[PASS] tool_registry_test\n"); return 0; }
    return 1;
}
