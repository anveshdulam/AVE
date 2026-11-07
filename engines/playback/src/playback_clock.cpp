#include "ave/playback_clock.hpp"

#include <cmath>
#include <algorithm>

namespace ave {

std::string to_string(PlaybackState state) {
    switch (state) {
        case PlaybackState::Stopped:   return "Stopped";
        case PlaybackState::Playing:   return "Playing";
        case PlaybackState::Paused:    return "Paused";
        case PlaybackState::Scrubbing: return "Scrubbing";
        default:                       return "Unknown";
    }
}

std::string to_string(ClockSource source) {
    switch (source) {
        case ClockSource::SystemClock: return "SystemClock";
        case ClockSource::AudioMaster: return "AudioMaster";
        case ClockSource::External:    return "External";
        default:                       return "Unknown";
    }
}

PlaybackClock::PlaybackClock(Rational frame_rate, Rational duration, ClockSource source)
    : clock_source_(source),
      frame_rate_(frame_rate),
      duration_(duration) {
    if (frame_rate_.numerator() <= 0 || frame_rate_.denominator() <= 0) {
        frame_rate_ = Rational{24, 1};
    }
}

PlaybackState PlaybackClock::state() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

ClockSource PlaybackClock::clock_source() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return clock_source_;
}

Rational PlaybackClock::frame_rate() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return frame_rate_;
}

Rational PlaybackClock::sequence_duration() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return duration_;
}

double PlaybackClock::playback_speed() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return speed_;
}

uint64_t PlaybackClock::current_seek_generation() const noexcept {
    return seek_generation_.load(std::memory_order_acquire);
}

void PlaybackClock::set_clock_source(ClockSource source) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    clock_source_ = source;
}

void PlaybackClock::set_frame_rate(Rational fps) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fps.numerator() > 0 && fps.denominator() > 0) {
        frame_rate_ = fps;
    }
}

void PlaybackClock::set_sequence_duration(Rational duration) {
    std::lock_guard<std::mutex> lock(mutex_);
    duration_ = duration;
    clamp_time(current_time_);
}

void PlaybackClock::play(double speed) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == PlaybackState::Playing && std::abs(speed_ - speed) < 1e-6) {
        return;
    }

    if (state_ == PlaybackState::Playing) {
        // Freeze elapsed time to current_time_ before changing speed/anchoring
        auto now = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - anchor_system_time_).count();
        Rational elapsed{elapsed_us, 1000000};
        Rational scaled_elapsed = elapsed * Rational{static_cast<int64_t>(std::round(speed_ * 1000)), 1000};
        current_time_ = anchor_timeline_time_ + scaled_elapsed;
        clamp_time(current_time_);
    }

    speed_ = speed;
    state_ = PlaybackState::Playing;
    anchor_system_time_ = std::chrono::steady_clock::now();
    anchor_timeline_time_ = current_time_;
}

void PlaybackClock::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == PlaybackState::Playing) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - anchor_system_time_).count();
        Rational elapsed{elapsed_us, 1000000};
        Rational scaled_elapsed = elapsed * Rational{static_cast<int64_t>(std::round(speed_ * 1000)), 1000};
        current_time_ = anchor_timeline_time_ + scaled_elapsed;
        clamp_time(current_time_);
    }
    state_ = PlaybackState::Paused;
}

void PlaybackClock::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = PlaybackState::Stopped;
    current_time_ = Rational{0, 1};
    anchor_timeline_time_ = Rational{0, 1};
    anchor_system_time_ = std::chrono::steady_clock::now();
}

uint64_t PlaybackClock::seek(Rational time) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t new_gen = seek_generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
    clamp_time(time);
    current_time_ = time;

    if (state_ == PlaybackState::Playing) {
        anchor_system_time_ = std::chrono::steady_clock::now();
        anchor_timeline_time_ = current_time_;
    }
    return new_gen;
}

uint64_t PlaybackClock::start_scrub(Rational initial_time) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = PlaybackState::Scrubbing;
    uint64_t new_gen = seek_generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
    clamp_time(initial_time);
    current_time_ = initial_time;
    anchor_timeline_time_ = current_time_;
    return new_gen;
}

void PlaybackClock::update_scrub(Rational time) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == PlaybackState::Scrubbing) {
        clamp_time(time);
        current_time_ = time;
        anchor_timeline_time_ = current_time_;
    }
}

void PlaybackClock::end_scrub() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == PlaybackState::Scrubbing) {
        state_ = PlaybackState::Paused;
    }
}

void PlaybackClock::step_forward(int frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = PlaybackState::Paused;
    Rational frame_dur{frame_rate_.denominator(), frame_rate_.numerator()};
    Rational step = frame_dur * Rational{frames, 1};
    current_time_ = current_time_ + step;
    clamp_time(current_time_);
    seek_generation_.fetch_add(1, std::memory_order_acq_rel);
}

void PlaybackClock::step_backward(int frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = PlaybackState::Paused;
    Rational frame_dur{frame_rate_.denominator(), frame_rate_.numerator()};
    Rational step = frame_dur * Rational{frames, 1};
    current_time_ = current_time_ - step;
    clamp_time(current_time_);
    seek_generation_.fetch_add(1, std::memory_order_acq_rel);
}

Rational PlaybackClock::current_time() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == PlaybackState::Playing && clock_source_ == ClockSource::SystemClock) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - anchor_system_time_).count();
        Rational elapsed{elapsed_us, 1000000};
        Rational scaled_elapsed = elapsed * Rational{static_cast<int64_t>(std::round(speed_ * 1000)), 1000};
        Rational pos = anchor_timeline_time_ + scaled_elapsed;
        clamp_time(pos);
        return pos;
    }
    return current_time_;
}

void PlaybackClock::sync_audio_time(Rational audio_pts) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (clock_source_ == ClockSource::AudioMaster && state_ == PlaybackState::Playing) {
        clamp_time(audio_pts);
        current_time_ = audio_pts;
        anchor_timeline_time_ = audio_pts;
        anchor_system_time_ = std::chrono::steady_clock::now();
    }
}

void PlaybackClock::clamp_time(Rational& time) const {
    if (time.numerator() < 0) {
        time = Rational{0, 1};
    } else if (duration_.numerator() > 0 && time > duration_) {
        time = duration_;
    }
}

} // namespace ave
