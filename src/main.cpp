// Phase 1 入口：验证骨架可编译可运行，初始化 JSONL 日志。
// 后续 Phase 会逐步扩展为完整管线入口。

#include <windows.h>
#include <cstdio>

#include "log.hpp"

int main(int argc, char** argv) {
    std::filesystem::path log_path = "logs/events.jsonl";
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--log" ) log_path = argv[i + 1];
    }
    logging::JsonlLogger logger(log_path);
    if (!logger.is_open()) {
        std::fprintf(stderr, "[FATAL] 无法打开日志文件: %s\n", log_path.string().c_str());
        return 1;
    }
    logger.event("startup", {{"phase", 1}, {"pid", (int)GetCurrentProcessId()}});
    std::printf("edge_agent skeleton OK; log=%s\n", log_path.string().c_str());
    return 0;
}
