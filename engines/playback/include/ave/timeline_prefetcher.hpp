#pragma once

#include "ave/playback_controller.hpp"
#include "ave/playback_snapshot.hpp"
#include "ave/stream_decoder.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace ave {

/**
 * @brief Background asynchronous worker that prefetches decoded video frames
 * from source media assets ahead of the playback clock and buffers them into the
 * BoundedFrameQueue, reading exclusively from immutable PlaybackSnapshots.
 */
class TimelinePrefetcher {
public:
    TimelinePrefetcher(std::shared_ptr<PlaybackSnapshotStore> snapshot_store,
                       PlaybackController& controller,
                       Rational prefetch_window = Rational{1, 1}); // Default 1.0 sec lookahead
    ~TimelinePrefetcher();

    TimelinePrefetcher(const TimelinePrefetcher&) = delete;
    TimelinePrefetcher& operator=(const TimelinePrefetcher&) = delete;

    void start();
    void stop();
    [[nodiscard]] bool is_running() const noexcept;

    /**
     * @brief Signals the prefetch worker to wake up immediately (e.g. after a seek or snapshot update).
     */
    void trigger();

    /**
     * @brief Sets the maximum preview bounds (capped at 1280x720) used when decoding frames.
     */
    void set_max_preview_bounds(int max_width, int max_height) noexcept;

    /**
     * @brief Synchronously decodes and buffers a single frame at the target timeline PTS
     * using the latest immutable PlaybackSnapshot.
     */
    bool prefetch_frame_at(Rational timeline_pts, uint64_t seek_generation);

private:
    std::shared_ptr<PlaybackSnapshotStore> snapshot_store_;
    PlaybackController& controller_;
    Rational prefetch_window_{1, 1};
    PlaybackSnapshotStore::ListenerId snapshot_listener_id_{0};

    std::atomic<int> max_preview_width_{1280};
    std::atomic<int> max_preview_height_{720};

    std::atomic<bool> running_{false};
    std::thread worker_thread_{};
    mutable std::mutex cv_mutex_{};
    std::condition_variable cv_{};
    bool wakeup_requested_{false};

    mutable std::mutex decoder_mutex_{};
    std::shared_ptr<std::atomic<bool>> decode_cancel_state_{};
    StreamDecoder decoder_{};
    std::string decoder_asset_id_{};
    uint64_t decoder_asset_file_size_{0};
    uint64_t last_generation_{0};
    uint64_t last_snapshot_version_{0};
    Rational prefetch_cursor_{0, 1};

    void worker_loop();
};

} // namespace ave
