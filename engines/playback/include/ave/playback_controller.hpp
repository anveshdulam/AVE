#pragma once

#include "ave/playback_clock.hpp"
#include "ave/frame_queue.hpp"

#include <memory>
#include <atomic>

namespace ave {

enum class FrameSyncAction {
    Present,
    Hold,
    Drop,
    Stale
};

[[nodiscard]] std::string to_string(FrameSyncAction action);

struct PlaybackMetrics {
    uint64_t frames_rendered{0};
    uint64_t frames_dropped{0};
    uint64_t frames_held{0};
    uint64_t seeks_performed{0};
};

/**
 * @brief Coordinates timeline clock, bounded frame buffering, A/V synchronization,
 * and presentation dispatch.
 */
class PlaybackController {
public:
    explicit PlaybackController(Rational frame_rate = Rational{24, 1},
                                Rational duration = Rational{0, 1},
                                size_t queue_capacity = 12);

    [[nodiscard]] PlaybackClock& clock() noexcept { return clock_; }
    [[nodiscard]] const PlaybackClock& clock() const noexcept { return clock_; }

    [[nodiscard]] BoundedFrameQueue<VideoFrame>& video_queue() noexcept { return video_queue_; }
    [[nodiscard]] const BoundedFrameQueue<VideoFrame>& video_queue() const noexcept { return video_queue_; }

    /**
     * @brief Evaluates an incoming video frame against the current clock time.
     */
    [[nodiscard]] FrameSyncAction evaluate_frame(const VideoFrame& frame, Rational clock_time) const;

    /**
     * @brief Fetches the next frame ready for presentation. Discards stale or overdue frames.
     * @return VideoFrame if a frame is ready for presentation now, or std::nullopt if held/empty.
     */
    std::optional<VideoFrame> fetch_next_display_frame();

    void seek(Rational target_time);

    [[nodiscard]] PlaybackMetrics metrics() const noexcept;
    void reset_metrics() noexcept;

private:
    PlaybackClock clock_;
    BoundedFrameQueue<VideoFrame> video_queue_;

    mutable std::mutex metrics_mutex_{};
    PlaybackMetrics metrics_{};

    // Frame synchronization tolerance (e.g. 1/2 frame duration)
    Rational sync_tolerance_{};
};

} // namespace ave
