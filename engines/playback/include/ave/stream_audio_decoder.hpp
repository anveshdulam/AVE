#pragma once

#include "ave/time.hpp"
#include "ave/audio_buffer.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;

namespace ave {

/**
 * @brief Thread-safe (per instance) FFmpeg audio stream decoder with sample-accurate
 * seeking and resampling to interleaved 32-bit float PCM at target sample rate and channel layout.
 */
class StreamAudioDecoder {
public:
    StreamAudioDecoder();
    ~StreamAudioDecoder();

    StreamAudioDecoder(const StreamAudioDecoder&) = delete;
    StreamAudioDecoder& operator=(const StreamAudioDecoder&) = delete;

    StreamAudioDecoder(StreamAudioDecoder&& other) noexcept;
    StreamAudioDecoder& operator=(StreamAudioDecoder&& other) noexcept;

    /**
     * @brief Opens the media file, discovers the audio stream, and initializes decoder & resampler.
     */
    bool open(const std::filesystem::path& file_path,
              uint32_t target_sample_rate = 48000,
              int target_channels = 2);

    /**
     * @brief Closes decoder and releases all FFmpeg resources.
     */
    void close();

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] const std::filesystem::path& file_path() const noexcept { return file_path_; }
    [[nodiscard]] uint32_t sample_rate() const noexcept { return target_sample_rate_; }
    [[nodiscard]] int channels() const noexcept { return target_channels_; }
    [[nodiscard]] Rational timebase() const noexcept { return timebase_; }
    [[nodiscard]] Rational duration() const noexcept { return duration_; }

    /**
     * @brief Seeks to keyframe preceding target_pts and flushes decoder buffers.
     */
    bool seek_pts(Rational target_pts);

    /**
     * @brief Decodes the next available audio packet, resamples to float PCM,
     * and returns the decoded AudioBuffer.
     */
    std::optional<AudioBuffer> decode_next_audio();

private:
    std::filesystem::path file_path_{};
    AVFormatContext* format_ctx_{nullptr};
    AVCodecContext* codec_ctx_{nullptr};
    AVFrame* raw_frame_{nullptr};
    AVPacket* packet_{nullptr};
    SwrContext* swr_ctx_{nullptr};

    int audio_stream_index_{-1};
    uint32_t target_sample_rate_{48000};
    int target_channels_{2};
    Rational timebase_{1, 48000};
    Rational duration_{0, 1};
    Rational last_decoded_pts_{0, 1};

    void free_resources() noexcept;
};

} // namespace ave

