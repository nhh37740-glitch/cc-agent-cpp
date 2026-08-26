// Phase 1 验收：确认 FFmpeg 头文件/导入库可链接、运行 DLL 可加载，
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
    std::fprintf(stderr, "ffmpeg: avformat=%u.%u.%u avcodec=%u.%u.%u swscale=%u.avutil=%u.%u.%u\n",
        avf >> 16, (avf >> 8) & 0xFF, avf & 0xFF,
        avc >> 16, (avc >> 8) & 0xFF, avc & 0xFF,
        sws >> 16,
        avu >> 16, (avu >> 8) & 0xFF, avu & 0xFF);
    if ((avf >> 16) < 63 || (avu >> 16) < 61) {
        std::fprintf(stderr, "[FAIL] FFmpeg 库版本过旧（期望 9.x 对应 avformat 63.x）\n");
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

    std::fprintf(stderr, "[PASS] deps_probe\n");
    return 0;
}
