#include "ave/stream_audio_decoder.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/avutil.h>
}

#include <iostream>
#include <algorithm>
#include <cmath>

namespace ave {

StreamAudioDecoder::StreamAudioDecoder() = default;

StreamAudioDecoder::~StreamAudioDecoder() {
    free_resources();
}

StreamAudioDecoder::StreamAudioDecoder(StreamAudioDecoder&& other) noexcept
    : file_path_(std::move(other.file_path_)),
      format_ctx_(other.format_ctx_),
      codec_ctx_(other.codec_ctx_),
      raw_frame_(other.raw_frame_),
      packet_(other.packet_),
      swr_ctx_(other.swr_ctx_),
      audio_stream_index_(other.audio_stream_index_),
      target_sample_rate_(other.target_sample_rate_),
      target_channels_(other.target_channels_),
      timebase_(other.timebase_),
      duration_(other.duration_),
      last_decoded_pts_(other.last_decoded_pts_) {
    other.format_ctx_ = nullptr;
    other.codec_ctx_ = nullptr;
    other.raw_frame_ = nullptr;
    other.packet_ = nullptr;
    other.swr_ctx_ = nullptr;
    other.audio_stream_index_ = -1;
}

StreamAudioDecoder& StreamAudioDecoder::operator=(StreamAudioDecoder&& other) noexcept {
    if (this != &other) {
        free_resources();
        file_path_ = std::move(other.file_path_);
        format_ctx_ = other.format_ctx_;
        codec_ctx_ = other.codec_ctx_;
        raw_frame_ = other.raw_frame_;
        packet_ = other.packet_;
        swr_ctx_ = other.swr_ctx_;
        audio_stream_index_ = other.audio_stream_index_;
        target_sample_rate_ = other.target_sample_rate_;
        target_channels_ = other.target_channels_;
        timebase_ = other.timebase_;
        duration_ = other.duration_;
        last_decoded_pts_ = other.last_decoded_pts_;

        other.format_ctx_ = nullptr;
        other.codec_ctx_ = nullptr;
        other.raw_frame_ = nullptr;
        other.packet_ = nullptr;
        other.swr_ctx_ = nullptr;
        other.audio_stream_index_ = -1;
    }
    return *this;
}

void StreamAudioDecoder::free_resources() noexcept {
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
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
    audio_stream_index_ = -1;
}

bool StreamAudioDecoder::is_open() const noexcept {
    return format_ctx_ != nullptr && codec_ctx_ != nullptr && swr_ctx_ != nullptr;
}

void StreamAudioDecoder::close() {
    free_resources();
    file_path_.clear();
}

bool StreamAudioDecoder::open(const std::filesystem::path& file_path,
                             uint32_t target_sample_rate,
                             int target_channels) {
    free_resources();
    file_path_ = file_path;
    target_sample_rate_ = target_sample_rate;
    target_channels_ = target_channels > 0 ? target_channels : 2;

    std::string path_str = file_path.string();
    if (avformat_open_input(&format_ctx_, path_str.c_str(), nullptr, nullptr) < 0) {
        return false;
    }

    if (avformat_find_stream_info(format_ctx_, nullptr) < 0) {
        free_resources();
        return false;
    }

    // Find first audio stream
    for (unsigned int i = 0; i < format_ctx_->nb_streams; ++i) {
        if (format_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index_ = static_cast<int>(i);
            break;
        }
    }

    if (audio_stream_index_ < 0) {
        free_resources();
        return false;
    }

    AVStream* stream = format_ctx_->streams[audio_stream_index_];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        free_resources();
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(decoder);
    if (!codec_ctx_) {
        free_resources();
        return false;
    }

    if (avcodec_parameters_to_context(codec_ctx_, stream->codecpar) < 0) {
        free_resources();
        return false;
    }

    if (avcodec_open2(codec_ctx_, decoder, nullptr) < 0) {
        free_resources();
        return false;
    }

    // Set up SwrContext
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, target_channels_);

    int ret = swr_alloc_set_opts2(
        &swr_ctx_,
        &out_layout,
        AV_SAMPLE_FMT_FLT, // Interleaved 32-bit float
        static_cast<int>(target_sample_rate_),
        &codec_ctx_->ch_layout,
        codec_ctx_->sample_fmt,
        codec_ctx_->sample_rate,
        0,
        nullptr
    );
    av_channel_layout_uninit(&out_layout);

    if (ret < 0 || !swr_ctx_ || swr_init(swr_ctx_) < 0) {
        free_resources();
        return false;
    }

    raw_frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!raw_frame_ || !packet_) {
        free_resources();
        return false;
    }

    if (stream->time_base.num > 0 && stream->time_base.den > 0) {
        timebase_ = Rational{stream->time_base.num, stream->time_base.den};
    } else {
        timebase_ = Rational{1, static_cast<int64_t>(target_sample_rate_)};
    }

    if (stream->duration > 0) {
        duration_ = Rational{stream->duration * timebase_.numerator(), timebase_.denominator()};
    } else if (format_ctx_->duration > 0) {
        duration_ = Rational{format_ctx_->duration, AV_TIME_BASE};
    } else {
        duration_ = Rational{0, 1};
    }

    return true;
}

bool StreamAudioDecoder::seek_pts(Rational target_pts) {
    if (!is_open()) {
        return false;
    }

    AVStream* stream = format_ctx_->streams[audio_stream_index_];
    int64_t target_ts = static_cast<int64_t>(std::llround(
        (static_cast<double>(target_pts.numerator()) / static_cast<double>(target_pts.denominator())) *
        (static_cast<double>(stream->time_base.den) / static_cast<double>(stream->time_base.num))));

    if (avformat_seek_file(format_ctx_, audio_stream_index_, INT64_MIN, target_ts, target_ts, 0) < 0) {
        return false;
    }

    avcodec_flush_buffers(codec_ctx_);
    last_decoded_pts_ = target_pts;
    return true;
}

std::optional<AudioBuffer> StreamAudioDecoder::decode_next_audio() {
    if (!is_open()) {
        return std::nullopt;
    }

    while (true) {
        int ret = avcodec_receive_frame(codec_ctx_, raw_frame_);
        if (ret == 0) {
            int out_samples = swr_get_out_samples(swr_ctx_, raw_frame_->nb_samples);
            if (out_samples <= 0) {
                out_samples = raw_frame_->nb_samples * static_cast<int>(target_sample_rate_) / codec_ctx_->sample_rate + 256;
            }

            AudioBuffer result(static_cast<size_t>(out_samples), target_channels_, static_cast<int>(target_sample_rate_));
            uint8_t* out_data[1] = { reinterpret_cast<uint8_t*>(result.data()) };

            int converted = swr_convert(
                swr_ctx_,
                out_data,
                out_samples,
                const_cast<const uint8_t**>(raw_frame_->extended_data),
                raw_frame_->nb_samples
            );

            av_frame_unref(raw_frame_);

            if (converted > 0) {
                result.truncate_frames(static_cast<size_t>(converted));
                return result;
            }
            continue;
        }

        int read_ret = av_read_frame(format_ctx_, packet_);
        if (read_ret < 0) {
            return std::nullopt;
        }

        if (packet_->stream_index != audio_stream_index_) {
            av_packet_unref(packet_);
            continue;
        }

        int send_ret = avcodec_send_packet(codec_ctx_, packet_);
        av_packet_unref(packet_);

        if (send_ret < 0) {
            return std::nullopt;
        }
    }
}

} // namespace ave

