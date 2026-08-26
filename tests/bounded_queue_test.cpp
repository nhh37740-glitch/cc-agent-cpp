// Phase 5 验收：BoundedQueue。
// 1) 容量不超限 + drop_oldest 淘汰顺序正确；
// 2) 生产/消费真实并行，顺序保持，drop 计数一致；
// 3) stop_token 立即唤醒阻塞的消费者；
// 4) close 后排空剩余元素再返回 false。

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include "queue/bounded_queue.hpp"

static int failures = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "[FAIL] %s (line %d)\n", msg, __LINE__); \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

int main() {
    using queue::BoundedQueue;

    // ---- 1) 容量与 drop_oldest 顺序 ----
    {
        BoundedQueue<int> q(4);
        for (int i = 0; i < 10; ++i) q.push(i);  // 6 7 8 9 应保留
        CHECK(q.size() == 4, "size 不应超过 capacity");
        CHECK(q.dropped_full() == 6, "应淘汰 6 个最旧元素");
        q.close();
        int v = -1;
        std::vector<int> got;
        while (q.pop(v)) got.push_back(v);
        CHECK((got == std::vector<int>{6, 7, 8, 9}), "保留的应是最新 4 个且顺序不变");
    }

    // ---- 2) 并行生产/消费：消费者慢，队列始终有界 ----
    {
        BoundedQueue<int> q(8);
        std::atomic<bool> overflow{false};
        constexpr int kTotal = 2000;
        std::jthread producer([&] {
            for (int i = 0; i < kTotal; ++i) {
                q.push(i);
                if (q.size() > 8) overflow = true;
            }
            q.close();
        });
        int last = -1;
        bool ordered = true;
        int64_t count = 0;
        int v;
        while (q.pop(v)) {
            if (v <= last) ordered = false;
            last = v;
            ++count;
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        producer.join();
        CHECK(!overflow.load(), "队列长度不应超过容量");
        CHECK(ordered, "交付顺序应保持 FIFO");
        CHECK(count > 0 && count <= kTotal, "消费数应在合理范围");
        CHECK(q.received() == static_cast<uint64_t>(count), "received 计数与实际一致");
        CHECK(static_cast<uint64_t>(kTotal) - q.received() == q.dropped_full(),
              "未交付数量应等于 drop 计数");
        std::fprintf(stderr,
                     "parallel: received=%llu dropped_full=%llu\n",
                     (unsigned long long)q.received(),
                     (unsigned long long)q.dropped_full());
    }

    // ---- 3) stop_token 唤醒阻塞消费者 ----
    {
        BoundedQueue<int> q(4);
        std::jthread consumer([&](std::stop_token st) {
            int v;
            bool r = q.pop(v, st);  // 队列为空，将阻塞
            CHECK(!r, "stop 后 pop 应返回 false");
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto t0 = std::chrono::steady_clock::now();
        consumer.request_stop();
        consumer.join();
        const double wake_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                .count();
        CHECK(wake_ms < 500.0, "stop 应立即唤醒（<500ms）");
    }

    // ---- 4) close 排空 ----
    {
        BoundedQueue<int> q(8);
        q.push(1);
        q.push(2);
        q.close();
        CHECK(!q.push(3), "close 后 push 应回绝");
        int v;
        CHECK(q.pop(v) && v == 1, "close 后仍可排空已有元素");
        CHECK(q.pop(v) && v == 2, "排空第二个");
        CHECK(!q.pop(v), "排空后 pop 返回 false");
    }

    if (failures == 0) {
        std::fprintf(stderr, "[PASS] bounded_queue_test\n");
        return 0;
    }
    return 1;
}
