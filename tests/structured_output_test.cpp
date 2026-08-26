// Phase 8（结构化输出部分）验收：
// 非法 JSON、缺失字段、未知字段、类型错误、围栏包装均安全处理。

#include <cstdio>

#include "agent/structured_output.hpp"

using agent::OutputType;
using agent::parse_model_output;

static int failures = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "[FAIL] %s (line %d)\n", msg, __LINE__); \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

int main() {
    // 合法 tool_call
    {
        auto r = parse_model_output(
            R"({"type":"tool_call","name":"notify","arguments":{"text":"hi"}})");
        CHECK(r.type == OutputType::ToolCall, "tool_call 应解析成功");
        CHECK(r.tool_name == "notify", "工具名应为 notify");
        CHECK(r.arguments["text"] == "hi", "参数应保留");
    }

    // 合法 final
    {
        auto r = parse_model_output(R"({"type":"final","content":"nothing to do"})");
        CHECK(r.type == OutputType::Final, "final 应解析成功");
        CHECK(r.content == "nothing to do", "content 应保留");
    }

    // Markdown 围栏包装 + 前后噪声
    {
        auto r = parse_model_output("Here you go:\n```json\n"
                                    "{\"type\":\"final\",\"content\":\"ok\"}\n```\ndone");
        CHECK(r.type == OutputType::Final && r.content == "ok", "应剥离围栏包装");
    }

    // arguments 缺省 → 空对象
    {
        auto r = parse_model_output(R"({"type":"tool_call","name":"get_time"})");
        CHECK(r.type == OutputType::ToolCall, "无参数 tool_call 应合法");
        CHECK(r.arguments.is_object(), "缺省 arguments 应为空对象");
    }

    // 非法：完全不是 JSON
    CHECK(parse_model_output("I think we should call the police.").error == "NO_JSON_OBJECT",
          "纯文本应返回 NO_JSON_OBJECT");

    // 非法：括号不平衡
    CHECK(parse_model_output("{\"type\":\"final\",\"content\":\"x\"").error == "NO_JSON_OBJECT",
          "括号不平衡应失败");

    // 非法：JSON 语法错误
    CHECK(parse_model_output("{\"type\": }").error == "JSON_PARSE_ERROR",
          "语法错误应返回 JSON_PARSE_ERROR");

    // 非法：数组而非对象（无 '{'，直接 NO_JSON_OBJECT）
    CHECK(parse_model_output("[1,2,3]").error == "NO_JSON_OBJECT", "数组应拒绝");

    // 非法：缺少 type
    CHECK(parse_model_output(R"({"name":"notify"})").error == "MISSING_OR_INVALID_TYPE",
          "缺 type 应失败");

    // 非法：未知 type
    CHECK(parse_model_output(R"({"type":"explode"})").error == "UNKNOWN_TYPE:explode",
          "未知 type 应失败");

    // 非法：tool_call 缺 name
    CHECK(parse_model_output(R"({"type":"tool_call","arguments":{}})").error ==
              "MISSING_OR_INVALID_NAME",
          "缺 name 应失败");

    // 非法：arguments 类型错误
    CHECK(parse_model_output(R"({"type":"tool_call","name":"notify","arguments":[1]})")
              .error == "INVALID_ARGUMENTS_TYPE",
          "arguments 非对象应失败");

    // 非法：final 缺 content
    CHECK(parse_model_output(R"({"type":"final"})").error == "MISSING_OR_INVALID_CONTENT",
          "final 缺 content 应失败");

    // 非法：未知字段
    CHECK(parse_model_output(R"({"type":"final","content":"x","evil":true})").error ==
              "UNKNOWN_FIELD:evil",
          "未知字段应失败");

    if (failures == 0) {
        std::fprintf(stderr, "[PASS] structured_output_test\n");
        return 0;
    }
    return 1;
}
