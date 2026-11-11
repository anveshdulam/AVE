#pragma once

#include "ave/time.hpp"
#include "ave/frame_queue.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace ave {

/**
 * @brief Thread-safe (per instance) FFmpeg media stream decoder with frame-accurate
 * seeking, RGBA frame scaling, exact rational timestamps, and cooperative AVIOInterruptCB cancellation.
 */
class StreamDecoder {
public:
    StreamDecoder();
    ~StreamDecoder();

    StreamDecoder(const StreamDecoder&) = delete;
    StreamDecoder& operator=(const StreamDecoder&) = delete;

    StreamDecoder(StreamDecoder&& other) noexcept;
    StreamDecoder& operator=(StreamDecoder&& other) noexcept;

    /**
     * @brief Opens the media file, discovers streams, and initializes the video decoder.
     */
    bool open(const std::filesystem::path& file_path,
              std::shared_ptr<std::atomic<bool>> cancel_state = nullptr);

    /**
     * @brief Requests cooperative cancellation of blocking FFmpeg operations.
     */
    void request_cancel() noexcept;

    /**
     * @brief Returns true if cancellation has been requested for the active session.
     */
    [[nodiscard]] bool is_cancelled() const noexcept;

    /**
     * @brief Closes decoder and releases all FFmpeg resources.
     */
    void close();

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] const std::filesystem::path& file_path() const noexcept { return file_path_; }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] Rational frame_rate() const noexcept { return frame_rate_; }
    [[nodiscard]] Rational timebase() const noexcept { return timebase_; }
    [[nodiscard]] Rational duration() const noexcept { return duration_; }

    /**
     * @brief Seeks to the keyframe preceding target_pts, flushes decoder buffers,
     * and decodes forward to achieve frame-accurate positioning.
     */
    bool seek_pts(Rational target_pts);

    /**
     * @brief Decodes the next available video frame, converting to RGBA format.
     * @param seek_generation Generation token assigned to the resulting frame.
     * @param target_w Desired width (0 = source width).
     * @param target_h Desired height (0 = source height).
     */
    std::optional<VideoFrame> decode_next_frame(uint64_t seek_generation,
                                               int target_w = 0,
                                               int target_h = 0);

private:
    std::filesystem::path file_path_{};
    std::shared_ptr<std::atomic<bool>> cancel_state_{};
    AVFormatContext* format_ctx_{nullptr};
    AVCodecContext* codec_ctx_{nullptr};
    AVFrame* raw_frame_{nullptr};
    AVPacket* packet_{nullptr};
    SwsContext* sws_ctx_{nullptr};

    int sws_width_{0};
    int sws_height_{0};
    int sws_src_format_{-1};

    int video_stream_index_{-1};
    int width_{0};
    int height_{0};
    Rational frame_rate_{24, 1};
    Rational timebase_{1, 1000};
    Rational duration_{0, 1};
    Rational last_decoded_pts_{0, 1};

    void free_resources() noexcept;
};

} // namespace ave

