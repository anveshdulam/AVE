#include "ave/playback_controller.hpp"

namespace ave {

std::string to_string(FrameSyncAction action) {
    switch (action) {
        case FrameSyncAction::Present: return "Present";
        case FrameSyncAction::Hold:    return "Hold";
        case FrameSyncAction::Drop:    return "Drop";
        case FrameSyncAction::Stale:   return "Stale";
        default:                       return "Unknown";
    }
}

PlaybackController::PlaybackController(Rational frame_rate, Rational duration, size_t queue_capacity)
    : clock_(frame_rate, duration),
      video_queue_(queue_capacity) {
    Rational fps = clock_.frame_rate();
    // 1/2 frame tolerance
    sync_tolerance_ = Rational{fps.denominator(), fps.numerator() * 2};
}

FrameSyncAction PlaybackController::evaluate_frame(const VideoFrame& frame, Rational clock_time) const {
    if (frame.seek_generation != clock_.current_seek_generation()) {
        return FrameSyncAction::Stale;
    }

    Rational frame_end = frame.pts_end();
    if (frame_end < clock_time - sync_tolerance_) {
        // Frame is in the past past tolerance window -> drop to catch up
        return FrameSyncAction::Drop;
    }

    if (frame.pts > clock_time + sync_tolerance_) {
        // Frame is in the future -> hold until clock advances
        return FrameSyncAction::Hold;
    }

    return FrameSyncAction::Present;
}

std::optional<VideoFrame> PlaybackController::fetch_next_display_frame() {
    Rational now = clock_.current_time();

    while (true) {
        auto peeked = video_queue_.peek();
        if (!peeked) {
            return std::nullopt;
        }

        FrameSyncAction action = evaluate_frame(*peeked, now);

        if (action == FrameSyncAction::Stale || action == FrameSyncAction::Drop) {
            video_queue_.pop(std::chrono::milliseconds(0));
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            metrics_.frames_dropped++;
            continue;
        }

        if (action == FrameSyncAction::Hold) {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            metrics_.frames_held++;
            return std::nullopt;
        }

        // Present
        auto frame = video_queue_.pop(std::chrono::milliseconds(0));
        if (frame) {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            metrics_.frames_rendered++;
        }
        return frame;
    }
}

void PlaybackController::seek(Rational target_time) {
    clock_.seek(target_time);
    video_queue_.clear();
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_.seeks_performed++;
}

PlaybackMetrics PlaybackController::metrics() const noexcept {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return metrics_;
}

void PlaybackController::reset_metrics() noexcept {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_ = PlaybackMetrics{};
}

} // namespace ave
