#include "ave/stream_decoder.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
}

#include <iostream>
#include <algorithm>
#include <cmath>

namespace ave {

StreamDecoder::StreamDecoder() = default;

StreamDecoder::~StreamDecoder() {
    free_resources();
}

StreamDecoder::StreamDecoder(StreamDecoder&& other) noexcept
    : file_path_(std::move(other.file_path_)),
      format_ctx_(other.format_ctx_),
      codec_ctx_(other.codec_ctx_),
      raw_frame_(other.raw_frame_),
      packet_(other.packet_),
      sws_ctx_(other.sws_ctx_),
      sws_width_(other.sws_width_),
      sws_height_(other.sws_height_),
      sws_src_format_(other.sws_src_format_),
      video_stream_index_(other.video_stream_index_),
      width_(other.width_),
      height_(other.height_),
      frame_rate_(other.frame_rate_),
      timebase_(other.timebase_),
      duration_(other.duration_),
      last_decoded_pts_(other.last_decoded_pts_) {
    other.format_ctx_ = nullptr;
    other.codec_ctx_ = nullptr;
    other.raw_frame_ = nullptr;
    other.packet_ = nullptr;
    other.sws_ctx_ = nullptr;
    other.video_stream_index_ = -1;
}

StreamDecoder& StreamDecoder::operator=(StreamDecoder&& other) noexcept {
    if (this != &other) {
        free_resources();

        file_path_ = std::move(other.file_path_);
        format_ctx_ = other.format_ctx_;
        codec_ctx_ = other.codec_ctx_;
        raw_frame_ = other.raw_frame_;
        packet_ = other.packet_;
        sws_ctx_ = other.sws_ctx_;
        sws_width_ = other.sws_width_;
        sws_height_ = other.sws_height_;
        sws_src_format_ = other.sws_src_format_;
        video_stream_index_ = other.video_stream_index_;
        width_ = other.width_;
        height_ = other.height_;
        frame_rate_ = other.frame_rate_;
        timebase_ = other.timebase_;
        duration_ = other.duration_;
        last_decoded_pts_ = other.last_decoded_pts_;

        other.format_ctx_ = nullptr;
        other.codec_ctx_ = nullptr;
        other.raw_frame_ = nullptr;
        other.packet_ = nullptr;
        other.sws_ctx_ = nullptr;
        other.video_stream_index_ = -1;
    }
    return *this;
}

bool StreamDecoder::is_open() const noexcept {
    return format_ctx_ != nullptr && codec_ctx_ != nullptr && video_stream_index_ >= 0;
}

bool StreamDecoder::open(const std::filesystem::path& file_path) {
    close();

    if (!std::filesystem::exists(file_path)) {
        return false;
    }

    std::string path_str = file_path.string();
    if (avformat_open_input(&format_ctx_, path_str.c_str(), nullptr, nullptr) < 0) {
        free_resources();
        return false;
    }

    if (avformat_find_stream_info(format_ctx_, nullptr) < 0) {
        free_resources();
        return false;
    }

    video_stream_index_ = -1;
    for (unsigned int i = 0; i < format_ctx_->nb_streams; ++i) {
        if (format_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index_ = static_cast<int>(i);
            break;
        }
    }

    if (video_stream_index_ < 0) {
        free_resources();
        return false;
    }

    AVStream* st = format_ctx_->streams[video_stream_index_];
    const AVCodec* decoder = avcodec_find_decoder(st->codecpar->codec_id);
    if (!decoder) {
        free_resources();
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(decoder);
    if (!codec_ctx_) {
        free_resources();
        return false;
    }

    if (avcodec_parameters_to_context(codec_ctx_, st->codecpar) < 0) {
        free_resources();
        return false;
    }

    codec_ctx_->thread_count = 0; // Automatic thread count

    if (avcodec_open2(codec_ctx_, decoder, nullptr) < 0) {
        free_resources();
        return false;
    }

    raw_frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!raw_frame_ || !packet_) {
        free_resources();
        return false;
    }

    width_ = codec_ctx_->width;
    height_ = codec_ctx_->height;
    file_path_ = file_path;

    if (st->time_base.num > 0 && st->time_base.den > 0) {
        timebase_ = Rational{st->time_base.num, st->time_base.den};
    } else {
        timebase_ = Rational{1, 1000};
    }

    if (st->r_frame_rate.num > 0 && st->r_frame_rate.den > 0) {
        frame_rate_ = Rational{st->r_frame_rate.num, st->r_frame_rate.den};
    } else if (st->avg_frame_rate.num > 0 && st->avg_frame_rate.den > 0) {
        frame_rate_ = Rational{st->avg_frame_rate.num, st->avg_frame_rate.den};
    } else {
        frame_rate_ = Rational{24, 1};
    }

    if (st->duration > 0) {
        duration_ = Rational{st->duration * timebase_.numerator(), timebase_.denominator()};
    } else if (format_ctx_->duration > 0) {
        duration_ = Rational{format_ctx_->duration, AV_TIME_BASE};
    } else {
        duration_ = Rational{0, 1};
    }

    last_decoded_pts_ = Rational{0, 1};
    return true;
}

void StreamDecoder::close() {
    free_resources();
}

void StreamDecoder::free_resources() noexcept {
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    if (raw_frame_) {
        av_frame_free(&raw_frame_);
    }
    if (packet_) {
        av_packet_free(&packet_);
    }
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }
    if (format_ctx_) {
        avformat_close_input(&format_ctx_);
    }

    file_path_.clear();
    video_stream_index_ = -1;
    width_ = 0;
    height_ = 0;
    sws_width_ = 0;
    sws_height_ = 0;
    sws_src_format_ = -1;
    frame_rate_ = Rational{24, 1};
    timebase_ = Rational{1, 1000};
    duration_ = Rational{0, 1};
    last_decoded_pts_ = Rational{0, 1};
}

bool StreamDecoder::seek_pts(Rational target_pts) {
    if (!is_open()) return false;

    double pts_sec = static_cast<double>(target_pts.numerator()) / static_cast<double>(target_pts.denominator());
    double tb_sec = static_cast<double>(timebase_.numerator()) / static_cast<double>(timebase_.denominator());
    int64_t target_ts = (tb_sec > 0.0) ? static_cast<int64_t>(pts_sec / tb_sec) : 0;

    int ret = avformat_seek_file(format_ctx_, video_stream_index_, INT64_MIN, target_ts, target_ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        return false;
    }

    avcodec_flush_buffers(codec_ctx_);
    last_decoded_pts_ = target_pts;
    return true;
}

std::optional<VideoFrame> StreamDecoder::decode_next_frame(uint64_t seek_generation,
                                                         int target_w,
                                                         int target_h) {
    if (!is_open()) return std::nullopt;

    while (true) {
        int ret = avcodec_receive_frame(codec_ctx_, raw_frame_);
        if (ret == 0) {
            // Frame successfully decoded
            Rational pts = (raw_frame_->pts != AV_NOPTS_VALUE)
                ? Rational{raw_frame_->pts * timebase_.numerator(), timebase_.denominator()}
                : last_decoded_pts_;

            last_decoded_pts_ = pts + Rational{frame_rate_.denominator(), frame_rate_.numerator()};

            int out_w = (target_w > 0) ? target_w : width_;
            int out_h = (target_h > 0) ? target_h : height_;

            // Setup or reuse SwsContext for RGBA conversion
            if (!sws_ctx_ || sws_width_ != out_w || sws_height_ != out_h || sws_src_format_ != raw_frame_->format) {
                if (sws_ctx_) sws_freeContext(sws_ctx_);
                sws_ctx_ = sws_getContext(
                    width_, height_, static_cast<AVPixelFormat>(raw_frame_->format),
                    out_w, out_h, AV_PIX_FMT_RGBA,
                    SWS_BILINEAR, nullptr, nullptr, nullptr
                );
                sws_width_ = out_w;
                sws_height_ = out_h;
                sws_src_format_ = raw_frame_->format;
            }

            if (!sws_ctx_) {
                av_frame_unref(raw_frame_);
                return std::nullopt;
            }

            std::vector<uint8_t> rgba_data(static_cast<size_t>(out_w) * static_cast<size_t>(out_h) * 4);
            uint8_t* dst_data[4] = { rgba_data.data(), nullptr, nullptr, nullptr };
            int dst_linesize[4] = { out_w * 4, 0, 0, 0 };

            sws_scale(sws_ctx_, raw_frame_->data, raw_frame_->linesize, 0, height_, dst_data, dst_linesize);

            VideoFrame vf;
            vf.seek_generation = seek_generation;
            vf.pts = pts;
            vf.duration = Rational{frame_rate_.denominator(), frame_rate_.numerator()};
            vf.width = out_w;
            vf.height = out_h;
            vf.rgba_data = std::move(rgba_data);
            vf.is_keyframe = (raw_frame_->flags & AV_FRAME_FLAG_KEY) != 0;

            av_frame_unref(raw_frame_);
            return vf;
        }

        if (ret == AVERROR(EAGAIN)) {
            // Need to supply packets to the decoder
            int read_ret = av_read_frame(format_ctx_, packet_);
            if (read_ret < 0) {
                // EOF or error: flush decoder
                avcodec_send_packet(codec_ctx_, nullptr);
                // Try to receive any final buffered frames
                ret = avcodec_receive_frame(codec_ctx_, raw_frame_);
                if (ret == 0) {
                    continue;
                }
                return std::nullopt;
            }

            if (packet_->stream_index == video_stream_index_) {
                avcodec_send_packet(codec_ctx_, packet_);
            }
            av_packet_unref(packet_);
            continue;
        }

        // Fatal decode error or EOF
        return std::nullopt;
    }
}

} // namespace ave
