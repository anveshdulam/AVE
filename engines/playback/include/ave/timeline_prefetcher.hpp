#pragma once

#include "ave/timeline.hpp"
#include "ave/media_pool.hpp"
#include "ave/playback_controller.hpp"
#include "ave/stream_decoder.hpp"

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <string>

namespace ave {

/**
 * @brief Background asynchronous worker that prefetches decoded video frames
 * from source media assets ahead of the playback clock and buffers them into the
 * BoundedFrameQueue.
 */
class TimelinePrefetcher {
public:
    TimelinePrefetcher(const Timeline& timeline,
                       const MediaPool& media_pool,
                       PlaybackController& controller,
                       Rational prefetch_window = Rational{1, 1}); // Default 1.0 sec lookahead
    ~TimelinePrefetcher();

    TimelinePrefetcher(const TimelinePrefetcher&) = delete;
    TimelinePrefetcher& operator=(const TimelinePrefetcher&) = delete;

    void start();
    void stop();
    [[nodiscard]] bool is_running() const noexcept;

    /**
     * @brief Signals the prefetch worker to wake up immediately (e.g. after a seek).
     */
    void trigger();

    /**
     * @brief Synchronously decodes and buffers a single frame at the target timeline PTS.
     * Useful for deterministic testing or single-frame scrubbing.
     */
    bool prefetch_frame_at(Rational timeline_pts, uint64_t seek_generation);

private:
    const Timeline& timeline_;
    const MediaPool& media_pool_;
    PlaybackController& controller_;
    Rational prefetch_window_{1, 1};

    std::atomic<bool> running_{false};
    std::thread worker_thread_{};
    std::mutex cv_mutex_{};
    std::condition_variable cv_{};

    StreamDecoder decoder_{};
    uint64_t last_generation_{0};
    Rational prefetch_cursor_{0, 1};

    void worker_loop();
};

} // namespace ave

