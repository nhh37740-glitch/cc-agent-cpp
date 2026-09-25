// Phase 4 验收：FrameFilter。
// 单元部分：合成 Y 平面帧验证降采样、分数计算与时间门控；
// 集成部分：静止视频候选极少，动态视频（testsrc）持续产生候选。

#include <cstdio>
#include <string>
#include <vector>

#include "filter/frame_filter.hpp"
#include "video/ffmpeg_video_source.hpp"

static int failures = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "[FAIL] %s (line %d)\n", msg, __LINE__); \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

// 构造一帧合成视图
struct SyntheticFrame {
    std::vector<uint8_t> buf;
    video::Frame frame;
    SyntheticFrame(int w, int h, uint8_t value) : buf(size_t(w) * h, value) {
        frame.width = w;
        frame.height = h;
        frame.y_plane = buf.data();
        frame.y_stride = w;
    }
};

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: frame_filter_test <dynamic_fixture.mp4> <static_fixture.mp4>\n");
        return 2;
    }

    // ---- 单元：相同画面分数为 0 ----
    {
        filter::FrameFilterConfig cfg;
        cfg.sample_interval_ms = 0;  // 关闭时间门控便于单元测试
        filter::FrameFilter filter(cfg);
        SyntheticFrame a(320, 240, 100);
        a.frame.timestamp = 0.0;
        auto r1 = filter.analyze(a.frame);
        CHECK(r1.was_evaluated && r1.should_analyze && r1.score == 1.0f,
              "首帧应作为候选启动管线");

        a.frame.timestamp = 2.0;
        auto r2 = filter.analyze(a.frame);
        CHECK(r2.was_evaluated && !r2.should_analyze && r2.score == 0.0f,
              "相同画面的变化分数应为 0");
    }

    // ---- 单元：全屏跳变产生高分数 ----
    {
        filter::FrameFilterConfig cfg;
        cfg.sample_interval_ms = 0;
        filter::FrameFilter filter(cfg);
        SyntheticFrame a(320, 240, 10);
        SyntheticFrame b(320, 240, 200);
        a.frame.timestamp = 0.0;
        b.frame.timestamp = 1.0;
        filter.analyze(a.frame);
        auto r = filter.analyze(b.frame);
        CHECK(r.score > 0.6f, "大幅跳变分数应 > 0.6");
        CHECK(r.should_analyze, "大幅跳变应为候选");
    }

    // ---- 单元：时间门控 ----
    {
        filter::FrameFilterConfig cfg;
        cfg.sample_interval_ms = 2000;
        filter::FrameFilter filter(cfg);
        SyntheticFrame a(320, 240, 10);
        a.frame.timestamp = 0.0;
        CHECK(filter.analyze(a.frame).was_evaluated, "首帧必须评估");
        a.frame.timestamp = 0.5;
        auto skipped = filter.analyze(a.frame);
        CHECK(!skipped.was_evaluated && skipped.score == 0.0f,
              "间隔内的帧应被时间门控跳过");
        a.frame.timestamp = 2.5;
        CHECK(filter.analyze(a.frame).was_evaluated, "到达采样间隔的帧必须评估");
    }

    // ---- 集成：静止视频候选应极少 ----
    {
        filter::FrameFilterConfig cfg;
        filter::FrameFilter filter(cfg);
        video::FFmpegFileSource source(argv[2]);
        video::Frame f;
        int64_t candidates = 0;
        while (source.read(f) == video::ReadStatus::Frame) {
            if (filter.analyze(f).should_analyze) ++candidates;
        }
        std::fprintf(stderr, "static: candidates=%lld\n", (long long)candidates);
        CHECK(candidates <= 1, "纯黑静止视频只应在首帧产生候选");
    }

    // ---- 集成：动态视频 testsrc 应产生多个候选 ----
    {
        filter::FrameFilterConfig cfg;
        filter::FrameFilter filter(cfg);
        video::FFmpegFileSource source(argv[1]);
        video::Frame f;
        int64_t frames = 0, candidates = 0, evaluated = 0;
        while (source.read(f) == video::ReadStatus::Frame) {
            ++frames;
            auto r = filter.analyze(f);
            if (r.was_evaluated) ++evaluated;
            if (r.should_analyze) ++candidates;
        }
        std::fprintf(stderr,
                     "integration: frames=%lld evaluated=%lld candidates=%lld\n",
                     (long long)frames, (long long)evaluated, (long long)candidates);
        CHECK(frames > 100, "应有大量输入帧");
        CHECK(evaluated <= frames / 20 + 2, "时间采样应把评估次数压缩约 60 倍");
        CHECK(candidates >= 2, "动态视频应产生多个候选");
    }

    if (failures == 0) {
        std::fprintf(stderr, "[PASS] frame_filter_test\n");
        return 0;
    }
    return 1;
}
