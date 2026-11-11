#include "ave/ffmpeg_probe.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libavutil/channel_layout.h>
}

#include <sstream>
#include <utility>

namespace ave {
namespace {

int probe_interrupt_cb(void* opaque) noexcept {
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

ProbeResult probe_internal(const std::filesystem::path& logical_path,
                           const CustomProbeIo* custom_io,
                           std::shared_ptr<std::atomic<bool>> cancel_state,
                           uint64_t known_file_size) {
    ProbeResult result;

    // Declare cancel_state BEFORE FFmpeg guards so it strictly outlives avformat_close_input().
    std::shared_ptr<std::atomic<bool>> state =
        cancel_state ? std::move(cancel_state) : std::make_shared<std::atomic<bool>>(false);

    if (state->load(std::memory_order_acquire)) {
        result.cancelled = true;
        result.error_message = "Probe cancelled";
        return result;
    }

    struct AvioGuard {
        AVIOContext* io{nullptr};
        ~AvioGuard() {
            if (io) {
                av_freep(&io->buffer);
                avio_context_free(&io);
            }
        }
    } avio_guard;

    struct FormatGuard {
        AVFormatContext* ctx{nullptr};
        ~FormatGuard() {
            if (ctx) {
                avformat_close_input(&ctx);
            }
        }
    } fmt_guard;

    AVFormatContext* fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        result.error_message = "Failed to allocate AVFormatContext";
        return result;
    }

    fmt_ctx->interrupt_callback.callback = &probe_interrupt_cb;
    fmt_ctx->interrupt_callback.opaque = state.get();

    const std::string path_utf8 = path_to_utf8(logical_path);
    const char* open_url = path_utf8.c_str();

    if (custom_io != nullptr) {
        if (!custom_io->read_packet) {
            avformat_free_context(fmt_ctx);
            result.error_message = "Custom probe I/O read_packet callback is null";
            return result;
        }
        constexpr int kCustomIoBufferSize = 4096;
        auto* io_buffer = static_cast<unsigned char*>(av_malloc(kCustomIoBufferSize));
        if (!io_buffer) {
            avformat_free_context(fmt_ctx);
            result.error_message = "Failed to allocate custom AVIO buffer";
            return result;
        }
        AVIOContext* avio_ctx = avio_alloc_context(
            io_buffer,
            kCustomIoBufferSize,
            0,
            custom_io->opaque,
            custom_io->read_packet,
            nullptr,
            custom_io->seek);
        if (!avio_ctx) {
            av_free(io_buffer);
            avformat_free_context(fmt_ctx);
            result.error_message = "Failed to allocate custom AVIOContext";
            return result;
        }
        avio_guard.io = avio_ctx;
        fmt_ctx->pb = avio_ctx;
        fmt_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;
        open_url = nullptr;
    }

    int err = avformat_open_input(&fmt_ctx, open_url, nullptr, nullptr);
    if (err < 0) {
        if (fmt_ctx) {
            avformat_free_context(fmt_ctx);
            fmt_ctx = nullptr;
        }
        if (err == AVERROR_EXIT || state->load(std::memory_order_acquire)) {
            result.cancelled = true;
            result.error_message = "Probe cancelled during input open";
        } else {
            char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(err, err_buf, sizeof(err_buf));
            result.error_message = "Failed to open input: " + std::string(err_buf);
        }
        return result;
    }

    fmt_guard.ctx = fmt_ctx;

    err = avformat_find_stream_info(fmt_ctx, nullptr);
    if (err < 0 || state->load(std::memory_order_acquire)) {
        if (err == AVERROR_EXIT || state->load(std::memory_order_acquire)) {
            result.cancelled = true;
            result.error_message = "Probe cancelled while reading stream info";
        } else {
            char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(err, err_buf, sizeof(err_buf));
            result.error_message = "Failed to find stream info: " + std::string(err_buf);
        }
        return result;
    }

    std::string container_format =
        (fmt_ctx->iformat && fmt_ctx->iformat->name) ? fmt_ctx->iformat->name : "unknown";

    Rational overall_duration{0, 1};
    if (fmt_ctx->duration != AV_NOPTS_VALUE && fmt_ctx->duration > 0) {
        overall_duration = Rational(fmt_ctx->duration, AV_TIME_BASE);
    }

    std::vector<SourceStream> streams;
    streams.reserve(fmt_ctx->nb_streams);

    for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
        if (state->load(std::memory_order_acquire)) {
            result.cancelled = true;
            result.error_message = "Probe cancelled";
            return result;
        }

        AVStream* st = fmt_ctx->streams[i];
        AVCodecParameters* par = st->codecpar;

        StreamType st_type = StreamType::Unknown;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO) st_type = StreamType::Video;
        else if (par->codec_type == AVMEDIA_TYPE_AUDIO) st_type = StreamType::Audio;
        else if (par->codec_type == AVMEDIA_TYPE_SUBTITLE) st_type = StreamType::Subtitle;
        else if (par->codec_type == AVMEDIA_TYPE_DATA) st_type = StreamType::Data;

        const char* c_name = avcodec_get_name(par->codec_id);
        std::string codec_name = c_name ? c_name : "unknown";

        Rational timebase{1, 1000};
        if (st->time_base.num > 0 && st->time_base.den > 0) {
            timebase = Rational(st->time_base.num, st->time_base.den);
        }

        int64_t duration_ticks = 0;
        if (st->duration != AV_NOPTS_VALUE && st->duration > 0) {
            duration_ticks = st->duration;
        } else if (overall_duration.numerator() > 0) {
            duration_ticks = (overall_duration / timebase).numerator();
        }

        SourceStream stream(static_cast<int>(i), st_type, std::move(codec_name), timebase, duration_ticks);

        if (st_type == StreamType::Video) {
            VideoStreamInfo vi;
            vi.width = par->width;
            vi.height = par->height;

            AVRational fr = av_guess_frame_rate(fmt_ctx, st, nullptr);
            if (fr.num > 0 && fr.den > 0) {
                vi.frame_rate = Rational(fr.num, fr.den);
            } else if (st->avg_frame_rate.num > 0 && st->avg_frame_rate.den > 0) {
                vi.frame_rate = Rational(st->avg_frame_rate.num, st->avg_frame_rate.den);
            } else if (st->r_frame_rate.num > 0 && st->r_frame_rate.den > 0) {
                vi.frame_rate = Rational(st->r_frame_rate.num, st->r_frame_rate.den);
            }

            if (par->sample_aspect_ratio.num > 0 && par->sample_aspect_ratio.den > 0) {
                vi.pixel_aspect_ratio = Rational(par->sample_aspect_ratio.num, par->sample_aspect_ratio.den);
            }

            const char* pix_fmt = av_get_pix_fmt_name(static_cast<AVPixelFormat>(par->format));
            if (pix_fmt) vi.pixel_format = pix_fmt;

            const char* cs = av_color_space_name(par->color_space);
            if (cs) vi.color_space = cs;

            vi.color_range = (par->color_range == AVCOL_RANGE_JPEG) ? "full" : "limited";

            const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(par->format));
            if (desc && desc->nb_components > 0) {
                vi.bit_depth = desc->comp[0].depth;
            }

            stream.set_video_info(std::move(vi));
        } else if (st_type == StreamType::Audio) {
            AudioStreamInfo ai;
            ai.sample_rate = par->sample_rate;
            ai.channels = par->ch_layout.nb_channels;

            char layout_buf[128] = {0};
            av_channel_layout_describe(&par->ch_layout, layout_buf, sizeof(layout_buf));
            if (layout_buf[0] != '\0') {
                ai.channel_layout = layout_buf;
            }

            const char* sample_fmt = av_get_sample_fmt_name(static_cast<AVSampleFormat>(par->format));
            if (sample_fmt) ai.sample_format = sample_fmt;

            int bytes_per_sample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(par->format));
            if (bytes_per_sample > 0) {
                ai.bit_depth = bytes_per_sample * 8;
            }

            stream.set_audio_info(std::move(ai));
        }

        streams.push_back(std::move(stream));
    }

    std::string asset_id = UUID::generate();
    std::string display_name = path_to_utf8(logical_path.filename());

    result.asset = MediaAsset(std::move(asset_id),
                              logical_path,
                              std::move(display_name),
                              known_file_size,
                              std::move(container_format),
                              overall_duration,
                              std::move(streams));
    result.success = true;
    return result;
}

} // namespace

std::string FFmpegProbe::ffmpeg_version_info() {
    std::ostringstream ss;
    ss << "FFmpeg " << av_version_info()
       << " (libavformat: " << LIBAVFORMAT_VERSION_MAJOR << "." << LIBAVFORMAT_VERSION_MINOR
       << ", libavcodec: " << LIBAVCODEC_VERSION_MAJOR << "." << LIBAVCODEC_VERSION_MINOR
       << ", libavutil: " << LIBAVUTIL_VERSION_MAJOR << "." << LIBAVUTIL_VERSION_MINOR << ")";
    return ss.str();
}

ProbeResult FFmpegProbe::probe(const std::filesystem::path& file_path,
                               std::shared_ptr<std::atomic<bool>> cancel_state) {
    if (cancel_state && cancel_state->load(std::memory_order_acquire)) {
        ProbeResult result;
        result.cancelled = true;
        result.error_message = "Probe cancelled";
        return result;
    }

    std::error_code ec;
    if (!std::filesystem::exists(file_path, ec)) {
        ProbeResult result;
        result.success = false;
        result.error_message = "File does not exist: " + path_to_utf8(file_path);
        return result;
    }

    uint64_t file_size = 0;
    const auto sz = std::filesystem::file_size(file_path, ec);
    if (!ec) {
        file_size = sz;
    }

    return probe_internal(file_path, nullptr, std::move(cancel_state), file_size);
}

ProbeResult FFmpegProbe::probe_custom_io(const CustomProbeIo& custom_io,
                                         const std::filesystem::path& logical_path,
                                         std::shared_ptr<std::atomic<bool>> cancel_state) {
    return probe_internal(logical_path, &custom_io, std::move(cancel_state), 0);
}

} // namespace ave

