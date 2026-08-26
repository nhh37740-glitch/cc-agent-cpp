#pragma once

// Skill 系统：从 Markdown 文件加载任务定义，与 C++ 代码解耦。
// 更换 Skill 不需要重新编译；但 Skill 声明的工具权限不可信，
// 真正的权限由 C++ ToolRegistry 白名单强制执行。

#include <string>
#include <vector>

namespace skill {

struct Skill {
    std::string name;     // 取自首个 "# 标题"，无标题则用文件名
    std::string content;  // Markdown 全文（去除 BOM 与行尾 \r）
};

// 读取并解析 Skill 文件；失败返回 false 并填充 error。
bool load_skill(const std::string& path, Skill& out, std::string& error);

// 组合系统提示：Skill 内容 + 工具清单 + 结构化输出协议。
std::string build_system_prompt(const Skill& skill,
                                const std::vector<std::string>& allowed_tools);

}  // namespace skill
