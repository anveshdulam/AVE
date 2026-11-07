#pragma once

#include "ave/time.hpp"

#include <chrono>
#include <atomic>
#include <mutex>
#include <string>

namespace ave {

enum class PlaybackState {
    Stopped,
    Playing,
    Paused,
    Scrubbing
};

[[nodiscard]] std::string to_string(PlaybackState state);

enum class ClockSource {
    SystemClock,
    AudioMaster,
    External
};

[[nodiscard]] std::string to_string(ClockSource source);

/**
 * @brief Clock driving the playback timeline with exact rational frame stepping,
 * monotonic system clock interpolation, seek generation tracking, and audio master sync.
 */
class PlaybackClock {
public:
    PlaybackClock(Rational frame_rate = Rational{24, 1},
                  Rational duration = Rational{0, 1},
                  ClockSource source = ClockSource::SystemClock);

    [[nodiscard]] PlaybackState state() const noexcept;
    [[nodiscard]] ClockSource clock_source() const noexcept;
    [[nodiscard]] Rational frame_rate() const noexcept;
    [[nodiscard]] Rational sequence_duration() const noexcept;
    [[nodiscard]] double playback_speed() const noexcept;
    [[nodiscard]] uint64_t current_seek_generation() const noexcept;

    void set_clock_source(ClockSource source) noexcept;
    void set_frame_rate(Rational fps);
    void set_sequence_duration(Rational duration);

    /**
     * @brief Starts playback at the specified speed (default 1.0 = normal forward).
     * Negative values indicate reverse playback.
     */
    void play(double speed = 1.0);

    /**
     * @brief Pauses playback, freezing the playhead at the current position.
     */
    void pause();

    /**
     * @brief Stops playback and resets the playhead to time 0.
     */
    void stop();

    /**
     * @brief Seeks to a specific rational time. Increments the seek generation token.
     * @return New seek generation token.
     */
    uint64_t seek(Rational time);

    /**
     * @brief Begins scrubbing mode at the given rational position.
     */
    uint64_t start_scrub(Rational initial_time);

    /**
     * @brief Updates scrub position. Rapid calls update current time without resetting generation.
     */
    void update_scrub(Rational time);

    /**
     * @brief Ends scrubbing mode, leaving the clock paused at the scrub position.
     */
    void end_scrub();

    /**
     * @brief Steps forward by the specified number of frames (default 1).
     */
    void step_forward(int frames = 1);

    /**
     * @brief Steps backward by the specified number of frames (default 1).
     */
    void step_backward(int frames = 1);

    /**
     * @brief Returns the current exact rational time of the playhead.
     */
    [[nodiscard]] Rational current_time() const;

    /**
     * @brief Updates the clock reference when using ClockSource::AudioMaster.
     */
    void sync_audio_time(Rational audio_pts);

private:
    mutable std::mutex mutex_{};
    PlaybackState state_{PlaybackState::Stopped};
    ClockSource clock_source_{ClockSource::SystemClock};
    Rational frame_rate_{24, 1};
    Rational duration_{0, 1};
    double speed_{1.0};
    Rational current_time_{0, 1};

    std::atomic<uint64_t> seek_generation_{0};

    // Monotonic anchor point for SystemClock interpolation
    std::chrono::steady_clock::time_point anchor_system_time_{};
    Rational anchor_timeline_time_{0, 1};

    void clamp_time(Rational& time) const;
};

} // namespace ave

