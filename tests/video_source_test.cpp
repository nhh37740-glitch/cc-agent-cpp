// Phase 2 验收：FFmpegVideoSource 单向解码。
// 检查：帧数>0、时间戳单调递增、首末 PTS 合理、EOF 后正确结束、
// 重新打开可再次完整读取（资源释放正常）、EOF flush 不丢尾帧。

#include <cstdio>
#include <string>

#include "video/ffmpeg_video_source.hpp"

using video::FFmpegFileSource;
using video::Frame;

static int failures = 0;
#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::fprintf(stderr, "[FAIL] %s (line %d)\n", msg, __LINE__);   \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: video_source_test <fixture.mp4>\n");
        return 2;
    }
    const std::string path = argv[1];

    // 第一遍：完整读取
    double first_ts = -1.0, last_ts = -1.0;
    int64_t count = 0;
    bool monotonic = true;
    {
        FFmpegFileSource source(path.c_str());
        CHECK(source.fps() > 20.0 && source.fps() < 40.0, "fps 应约为 30");
        Frame f;
        while (source.read(f)) {
            if (count > 0 && !(f.timestamp > last_ts)) monotonic = false;
            if (count == 0) first_ts = f.timestamp;
            last_ts = f.timestamp;
            ++count;
        }
    }
    std::fprintf(stderr, "frames=%lld first=%.3f last=%.3f\n",
                 (long long)count, first_ts, last_ts);
    CHECK(count > 100, "6 秒 30fps 视频应有大量帧");
    CHECK(monotonic, "时间戳应严格单调递增");
    CHECK(first_ts >= 0.0 && first_ts < 0.2, "首帧 PTS 接近 0");
    CHECK(last_ts > 5.4 && last_ts <= 6.5, "末帧 PTS 接近视频时长 6s");

    // 第二遍：重新打开，验证资源释放后可重复读取
    {
        FFmpegFileSource source(path.c_str());
        Frame f;
        int64_t second = 0;
        while (source.read(f)) ++second;
        CHECK(second == count, "重开后应读到相同帧数（资源释放正常）");
    }

    if (failures == 0) {
        std::fprintf(stderr, "[PASS] video_source_test\n");
        return 0;
    }
    return 1;
}
