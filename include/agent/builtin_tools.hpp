#pragma once

// MVP 唯一工具 push_frame：把当前关键帧及摘要推送到远程面板。

#include <functional>
#include <string>

#include "agent/tool_registry.hpp"

namespace agent {

struct ToolContext {
    // 返回 false 时 error 给出可记录的失败原因。
    std::function<bool(const std::string& summary, std::string& error)> push_frame_sink;
};

ToolRegistry make_default_registry(ToolContext ctx);

}  // namespace agent
