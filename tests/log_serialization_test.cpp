// 日志并发验收：运行事件与 frame_pushed 并发追加同一 JSONL 文件。
// 两个线程各写 10000 行，
// 结束后每一行必须可独立 JSON 解析、总行数与各 type 计数精确。

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "log.hpp"

static int failures = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "[FAIL] %s (line %d)\n", msg, __LINE__); \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

int main() {
    const std::string log_path =
        (std::filesystem::temp_directory_path() / "edge_agent_test" /
         "concurrent_events.jsonl")
            .string();
    std::filesystem::create_directories(
        std::filesystem::path(log_path).parent_path());
    std::filesystem::remove(log_path);

    logging::JsonlLogger logger(log_path);

    constexpr int kRunLines = 10000;   // 直接写运行事件
    constexpr int kSaveLines = 10000;  // 写关键帧推送事件

    std::jthread runner([&](std::stop_token st) {
        for (int i = 0; i < kRunLines && !st.stop_requested(); ++i) {
            logger.event("run_event", {{"i", i}});
        }
    });
    std::jthread saver([&](std::stop_token st) {
        for (int i = 0; i < kSaveLines && !st.stop_requested(); ++i) {
            logger.event("frame_pushed", {{"i", i}});
        }
    });
    runner.join();
    saver.join();

    // 校验文件
    std::ifstream file(log_path);
    int64_t total = 0, runs = 0, saves = 0, bad = 0;
    std::string line;
    while (std::getline(file, line)) {
        ++total;
        auto j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded()) {
            ++bad;
            continue;
        }
        const std::string type = j.value("type", "");
        if (type == "run_event") ++runs;
        else if (type == "frame_pushed") ++saves;
    }
    std::fprintf(stderr,
                 "total=%lld run=%lld saved=%lld malformed=%lld\n",
                 (long long)total, (long long)runs, (long long)saves, (long long)bad);
    CHECK(bad == 0, "所有行必须是完整可解析的 JSON");
    CHECK(total == kRunLines + kSaveLines, "行数应精确等于写入总数");
    CHECK(runs == kRunLines && saves == kSaveLines, "各 type 计数应精确");

    if (failures == 0) {
        std::fprintf(stderr, "[PASS] log_serialization_test\n");
        return 0;
    }
    return 1;
}
