#pragma once

// JSONL 日志：所有运行期事件追加写入 logs/events.jsonl。
// 线程安全：内部互斥锁保护文件句柄；每条记录一行 JSON。

#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace logging {

// 返回自进程启动以来的秒数（单调时钟），作为事件时间戳。
inline double monotonic_seconds() {
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

// 简单的 JSONL 追加写器。flush_every=true 时逐条落盘，便于演示时实时查看。
class JsonlLogger {
public:
    explicit JsonlLogger(const std::filesystem::path& file, bool flush_every = true)
        : file_(file, std::ios::app), flush_every_(flush_every) {
        if (file.has_parent_path()) {
            std::filesystem::create_directories(file.parent_path());
            file_.close();
            file_.open(file, std::ios::app);
        }
    }

    // 写入一条事件。fields 为事件的附加字段（不含 type/timestamp）。
    void event(const std::string& type, nlohmann::ordered_json fields = {}) {
        nlohmann::ordered_json record;
        record["type"] = type;
        record["timestamp"] = round(monotonic_seconds());
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            record[it.key()] = it.value();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!file_.is_open()) return;
        file_ << record.dump() << '\n';
        if (flush_every_) file_.flush();
    }

    bool is_open() const { return file_.is_open(); }

private:
    static double round(double v) { return std::floor(v * 1000.0) / 1000.0; }

    std::ofstream file_;
    bool flush_every_;
    std::mutex mutex_;
};

}  // namespace logging
