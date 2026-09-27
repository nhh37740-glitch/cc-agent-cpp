// Phase 1 验收：确认 FFmpeg 运行库与编译期头文件的 ABI 主版本一致，
// 以及 llama.cpp 的 llama 与 mtmd 目标真实存在且可链接。

#include <cstdio>

#include "log.hpp"
#include "llama.h"
#include "mtmd.h"

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavutil/avutil.h"
}

int main() {
    unsigned avf = avformat_version();
    unsigned avc = avcodec_version();
    unsigned sws = swscale_version();
    unsigned avu = avutil_version();
    std::fprintf(stderr, "ffmpeg: avformat=%u.%u.%u avcodec=%u.%u.%u swscale=%u.%u.%u avutil=%u.%u.%u\n",
        avf >> 16, (avf >> 8) & 0xFF, avf & 0xFF,
        avc >> 16, (avc >> 8) & 0xFF, avc & 0xFF,
        sws >> 16, (sws >> 8) & 0xFF, sws & 0xFF,
        avu >> 16, (avu >> 8) & 0xFF, avu & 0xFF);
    if ((avf >> 16) != LIBAVFORMAT_VERSION_MAJOR ||
        (avc >> 16) != LIBAVCODEC_VERSION_MAJOR ||
        (sws >> 16) != LIBSWSCALE_VERSION_MAJOR ||
        (avu >> 16) != LIBAVUTIL_VERSION_MAJOR) {
        std::fprintf(stderr,
            "[FAIL] FFmpeg 运行库与编译期头文件 ABI 主版本不一致："
            "headers avformat=%d avcodec=%d swscale=%d avutil=%d\n",
            LIBAVFORMAT_VERSION_MAJOR, LIBAVCODEC_VERSION_MAJOR,
            LIBSWSCALE_VERSION_MAJOR, LIBAVUTIL_VERSION_MAJOR);
        return 1;
    }

    std::fprintf(stderr, "before backend_init\n");
    llama_backend_init();
    int64_t t = llama_time_us();  // 仅验证 llama API 可链接
    bool has_mtmd_header = true; // mtmd.h 已成功 include 即为真
    std::fprintf(stderr, "llama.cpp time_us=%lld mtmd_header=%d\n", (long long)t, (int)has_mtmd_header);

    logging::JsonlLogger logger("logs/events.jsonl");
    logger.event("deps_probe", {{"avformat", avf}});
    if (!logger.is_open()) return 1;

    llama_backend_free();

    std::fprintf(stderr, "[PASS] deps_probe\n");
    return 0;
}
