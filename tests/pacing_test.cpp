// Phase 3 验收：realtime pacing。
// 10 秒视频按 PTS 节奏输出应耗时约 10 秒（容差 [9.2, 12.0]），
// 而非 pacing 模式应在 2 秒内完成。

#include <chrono>
#include <cstdio>
#include <string>

#include "video/ffmpeg_video_source.hpp"
#include "video/realtime_pacing_source.hpp"

static int failures = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "[FAIL] %s (line %d)\n", msg, __LINE__); \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: pacing_test <fixture.mp4 (10s)>\n");
        return 2;
    }
    const std::string path = argv[1];

    // 无 pacing：应远快于实时
    {
        const auto t0 = std::chrono::steady_clock::now();
        video::FFmpegFileSource source(path.c_str());
        video::Frame f;
        int64_t n = 0;
        while (source.read(f)) ++n;
        const double secs =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "no-pacing: frames=%lld secs=%.2f\n", (long long)n, secs);
        CHECK(n > 150, "10 秒 30fps 应有大量帧");
        CHECK(secs < 2.0, "无 pacing 解码应在 2 秒内完成");
    }

    // realtime pacing：墙钟时长应约等于视频时长
    double paced_secs = 0.0;
    int64_t paced_frames = 0;
    {
        const auto t0 = std::chrono::steady_clock::now();
        video::RealtimePacingSource source(path.c_str());
        video::Frame f;
        while (source.read(f)) ++paced_frames;
        paced_secs =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    std::fprintf(stderr, "pacing: frames=%lld secs=%.2f\n",
                 (long long)paced_frames, paced_secs);
    CHECK(paced_frames >= 280, "pacing 不应丢帧");
    CHECK(paced_secs >= 9.2 && paced_secs <= 12.0, "pacing 墙钟时长应接近 10 秒");

    if (failures == 0) {
        std::fprintf(stderr, "[PASS] pacing_test\n");
        return 0;
    }
    return 1;
}
