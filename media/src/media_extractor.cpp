#include "ave/media_extractor.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
#include <utility>

namespace ave {
namespace {

int extractor_interrupt_cb(void* opaque) noexcept {
    if (!opaque) {
        return 0;
    }
    const auto* cancel = static_cast<const std::atomic<bool>*>(opaque);
    return cancel->load(std::memory_order_acquire) ? 1 : 0;
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

} // namespace

// --- Thumbnail Extractor ---

std::optional<ThumbnailData> ThumbnailExtractor::extract(
    const std::filesystem::path& file_path,
    Rational timestamp,
    int target_width,
    int target_height,
    std::shared_ptr<std::atomic<bool>> cancel_state) {

    std::shared_ptr<std::atomic<bool>> state =
        cancel_state ? std::move(cancel_state) : std::make_shared<std::atomic<bool>>(false);
    if (state->load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    std::error_code ec;
    if (!std::filesystem::exists(file_path, ec) || target_width <= 0 || target_height <= 0) {
        return std::nullopt;
    }

    struct FormatGuard {
        AVFormatContext* ctx{nullptr};
        ~FormatGuard() {
            if (ctx) {
                avformat_close_input(&ctx);
            }
        }
    } fmt_guard; // Declared AFTER `state` so `state` outlives avformat_close_input().

    AVFormatContext* fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        return std::nullopt;
    }
    fmt_ctx->interrupt_callback.callback = &extractor_interrupt_cb;
    fmt_ctx->interrupt_callback.opaque = state.get();

    const std::string path_str = path_to_utf8(file_path);
    if (avformat_open_input(&fmt_ctx, path_str.c_str(), nullptr, nullptr) < 0) {
        if (fmt_ctx) {
            avformat_free_context(fmt_ctx);
        }
        return std::nullopt;
    }
    fmt_guard.ctx = fmt_ctx;

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0 || state->load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    int video_stream_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = static_cast<int>(i);
            break;
        }
    }
    if (video_stream_idx < 0) return std::nullopt;

    AVStream* st = fmt_ctx->streams[video_stream_idx];
    const AVCodec* decoder = avcodec_find_decoder(st->codecpar->codec_id);
    if (!decoder) return std::nullopt;

    AVCodecContext* codec_ctx = avcodec_alloc_context3(decoder);
    if (!codec_ctx) return std::nullopt;

    struct CodecCtxGuard {
        AVCodecContext* ctx;
        ~CodecCtxGuard() { if (ctx) avcodec_free_context(&ctx); }
    } codec_guard{codec_ctx};

    if (avcodec_parameters_to_context(codec_ctx, st->codecpar) < 0) return std::nullopt;
    if (avcodec_open2(codec_ctx, decoder, nullptr) < 0) return std::nullopt;

    // Seek if timestamp > 0
    if (timestamp.numerator() > 0 && st->time_base.den > 0) {
        Rational tb(st->time_base.num, st->time_base.den);
        int64_t seek_pts = (timestamp / tb).numerator();
        const int seek_ret = av_seek_frame(fmt_ctx, video_stream_idx, seek_pts, AVSEEK_FLAG_BACKWARD);
        if (seek_ret == AVERROR_EXIT || state->load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        avcodec_flush_buffers(codec_ctx);
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!pkt || !frame) {
        if (pkt) av_packet_free(&pkt);
        if (frame) av_frame_free(&frame);
        return std::nullopt;
    }

    struct PacketFrameGuard {
        AVPacket* p;
        AVFrame* f;
        ~PacketFrameGuard() {
            if (p) av_packet_free(&p);
            if (f) av_frame_free(&f);
        }
    } pf_guard{pkt, frame};

    bool got_frame = false;
    int read_ret = 0;
    while (!state->load(std::memory_order_acquire) &&
           (read_ret = av_read_frame(fmt_ctx, pkt)) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            if (avcodec_send_packet(codec_ctx, pkt) == 0) {
                if (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    got_frame = true;
                    av_packet_unref(pkt);
                    break;
                }
            }
        }
        av_packet_unref(pkt);
    }

    if (read_ret == AVERROR_EXIT || state->load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    if (!got_frame) {
        // Drain decoder
        if (avcodec_send_packet(codec_ctx, nullptr) == 0) {
            if (avcodec_receive_frame(codec_ctx, frame) == 0) {
                got_frame = true;
            }
        }
    }

    if (!got_frame) return std::nullopt;

    // Scale and convert to RGBA32
    SwsContext* sws = sws_getContext(
        frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
        target_width, target_height, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    if (!sws) return std::nullopt;

    struct SwsGuard {
        SwsContext* s;
        ~SwsGuard() { if (s) sws_freeContext(s); }
    } sws_guard{sws};

    ThumbnailData thumb;
    thumb.width = target_width;
    thumb.height = target_height;
    thumb.timestamp = timestamp;
    thumb.rgba_pixels.resize(static_cast<size_t>(target_width) * static_cast<size_t>(target_height) * 4u);

    uint8_t* dst_data[4] = { thumb.rgba_pixels.data(), nullptr, nullptr, nullptr };
    int dst_linesize[4] = { target_width * 4, 0, 0, 0 };

    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst_data, dst_linesize);

    return thumb;
}

// --- Waveform Extractor ---

std::optional<WaveformData> WaveformExtractor::extract(
    const std::filesystem::path& file_path,
    int peaks_per_second,
    std::shared_ptr<std::atomic<bool>> cancel_state) {

    std::shared_ptr<std::atomic<bool>> state =
        cancel_state ? std::move(cancel_state) : std::make_shared<std::atomic<bool>>(false);
    if (state->load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    std::error_code ec;
    if (!std::filesystem::exists(file_path, ec) || peaks_per_second <= 0) {
        return std::nullopt;
    }

    struct FormatGuard {
        AVFormatContext* ctx{nullptr};
        ~FormatGuard() {
            if (ctx) {
                avformat_close_input(&ctx);
            }
        }
    } fmt_guard; // Declared AFTER `state` so `state` outlives avformat_close_input().

    AVFormatContext* fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        return std::nullopt;
    }
    fmt_ctx->interrupt_callback.callback = &extractor_interrupt_cb;
    fmt_ctx->interrupt_callback.opaque = state.get();

    const std::string path_str = path_to_utf8(file_path);
    if (avformat_open_input(&fmt_ctx, path_str.c_str(), nullptr, nullptr) < 0) {
        if (fmt_ctx) {
            avformat_free_context(fmt_ctx);
        }
        return std::nullopt;
    }
    fmt_guard.ctx = fmt_ctx;

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0 || state->load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    int audio_stream_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_idx = static_cast<int>(i);
            break;
        }
    }
    if (audio_stream_idx < 0) return std::nullopt;

    AVStream* st = fmt_ctx->streams[audio_stream_idx];
    const AVCodec* decoder = avcodec_find_decoder(st->codecpar->codec_id);
    if (!decoder) return std::nullopt;

    AVCodecContext* codec_ctx = avcodec_alloc_context3(decoder);
    if (!codec_ctx) return std::nullopt;

    struct CodecCtxGuard {
        AVCodecContext* ctx;
        ~CodecCtxGuard() { if (ctx) avcodec_free_context(&ctx); }
    } codec_guard{codec_ctx};

    if (avcodec_parameters_to_context(codec_ctx, st->codecpar) < 0) return std::nullopt;
    if (avcodec_open2(codec_ctx, decoder, nullptr) < 0) return std::nullopt;

    int channels = codec_ctx->ch_layout.nb_channels;
    if (channels <= 0) channels = 1;
    int sample_rate = codec_ctx->sample_rate;
    if (sample_rate <= 0) sample_rate = 48000;

    // Resampler to Float32 interleaved
    SwrContext* swr_ctx = nullptr;
    AVChannelLayout out_layout{};
    av_channel_layout_default(&out_layout, channels);

    int err = swr_alloc_set_opts2(
        &swr_ctx,
        &out_layout,
        AV_SAMPLE_FMT_FLT,
        sample_rate,
        &codec_ctx->ch_layout,
        codec_ctx->sample_fmt,
        codec_ctx->sample_rate,
        0, nullptr
    );
    av_channel_layout_uninit(&out_layout);
    if (err < 0 || !swr_ctx) return std::nullopt;

    struct SwrGuard {
        SwrContext* s;
        ~SwrGuard() { if (s) swr_free(&s); }
    } swr_guard{swr_ctx};

    if (swr_init(swr_ctx) < 0) return std::nullopt;

    WaveformData wave;
    wave.channels = channels;
    wave.sample_rate = sample_rate;
    wave.peaks_per_second = peaks_per_second;
    wave.channel_peaks.resize(static_cast<size_t>(channels));

    int bucket_samples = std::max(1, sample_rate / peaks_per_second);

    std::vector<float> curr_min(static_cast<size_t>(channels), 0.0f);
    std::vector<float> curr_max(static_cast<size_t>(channels), 0.0f);
    int samples_in_bucket = 0;

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!pkt || !frame) {
        if (pkt) av_packet_free(&pkt);
        if (frame) av_frame_free(&frame);
        return std::nullopt;
    }

    struct PacketFrameGuard {
        AVPacket* p;
        AVFrame* f;
        ~PacketFrameGuard() {
            if (p) av_packet_free(&p);
            if (f) av_frame_free(&f);
        }
    } pf_guard{pkt, frame};

    std::vector<float> resample_buffer;

    auto process_samples = [&](const float* samples, int nb_samples) {
        for (int i = 0; i < nb_samples; ++i) {
            for (int ch = 0; ch < channels; ++ch) {
                float val = samples[static_cast<size_t>(i) * static_cast<size_t>(channels) + static_cast<size_t>(ch)];
                if (samples_in_bucket == 0) {
                    curr_min[static_cast<size_t>(ch)] = val;
                    curr_max[static_cast<size_t>(ch)] = val;
                } else {
                    curr_min[static_cast<size_t>(ch)] = std::min(curr_min[static_cast<size_t>(ch)], val);
                    curr_max[static_cast<size_t>(ch)] = std::max(curr_max[static_cast<size_t>(ch)], val);
                }
            }
            samples_in_bucket++;
            if (samples_in_bucket >= bucket_samples) {
                for (int ch = 0; ch < channels; ++ch) {
                    wave.channel_peaks[static_cast<size_t>(ch)].min_peaks.push_back(curr_min[static_cast<size_t>(ch)]);
                    wave.channel_peaks[static_cast<size_t>(ch)].max_peaks.push_back(curr_max[static_cast<size_t>(ch)]);
                }
                samples_in_bucket = 0;
            }
        }
    };

    int read_ret = 0;
    while (!state->load(std::memory_order_acquire) &&
           (read_ret = av_read_frame(fmt_ctx, pkt)) >= 0) {
        if (pkt->stream_index == audio_stream_idx) {
            if (avcodec_send_packet(codec_ctx, pkt) == 0) {
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    if (state->load(std::memory_order_acquire)) {
                        av_packet_unref(pkt);
                        return std::nullopt;
                    }
                    int max_out_samples = static_cast<int>(av_rescale_rnd(
                        swr_get_delay(swr_ctx, sample_rate) + frame->nb_samples,
                        sample_rate, codec_ctx->sample_rate, AV_ROUND_UP
                    ));
                    resample_buffer.resize(static_cast<size_t>(max_out_samples) * static_cast<size_t>(channels));
                    uint8_t* out_data[1] = { reinterpret_cast<uint8_t*>(resample_buffer.data()) };

                    int converted = swr_convert(
                        swr_ctx, out_data, max_out_samples,
                        const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples
                    );
                    if (converted > 0) {
                        process_samples(resample_buffer.data(), converted);
                    }
                }
            }
        }
        av_packet_unref(pkt);
    }

    if (read_ret == AVERROR_EXIT || state->load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    // Flush any remaining partial bucket
    if (samples_in_bucket > 0) {
        for (int ch = 0; ch < channels; ++ch) {
            wave.channel_peaks[static_cast<size_t>(ch)].min_peaks.push_back(curr_min[static_cast<size_t>(ch)]);
            wave.channel_peaks[static_cast<size_t>(ch)].max_peaks.push_back(curr_max[static_cast<size_t>(ch)]);
        }
    }

    if (fmt_ctx->duration != AV_NOPTS_VALUE && fmt_ctx->duration > 0) {
        wave.duration = Rational(fmt_ctx->duration, AV_TIME_BASE);
    } else {
        size_t total_points = wave.channel_peaks[0].size();
        wave.duration = Rational(static_cast<int64_t>(total_points), static_cast<int64_t>(peaks_per_second));
    }

    return wave;
}

} // namespace ave
