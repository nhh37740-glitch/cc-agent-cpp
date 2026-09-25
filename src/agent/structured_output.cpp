#include "agent/structured_output.hpp"

#include <algorithm>
#include <cctype>

namespace agent {

namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}

// 剥掉恰好一对完整的 Markdown 围栏；不是围栏形态时原样返回。
std::string strip_code_fence(const std::string& s) {
    if (s.rfind("```", 0) != 0) return s;  // 必须以 ``` 开头
    size_t body_start = 3;
    // 允许语言标注（如 ```json），到行尾为止
    const size_t nl = s.find('\n', 3);
    if (nl == std::string::npos) return s;
    body_start = nl + 1;
    // 必须以 ``` 结尾（允许尾随空白已由调用方去除）
    if (s.compare(s.size() - 3, 3, "```") != 0) return s;
    return trim(s.substr(body_start, s.size() - 3 - body_start));
}

ParsedOutput parse_single_object(const std::string& json_str, ParsedOutput& out) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_str);  // 要求全部输入被消费
    } catch (const std::exception&) {
        out.error = "NOT_A_SINGLE_JSON_OBJECT";
        return out;
    }
    if (!j.is_object()) {
        out.error = "NOT_AN_OBJECT";
        return out;
    }
    if (!j.contains("type") || !j["type"].is_string()) {
        out.error = "MISSING_OR_INVALID_TYPE";
        return out;
    }
    const std::string type = j["type"].get<std::string>();

    if (type == "final") {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.key() != "type" && it.key() != "content") {
                out.error = "UNKNOWN_FIELD:" + it.key();
                return out;
            }
        }
        if (!j.contains("content") || !j["content"].is_string()) {
            out.error = "MISSING_OR_INVALID_CONTENT";
            return out;
        }
        out.type = OutputType::Final;
        out.content = j["content"].get<std::string>();
        return out;
    }

    if (type == "tool_call") {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.key() != "type" && it.key() != "name" && it.key() != "arguments") {
                out.error = "UNKNOWN_FIELD:" + it.key();
                return out;
            }
        }
        if (!j.contains("name") || !j["name"].is_string()) {
            out.error = "MISSING_OR_INVALID_NAME";
            return out;
        }
        if (j.contains("arguments") && !j["arguments"].is_object()) {
            out.error = "INVALID_ARGUMENTS_TYPE";
            return out;
        }
        out.type = OutputType::ToolCall;
        out.tool_name = j["name"].get<std::string>();
        out.arguments = j.value("arguments", nlohmann::json::object());
        return out;
    }

    out.error = "UNKNOWN_TYPE:" + type;
    return out;
}

}  // namespace

ParsedOutput parse_model_output(const std::string& raw_text) {
    ParsedOutput out;

    std::string text = trim(raw_text);
    if (text.empty()) {
        out.error = "EMPTY_OUTPUT";
        return out;
    }

    // 唯一宽容项：完整输入恰为一对围栏，且围栏内恰好一个对象
    if (text.rfind("```", 0) == 0) {
        text = strip_code_fence(text);
        if (text.empty() || text.rfind("```", 0) == 0 ||
            text.compare(text.size() - 3, 3, "```") == 0) {
            out.error = "MALFORMED_CODE_FENCE";
            return out;
        }
        text = trim(text);
    }

    return parse_single_object(text, out);
}

}  // namespace agent
