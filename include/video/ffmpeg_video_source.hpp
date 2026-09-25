#pragma once

// 基于 FFmpeg 的本地文件视频源：单向顺序解码，模拟实时摄像头的数据来源。
// 禁止 seek / 二遍扫描；EOF 时执行 decoder flush 并释放全部资源。

#include <cstdint>
#include <string>

#include "video/video_source.hpp"

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace video {

class FFmpegFileSource : public VideoSource {
public:
    // realtime_pacing=true 时按 PTS 节奏输出（Phase 3 启用）。
    explicit FFmpegFileSource(const char* path, bool realtime_pacing = false);
    ~FFmpegFileSource() override;

    FFmpegFileSource(const FFmpegFileSource&) = delete;
    FFmpegFileSource& operator=(const FFmpegFileSource&) = delete;

    ReadStatus read(Frame& frame) override;
    bool is_open() const override { return fmt_ctx_ && codec_ctx_ && open_ok_; }
    const std::string& last_error() const override { return last_error_; }

    double fps() const { return fps_; }
    int stream_width() const { return width_; }
    int stream_height() const { return height_; }
    int64_t dropped_nonmonotonic_frames() const { return nonmono_dropped_; }
    int64_t synthesized_timestamps() const { return synth_count_; }

private:
    bool open(const char* path);
    void fail(const std::string& context, int av_err = 0);
    void close();
    // 从解码器取出一个已解出的 AVFrame（含 EOF flush 逻辑）；
    // 返回：1=得到一帧，0=流结束，-1=错误
    int next_decoded_frame();

    std::string path_;
    bool realtime_pacing_ = false;
    bool open_ok_ = false;
    std::string last_error_;

    AVFormatContext* fmt_ctx_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;

    int video_stream_index_ = -1;
    double time_base_ = 0.0;
    double fps_ = 30.0;
    int width_ = 0, height_ = 0;

    bool input_eof_ = false;     // 已读到输入末尾
    bool flush_sent_ = false;    // 已向解码器发送 flush
    bool drained_ = false;       // 解码器缓冲已全部取空
    bool first_frame_done_ = false;

    double last_timestamp_ = -1.0;
    int64_t nonmono_dropped_ = 0;
    int64_t synth_count_ = 0;
};

}  // namespace video
