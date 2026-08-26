#include "agent/structured_output.hpp"

namespace agent {

std::string extract_json_object(const std::string& text) {
    // 找到第一个 '{'，然后按括号深度与字符串/转义状态找配对的 '}'
    const size_t start = text.find('{');
    if (start == std::string::npos) return "";
    int depth = 0;
    bool in_string = false, escaped = false;
    for (size_t i = start; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) return text.substr(start, i - start + 1);
        }
    }
    return "";  // 括号不平衡
}

ParsedOutput parse_model_output(const std::string& raw_text) {
    ParsedOutput out;

    const std::string json_str = extract_json_object(raw_text);
    if (json_str.empty()) {
        out.error = "NO_JSON_OBJECT";
        return out;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_str);
    } catch (const std::exception&) {
        out.error = "JSON_PARSE_ERROR";
        return out;
    }

    if (!j.is_object()) {
        out.error = "NOT_AN_OBJECT";
        return out;
    }

    // type 字段必须存在且为字符串
    if (!j.contains("type") || !j["type"].is_string()) {
        out.error = "MISSING_OR_INVALID_TYPE";
        return out;
    }
    const std::string type = j["type"].get<std::string>();

    if (type == "final") {
        // 允许键：type、content
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
        // 允许键：type、name、arguments
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

}  // namespace agent
