// FFmpeg 本地文件视频源实现。
// 关键点：
// - 单向 av_read_frame → send_packet → receive_frame，绝不 seek；
// - EOF 后发送 NULL packet 触发 decoder flush，把缓冲帧全部取出；
// - PTS 缺失时用 best_effort_timestamp，仍缺失则按 1/fps 合成；
// - 非单调时间戳（<=上一帧）直接丢弃并计数，不进入下游。

#include "video/ffmpeg_video_source.hpp"

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/error.h"
}

#include <cmath>

namespace video {

FFmpegFileSource::FFmpegFileSource(const char* path, bool realtime_pacing)
    : path_(path), realtime_pacing_(realtime_pacing) {
    open(path);
}

FFmpegFileSource::~FFmpegFileSource() {
    close();
}

void FFmpegFileSource::close() {
    if (codec_ctx_) avcodec_free_context(&codec_ctx_);
    if (fmt_ctx_) avformat_close_input(&fmt_ctx_);
    if (frame_) av_frame_free(&frame_);
    if (packet_) av_packet_free(&packet_);
    video_stream_index_ = -1;
    input_eof_ = flush_sent_ = drained_ = first_frame_done_ = false;
    last_timestamp_ = -1.0;
}

bool FFmpegFileSource::open(const char* path) {
    int ret = avformat_open_input(&fmt_ctx_, path, nullptr, nullptr);
    if (ret < 0) return false;

    ret = avformat_find_stream_info(fmt_ctx_, nullptr);
    if (ret < 0) { close(); return false; }

    const AVCodec* codec = nullptr;
    video_stream_index_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1,
                                              const_cast<const AVCodec**>(&codec), 0);
    if (video_stream_index_ < 0 || !codec) { close(); return false; }

    AVStream* stream = fmt_ctx_->streams[video_stream_index_];
    time_base_ = av_q2d(stream->time_base);
    width_ = stream->codecpar->width;
    height_ = stream->codecpar->height;

    // 用流平均帧率估计 fps；无效时退回 r_frame_rate 或默认 30
    AVRational fr = stream->avg_frame_rate;
    if (fr.num <= 0 || fr.den <= 0) fr = stream->r_frame_rate;
    fps_ = (fr.num > 0 && fr.den > 0) ? av_q2d(fr) : 30.0;
    if (!(fps_ > 0.0 && std::isfinite(fps_))) fps_ = 30.0;

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) { close(); return false; }
    ret = avcodec_parameters_to_context(codec_ctx_, stream->codecpar);
    if (ret < 0) { close(); return false; }

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "threads", "auto", 0);
    ret = avcodec_open2(codec_ctx_, codec, &opts);
    av_dict_free(&opts);
    if (ret < 0) { close(); return false; }

    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!frame_ || !packet_) { close(); return false; }
    return true;
}

int FFmpegFileSource::next_decoded_frame() {
    for (;;) {
        int ret = avcodec_receive_frame(codec_ctx_, frame_);
        if (ret == 0) return 1;                       // 拿到一帧
        if (ret == AVERROR(EAGAIN)) {                 // 解码器要更多输入
            if (input_eof_) {
                if (!flush_sent_) {
                    avcodec_send_packet(codec_ctx_, nullptr);  // 触发 flush
                    flush_sent_ = true;
                    continue;
                }
                drained_ = true;                      // flush 后再无输出
                return 0;
            }
            ret = av_read_frame(fmt_ctx_, packet_);
            if (ret == AVERROR_EOF) {                 // 输入结束，转入 flush 阶段
                input_eof_ = true;
                continue;
            }
            if (ret < 0) return 0;                    // 读包错误：视为流结束
            if (packet_->stream_index != video_stream_index_) {
                av_packet_unref(packet_);
                continue;
            }
            ret = avcodec_send_packet(codec_ctx_, packet_);
            av_packet_unref(packet_);
            if (ret < 0 && ret != AVERROR(EAGAIN)) return 0;  // 发送错误：结束
            continue;
        }
        if (ret == AVERROR_EOF) return 0;             // 解码器报告 EOF
        return 0;                                     // 其他解码错误：结束
    }
}

bool FFmpegFileSource::read(Frame& out) {
    if (!fmt_ctx_ || !codec_ctx_) return false;
    if (drained_) return false;

    while (next_decoded_frame() == 1) {
        int64_t pts = frame_->best_effort_timestamp;
        if (pts == AV_NOPTS_VALUE) pts = frame_->pts;
        double ts = 0.0;
        bool synthesized = false;
        if (pts == AV_NOPTS_VALUE) {
            // 时间戳缺失：按上一帧 + 1/fps 合成
            ts = last_timestamp_ < 0 ? 0.0 : last_timestamp_ + 1.0 / fps_;
            ++synth_count_;
            synthesized = true;
        } else {
            ts = pts * time_base_;
        }

        // 非单调时间戳策略：丢弃该帧，不进入下游
        if (first_frame_done_ && !synthesized && ts <= last_timestamp_) {
            ++nonmono_dropped_;
            continue;
        }

        out.pts = pts;
        out.timestamp = ts;
        out.width = frame_->width;
        out.height = frame_->height;
        out.pix_fmt = frame_->format;
        out.y_plane = frame_->data[0];
        out.y_stride = frame_->linesize[0];
        out.u_plane = frame_->data[1];
        out.u_stride = frame_->linesize[1];
        out.v_plane = frame_->data[2];
        out.v_stride = frame_->linesize[2];

        last_timestamp_ = ts;
        first_frame_done_ = true;

        // TODO(Phase 3): realtime_pacing 按 PTS 节奏等待
        (void)realtime_pacing_;
        return true;
    }
    return false;  // 流结束（含 flush 完毕）
}

}  // namespace video
