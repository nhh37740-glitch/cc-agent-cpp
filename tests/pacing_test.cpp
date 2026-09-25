// Phase 3 验收：realtime pacing。
// 10 秒视频按 PTS 节奏输出应耗时约 10 秒（容差 [9.2, 12.0]），
// 而非 pacing 模式应在 2 秒内完成。

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

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
        std::fprintf(stderr,
                     "usage: pacing_test <fixture10s> [offset_fixture(+5s)] "
                     "[negative_fixture(-2s)]\n");
        return 2;
    }
    const std::string path = argv[1];

    // 无 pacing：应远快于实时
    {
        const auto t0 = std::chrono::steady_clock::now();
        video::FFmpegFileSource source(path.c_str());
        video::Frame f;
        int64_t n = 0;
        while (source.read(f) == video::ReadStatus::Frame) ++n;
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
        while (source.read(f) == video::ReadStatus::Frame) ++paced_frames;
        paced_secs =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    std::fprintf(stderr, "pacing: frames=%lld secs=%.2f\n",
                 (long long)paced_frames, paced_secs);
    CHECK(paced_frames >= 280, "pacing 不应丢帧");
    CHECK(paced_secs >= 9.2 && paced_secs <= 12.0, "pacing 墙钟时长应接近 10 秒");

    // 非零首 PTS：+5 秒偏移的 fixture 首帧必须立即交付（R-025）
    if (argc >= 3) {
        const auto t0 = std::chrono::steady_clock::now();
        video::RealtimePacingSource source(argv[2]);
        video::Frame f;
        CHECK(source.read(f) == video::ReadStatus::Frame, "offset fixture 应能读到帧");
        const double first_wait =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "offset(+5s): first frame after %.2fs\n", first_wait);
        CHECK(first_wait < 1.0, "首帧 PTS=5s 不应造成启动空等");

        // 整段墙钟仍约为视频时长（1 秒）
        int64_t n = 1;
        while (source.read(f) == video::ReadStatus::Frame) ++n;
        const double total =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "offset(+5s): frames=%lld total=%.2fs\n",
                     (long long)n, total);
        CHECK(total >= 0.8 && total <= 3.0, "1 秒视频 pacing 总时长应约 1 秒");
    }

    // 负首 PTS：不应等待 |PTS| 时长（MP4 无法存负时间戳，用合成源验证）
    if (argc >= 3) {
        struct NegSource : public video::VideoSource {
            std::vector<uint8_t> plane = std::vector<uint8_t>(160 * 90, 128);
            int emitted = 0;
            video::ReadStatus read(video::Frame& f) override {
                if (emitted >= 10) return video::ReadStatus::Eof;
                f.width = 160;
                f.height = 90;
                f.pix_fmt = -1;  // 不做 RGB 转换，仅测节奏
                f.y_plane = plane.data();
                f.y_stride = 160;
                f.timestamp = -2.0 + emitted * 0.1;  // 首帧 -2s
                f.pts = -200 + emitted;
                ++emitted;
                return video::ReadStatus::Frame;
            }
            bool is_open() const override { return true; }
            const std::string& last_error() const override { return empty_; }
            std::string empty_;
        };

        const auto t0 = std::chrono::steady_clock::now();
        video::RealtimePacingSource source(std::make_unique<NegSource>());
        video::Frame f;
        CHECK(source.read(f) == video::ReadStatus::Frame, "negpts: first frame ok");
        CHECK(f.timestamp == -2.0, "negpts: first timestamp is -2");
        int64_t n = 1;
        while (source.read(f) == video::ReadStatus::Frame) ++n;
        const double total =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "negative pts: frames=%lld total=%.2fs\n", (long long)n, total);
        CHECK(n == 10, "negpts: all 10 frames delivered");
        CHECK(total <= 2.5, "negpts: wall clock ~1s (relative to first frame)");
    }

    if (failures == 0) {
        std::fprintf(stderr, "[PASS] pacing_test\n");
        return 0;
    }
    return 1;
}
