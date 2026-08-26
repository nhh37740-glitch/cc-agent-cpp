#pragma once

// 第一版四个内置工具的构造：notify、save_event、get_time、speak。
// 依赖经 ToolContext 注入，便于测试失败路径（如输出写入失败）。

#include <filesystem>
#include <functional>
#include <string>

#include "agent/tool_registry.hpp"

namespace agent {

struct ToolContext {
    // notify/speak 的输出口；返回 false 模拟写入失败。
    // 默认实现：打印到 stdout（[NOTIFY] ... / [SPEAK] ...）。
    std::function<bool(const std::string& line)> notify_sink;
    std::function<bool(const std::string& line)> speak_sink;

    // save_event 写入目标（JSONL 追加）。
    std::filesystem::path events_log = "logs/events.jsonl";

    // get_time 的时钟；默认系统本地时间 ISO8601。
    std::function<std::string()> clock;
};

ToolRegistry make_default_registry(ToolContext ctx);

}  // namespace agent
