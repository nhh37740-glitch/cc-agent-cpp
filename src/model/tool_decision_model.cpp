#include "model/tool_decision_model.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include <nlohmann/json.hpp>

namespace model {
namespace {
std::string trim(std::string s) {
    const auto nonspace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), nonspace));
    s.erase(std::find_if(s.rbegin(), s.rend(), nonspace).base(), s.end());
    return s;
}

std::string checklist_from(const std::string& user_text) {
    std::string out = user_text;
    constexpr const char* prefix = "Visual checklist:\n";
    if (out.rfind(prefix, 0) == 0) out.erase(0, std::char_traits<char>::length(prefix));
    const auto end = out.find("\nApply the Skill");
    if (end != std::string::npos) out.erase(end);
    return trim(std::move(out));
}
}

ModelResponse ToolDecisionModel::generate(const std::vector<Message>& messages,
                                          const uint8_t*, int, int) {
    // 工具结果已回填：生成一个确定的 final，完成 Agent Loop。
    if (std::any_of(messages.begin(), messages.end(),
                    [](const Message& m) { return m.role == Role::Tool; })) {
        return {R"({"type":"final","content":"push_frame completed"})", 0};
    }

    std::string evidence;
    constexpr const char* checklist_prefix = "Visual checklist:\n";
    
    // 优先从包含 "Visual checklist:\n" 前缀的用户消息中提取证据
    for (const auto& message : messages) {
        if (message.role == Role::User && message.text.rfind(checklist_prefix, 0) == 0) {
            evidence = checklist_from(message.text);
            break;
        }
    }
    
    // 如果没有找到，从最后一个用户消息中提取
    if (evidence.empty()) {
        for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
            if (it->role == Role::User) { 
                evidence = checklist_from(it->text); 
                break; 
            }
        }
    }
    
    // 如果仍然为空，使用所有用户消息的文本作为证据
    if (evidence.empty()) {
        for (const auto& message : messages) {
            if (message.role == Role::User && !message.text.empty()) {
                evidence = message.text;
                break;
            }
        }
    }

    // 用户需求：有新内容就推送，不依赖 Skill 规则判断。
    // 只要视觉描述非空且不是空场景，直接生成 push_frame 工具调用。
    if (!evidence.empty()) {
        std::string lower = evidence;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool empty_scene = lower.find("scene: empty") != std::string::npos &&
                           lower.find("scene: empty") == lower.find("empty");
        if (!empty_scene) {
            nlohmann::ordered_json j = {{"type", "tool_call"}, {"name", "push_frame"},
                                        {"arguments", {{"summary", evidence}}}};
            return {j.dump(), 0};
        }
    }
    nlohmann::ordered_json j = {{"type", "final"},
                                {"content", "No visible content detected."}};
    return {j.dump(), 0};
}

}  // namespace model
