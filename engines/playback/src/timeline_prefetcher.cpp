#include "ave/timeline_prefetcher.hpp"
#include "ave/video_frame_packet.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace ave {

namespace {

Rational safe_frame_rate(Rational fps) noexcept {
    return (fps.numerator() > 0 && fps.denominator() > 0) ? fps : Rational{24, 1};
}

} // namespace

TimelinePrefetcher::TimelinePrefetcher(std::shared_ptr<PlaybackSnapshotStore> snapshot_store,
                                       PlaybackController& controller,
                                       Rational prefetch_window)
    : snapshot_store_(snapshot_store ? std::move(snapshot_store) : std::make_shared<PlaybackSnapshotStore>()),
      controller_(controller),
      prefetch_window_(prefetch_window),
      decode_cancel_state_(std::make_shared<std::atomic<bool>>(false)) {
    snapshot_listener_id_ = snapshot_store_->add_listener([this](const PlaybackSnapshotPtr&) {
        trigger();
    });
}

TimelinePrefetcher::~TimelinePrefetcher() {
    if (snapshot_store_ && snapshot_listener_id_ != 0) {
        snapshot_store_->remove_listener(snapshot_listener_id_);
        snapshot_listener_id_ = 0;
    }
    stop();
}

void TimelinePrefetcher::start() {
    const uint64_t cur_gen = controller_.clock().current_seek_generation();
    const uint64_t cur_snap_ver = snapshot_store_ ? snapshot_store_->current_version() : 0;
    const Rational cur_time = controller_.clock().current_time();

    std::lock_guard<std::mutex> lock(cv_mutex_);
    if (running_.load(std::memory_order_acquire)) {
        return;
    }
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    {
        std::lock_guard<std::mutex> dec_lock(decoder_mutex_);
        decode_cancel_state_ = std::make_shared<std::atomic<bool>>(false);
    }
    wakeup_requested_ = false;
    last_generation_ = cur_gen;
    last_snapshot_version_ = cur_snap_ver;
    prefetch_cursor_ = cur_time;
    running_.store(true, std::memory_order_release);
    worker_thread_ = std::thread(&TimelinePrefetcher::worker_loop, this);
}

void TimelinePrefetcher::stop() {
    std::thread to_join;
    {
        std::lock_guard<std::mutex> lock(cv_mutex_);
        running_.store(false, std::memory_order_release);
        wakeup_requested_ = true;
        to_join = std::move(worker_thread_);
    }
    {
        std::lock_guard<std::mutex> dec_lock(decoder_mutex_);
        if (decode_cancel_state_) {
            decode_cancel_state_->store(true, std::memory_order_release);
        }
    }
    cv_.notify_all();
    if (to_join.joinable()) {
        to_join.join();
    }
    {
        std::lock_guard<std::mutex> dec_lock(decoder_mutex_);
        decoder_.close();
        decoder_asset_id_.clear();
        decoder_asset_file_size_ = 0;
        decode_cancel_state_ = std::make_shared<std::atomic<bool>>(false);
    }
}

bool TimelinePrefetcher::is_running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

void TimelinePrefetcher::trigger() {
    {
        std::lock_guard<std::mutex> lock(cv_mutex_);
        wakeup_requested_ = true;
    }
    cv_.notify_all();
}

void TimelinePrefetcher::set_max_preview_bounds(int max_width, int max_height) noexcept {
    max_preview_width_.store(std::clamp(max_width, 2, kMaxPreviewWidth), std::memory_order_release);
    max_preview_height_.store(std::clamp(max_height, 2, kMaxPreviewHeight), std::memory_order_release);
}

bool TimelinePrefetcher::prefetch_frame_at(Rational timeline_pts, uint64_t seek_generation) {
    const PlaybackSnapshotPtr snap = snapshot_store_ ? snapshot_store_->load() : nullptr;
    if (!snap) {
        return false;
    }
    const uint64_t snap_version = snap->version;

    const TimelineItem* video_item = nullptr;
    for (const auto& vt : snap->timeline.video_tracks()) {
        if (vt.muted()) {
            continue;
        }
        const TimelineItem* item = vt.item_at(timeline_pts);
        if (item && item->enabled()) {
            video_item = item;
            break;
        }
    }

    const Rational fps = safe_frame_rate(snap->timeline.frame_rate());
    const Rational frame_dur{fps.denominator(), fps.numerator()};
    int target_w = 0;
    int target_h = 0;
    clamp_preview_dimensions(snap->timeline.width(),
                             snap->timeline.height(),
                             target_w,
                             target_h,
                             max_preview_width_.load(std::memory_order_acquire),
                             max_preview_height_.load(std::memory_order_acquire));

    const auto is_superseded = [&]() -> bool {
        if (snapshot_store_ && snapshot_store_->current_version() != snap_version) {
            return true;
        }
        if (seek_generation != controller_.clock().current_seek_generation()) {
            return true;
        }
        return false;
    };

    if (video_item) {
        const MediaAsset* asset = snap->find_asset(video_item->asset_id());
        if (!asset) {
            std::lock_guard<std::mutex> dec_lock(decoder_mutex_);
            if (decoder_.is_open() && !decoder_asset_id_.empty() &&
                !snap->find_asset(decoder_asset_id_)) {
                decoder_.close();
                decoder_asset_id_.clear();
                decoder_asset_file_size_ = 0;
            }
            return false;
        }

        std::optional<VideoFrame> decoded_frame;
        {
            std::lock_guard<std::mutex> dec_lock(decoder_mutex_);
            if (!decode_cancel_state_) {
                decode_cancel_state_ = std::make_shared<std::atomic<bool>>(false);
            }
            const bool need_reopen = !decoder_.is_open() ||
                                     decoder_.file_path() != asset->file_path() ||
                                     decoder_asset_id_ != asset->id() ||
                                     decoder_asset_file_size_ != asset->file_size_bytes();
            if (need_reopen) {
                if (!decoder_.open(asset->file_path(), decode_cancel_state_)) {
                    decoder_asset_id_.clear();
                    decoder_asset_file_size_ = 0;
                    return false;
                }
                decoder_asset_id_ = asset->id();
                decoder_asset_file_size_ = asset->file_size_bytes();
            }

            const Rational source_pts = video_item->source_time_at(timeline_pts);
            decoder_.seek_pts(source_pts);
            decoded_frame = decoder_.decode_next_frame(seek_generation, target_w, target_h);
        }

        if (!decoded_frame.has_value() || is_superseded()) {
            return false;
        }

        decoded_frame->pts = timeline_pts;
        decoded_frame->duration = frame_dur;
        decoded_frame->snapshot_version = snap_version;
        return controller_.video_queue().push(std::move(*decoded_frame), std::chrono::milliseconds(50));
    }

    {
        std::lock_guard<std::mutex> dec_lock(decoder_mutex_);
        if (decoder_.is_open() && !decoder_asset_id_.empty() &&
            !snap->find_asset(decoder_asset_id_)) {
            decoder_.close();
            decoder_asset_id_.clear();
            decoder_asset_file_size_ = 0;
        }
    }

    if (is_superseded()) {
        return false;
    }

    // Gap on timeline: generate black placeholder frame
    VideoFrame black_frame;
    black_frame.seek_generation = seek_generation;
    black_frame.snapshot_version = snap_version;
    black_frame.pts = timeline_pts;
    black_frame.duration = frame_dur;
    black_frame.width = target_w;
    black_frame.height = target_h;
    black_frame.rgba_data = std::vector<uint8_t>(
        static_cast<size_t>(target_w) * static_cast<size_t>(target_h) * 4, 0);
    black_frame.is_keyframe = true;

    return controller_.video_queue().push(std::move(black_frame), std::chrono::milliseconds(50));
}

void TimelinePrefetcher::worker_loop() {
    while (running_.load(std::memory_order_acquire)) {
        const PlaybackSnapshotPtr snap = snapshot_store_ ? snapshot_store_->load() : nullptr;
        const uint64_t current_snap_ver = snap ? snap->version : 0;
        const uint64_t current_gen = controller_.clock().current_seek_generation();
        const Rational clock_now = controller_.clock().current_time();

        const Rational fps = snap ? safe_frame_rate(snap->timeline.frame_rate()) : Rational{24, 1};
        const Rational frame_dur{fps.denominator(), fps.numerator()};

        if (current_gen != last_generation_ || current_snap_ver != last_snapshot_version_) {
            last_generation_ = current_gen;
            last_snapshot_version_ = current_snap_ver;
            prefetch_cursor_ = clock_now;
        }

        if (controller_.video_queue().is_full()) {
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(15), [this] {
                return !running_.load(std::memory_order_acquire) || wakeup_requested_;
            });
            wakeup_requested_ = false;
            continue;
        }

        const Rational max_prefetch = clock_now + prefetch_window_;
        if (prefetch_cursor_ < max_prefetch) {
            prefetch_frame_at(prefetch_cursor_, current_gen);
            if (!running_.load(std::memory_order_acquire)) {
                break;
            }
            if (snapshot_store_ && snapshot_store_->current_version() == current_snap_ver &&
                controller_.clock().current_seek_generation() == current_gen) {
                prefetch_cursor_ = prefetch_cursor_ + frame_dur;
            }
        } else {
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(15), [this] {
                return !running_.load(std::memory_order_acquire) || wakeup_requested_;
            });
            wakeup_requested_ = false;
        }
    }
}

} // namespace ave
