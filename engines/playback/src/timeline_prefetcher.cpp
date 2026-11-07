#include "ave/timeline_prefetcher.hpp"

#include <chrono>

namespace ave {

TimelinePrefetcher::TimelinePrefetcher(const Timeline& timeline,
                                       const MediaPool& media_pool,
                                       PlaybackController& controller,
                                       Rational prefetch_window)
    : timeline_(timeline),
      media_pool_(media_pool),
      controller_(controller),
      prefetch_window_(prefetch_window) {}

TimelinePrefetcher::~TimelinePrefetcher() {
    stop();
}

void TimelinePrefetcher::start() {
    if (running_.load()) return;
    running_ = true;
    last_generation_ = controller_.clock().current_seek_generation();
    prefetch_cursor_ = controller_.clock().current_time();
    worker_thread_ = std::thread(&TimelinePrefetcher::worker_loop, this);
}

void TimelinePrefetcher::stop() {
    if (!running_.load()) return;
    running_ = false;
    cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    decoder_.close();
}

bool TimelinePrefetcher::is_running() const noexcept {
    return running_.load();
}

void TimelinePrefetcher::trigger() {
    cv_.notify_all();
}

bool TimelinePrefetcher::prefetch_frame_at(Rational timeline_pts, uint64_t seek_generation) {
    auto active_items = timeline_.active_items_at(timeline_pts);
    const TimelineItem* video_item = nullptr;

    for (const auto* it : active_items) {
        for (const auto& vt : timeline_.video_tracks()) {
            if (vt.find_item(it->id())) {
                video_item = it;
                break;
            }
        }
        if (video_item) break;
    }

    Rational frame_dur = Rational{timeline_.frame_rate().denominator(), timeline_.frame_rate().numerator()};

    if (video_item) {
        const auto* asset = media_pool_.find_asset(video_item->asset_id());
        if (!asset) return false;

        if (!decoder_.is_open() || decoder_.file_path() != asset->file_path()) {
            if (!decoder_.open(asset->file_path())) {
                return false;
            }
        }

        Rational source_pts = video_item->source_time_at(timeline_pts);
        decoder_.seek_pts(source_pts);

        auto frame = decoder_.decode_next_frame(seek_generation, timeline_.width(), timeline_.height());
        if (frame.has_value()) {
            frame->pts = timeline_pts;
            frame->duration = frame_dur;
            return controller_.video_queue().push(std::move(*frame), std::chrono::milliseconds(50));
        }
        return false;
    }

    // Gap on timeline: generate black placeholder frame
    VideoFrame black_frame;
    black_frame.seek_generation = seek_generation;
    black_frame.pts = timeline_pts;
    black_frame.duration = frame_dur;
    black_frame.width = timeline_.width();
    black_frame.height = timeline_.height();
    black_frame.rgba_data = std::vector<uint8_t>(static_cast<size_t>(timeline_.width()) * static_cast<size_t>(timeline_.height()) * 4, 0);
    black_frame.is_keyframe = true;

    return controller_.video_queue().push(std::move(black_frame), std::chrono::milliseconds(50));
}

void TimelinePrefetcher::worker_loop() {
    Rational frame_dur = Rational{timeline_.frame_rate().denominator(), timeline_.frame_rate().numerator()};

    while (running_.load()) {
        uint64_t current_gen = controller_.clock().current_seek_generation();
        Rational clock_now = controller_.clock().current_time();

        if (current_gen != last_generation_) {
            last_generation_ = current_gen;
            prefetch_cursor_ = clock_now;
        }

        if (controller_.video_queue().is_full()) {
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(15), [this] {
                return !running_.load();
            });
            continue;
        }

        Rational max_prefetch = clock_now + prefetch_window_;
        if (prefetch_cursor_ < max_prefetch) {
            prefetch_frame_at(prefetch_cursor_, current_gen);
            prefetch_cursor_ = prefetch_cursor_ + frame_dur;
        } else {
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(15), [this] {
                return !running_.load();
            });
        }
    }
}

} // namespace ave
