#include "skill/skill_loader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace skill {

static std::string strip_bom(std::string s) {
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF) {
        s.erase(0, 3);
    }
    return s;
}

bool load_skill(const std::string& path, Skill& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "无法打开 Skill 文件: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string text = strip_bom(ss.str());

    // 统一换行，避免 CRLF 进入提示
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());

    // 名称取首个 "# " 标题；否则用文件名（不含扩展名）
    size_t name_end = std::string::npos;
    size_t name_start = std::string::npos;
    for (size_t pos = text.find('#'); pos != std::string::npos; pos = text.find('#', pos + 1)) {
        if (pos + 1 < text.size() && text[pos + 1] == ' ') {
            name_start = pos + 2;
            name_end = text.find('\n', name_start);
            break;
        }
    }
    if (name_start != std::string::npos && name_end != std::string::npos) {
        out.name = text.substr(name_start, name_end - name_start);
    } else {
        out.name = std::filesystem::path(path).stem().string();
    }
    out.content = text;
    return true;
}

std::string build_system_prompt(const Skill& skill,
                                const std::vector<std::string>& allowed_tools) {
    std::ostringstream ss;
    ss << "You are a local video monitoring agent running on an edge device.\n\n";
    ss << "# Your task (skill)\n" << skill.content << "\n\n";

    ss << "# Available tools\n";
    for (const auto& t : allowed_tools) ss << "- " << t << "\n";
    ss << "\n";

    ss << "# Output protocol (mandatory)\n";
    ss << "Analyze the given image. Respond with EXACTLY ONE JSON object and nothing "
          "else. No markdown, no code fences, no extra words.\n";
    ss << "The JSON object must have this exact schema:\n";
    ss << "- To call a tool: {\"type\":\"tool_call\",\"name\":\"TOOL_NAME\","
          "\"arguments\":{...}}\n";
    ss << "- If no action is required: {\"type\":\"final\",\"content\":\"short reason\"}\n";
    ss << "Every response must contain the key \"type\" whose value is either "
          "\"tool_call\" or \"final\".\n\n";
    ss << "Example correct responses:\n"
       << "{\"type\":\"tool_call\",\"name\":\"notify\","
          "\"arguments\":{\"text\":\"A package appeared near the door\"}}\n"
       << "{\"type\":\"final\",\"content\":\"Nothing notable in this frame.\"}\n";
    return ss.str();
}

}  // namespace skill
