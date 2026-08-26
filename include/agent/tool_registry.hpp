#pragma once

// 工具注册表：C++ 强制白名单。
// Skill 文件声明的权限不可信；模型只能调用这里注册过的工具。

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "agent/tool_result.hpp"

namespace agent {

using ToolFn = std::function<ToolResult(const nlohmann::json& arguments)>;

class ToolRegistry {
public:
    void register_tool(std::string name, ToolFn fn) {
        tools_[std::move(name)] = std::move(fn);
    }

    bool has(const std::string& name) const { return tools_.count(name) > 0; }

    std::vector<std::string> names() const {
        std::vector<std::string> out;
        out.reserve(tools_.size());
        for (const auto& [k, v] : tools_) out.push_back(k);
        return out;
    }

    // 白名单外的工具一律拒绝，绝不执行；错误同样以 ToolResult 表达。
    ToolResult execute(const std::string& name, const nlohmann::json& arguments) const {
        auto it = tools_.find(name);
        if (it == tools_.end()) {
            return ToolResult::fail(name, "TOOL_NOT_ALLOWED",
                                    "Tool is not in the whitelist");
        }
        return it->second(arguments);
    }

private:
    std::unordered_map<std::string, ToolFn> tools_;
};

}  // namespace agent
