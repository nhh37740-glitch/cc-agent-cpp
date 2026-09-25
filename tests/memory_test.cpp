// 长时间运行内存稳定性验收（视频管线，无模型）：
// - 视频生产者持续产出候选帧，消费者慢速取用（制造队列溢出淘汰）；
// - 全程采样进程工作集内存：不随帧数增长；
// - 结束后两个 Worker 正常退出。

#include <chrono>
#include <cstdio>
#include <random>
#include <thread>
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

#include "app/pipeline.hpp"

static int failures = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "[FAIL] %s (line %d)\n", msg, __LINE__); \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

static size_t rss_mb() {
    PROCESS_MEMORY_COUNTERS pmc{};
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    return pmc.WorkingSetSize / (1024 * 1024);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: memory_test <fixture.mp4>\n");
        return 2;
    }

    logging::JsonlLogger logger("logs/events.jsonl");
    app::PipelineConfig cfg;
    cfg.video_path = argv[1];
    cfg.realtime_pacing = false;  // 加速测试；内存行为与 pacing 无关
    cfg.filter.sample_interval_ms = 500;
    cfg.filter.change_threshold = 0.005f;  // 故意放宽：制造大量候选与队列溢出淘汰
    cfg.queue_capacity = 4;                // 用最小容量加大淘汰压力

    app::PipelineStats stats;
    queue::BoundedQueue<video::CandidateFrame> q(cfg.queue_capacity);

    // 消费者：慢速出队，故意让队列经常处于满状态
    std::jthread consumer([&](std::stop_token st) {
        video::CandidateFrame c;
        while (!st.stop_requested() && q.pop(c, st)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    });

    std::jthread producer(app::video_worker, std::cref(cfg), std::ref(logger),
                          std::ref(q), std::ref(stats));

    // 生产期间周期性采样内存；连续 5 次帧数无增长且队列空则认为结束
    size_t min_mb = SIZE_MAX, max_mb = 0;
    int64_t last_frames = -1;
    int stable_rounds = 0;
    const int kMaxRounds = 600;  // 最多 60 秒
    for (int round = 0; round < kMaxRounds && stable_rounds < 5; ++round) {
        const int64_t now = stats.frames_read.load();
        if (now == last_frames && q.size() == 0) {
            ++stable_rounds;
        } else {
            stable_rounds = 0;
        }
        last_frames = now;
        const size_t m = rss_mb();
        min_mb = std::min(min_mb, m);
        max_mb = std::max(max_mb, m);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    producer.join();
    q.close();
    consumer.request_stop();
    consumer.join();

    CHECK(!stats.video_failed.load(), "视频源不应报错");

    const size_t end_mb = rss_mb();
    std::fprintf(stderr,
                 "frames=%lld pushed=%lld dropped=%lld analyzed=%lld mem[min,max,end]=[%zu,%zu,%zu] MB\n",
                 (long long)stats.frames_read.load(),
                 (long long)stats.candidates_pushed.load(),
                 (long long)q.dropped_full(),
                 (long long)stats.candidates_analyzed.load(),
                 min_mb, max_mb, end_mb);

    CHECK(stats.frames_read.load() > 600, "30s@30fps 应读到大量帧");
    CHECK(stats.candidates_pushed.load() > 50, "低阈值下应产生大量候选");
    CHECK(q.dropped_full() > 0, "消费者慢时应发生 drop_oldest");
    CHECK(max_mb - min_mb < 150, "工作集波动不应超过 150MB");
    CHECK(end_mb <= min_mb + 50 || end_mb <= max_mb,
          "结束时内存不应显著高于运行区间");

    if (failures == 0) {
        std::fprintf(stderr, "[PASS] memory_test\n");
        return 0;
    }
    return 1;
}
