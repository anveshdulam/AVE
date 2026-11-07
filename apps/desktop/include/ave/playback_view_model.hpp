#pragma once

#include "ave/playback_session.hpp"
#include "ave/timecode.hpp"
#include <string>
#include <functional>

namespace ave {

/**
 * @brief ViewModel for the transport bar and player display.
 * Exposes playback commands, transport metrics, and SMPTE timecode.
 */
class PlaybackViewModel {
public:
    explicit PlaybackViewModel(PlaybackSession& session, Rational sequence_fps = Rational{24, 1});

    [[nodiscard]] bool is_playing() const noexcept;
    [[nodiscard]] bool is_paused() const noexcept;
    [[nodiscard]] double speed() const noexcept;
    [[nodiscard]] Rational current_pts() const noexcept;
    [[nodiscard]] std::string current_timecode() const;

    void play();
    void pause();
    void toggle_play();
    void stop();
    void seek(Rational target_pts);
    void seek_timecode(const std::string& timecode_str);
    void step_forward(int64_t frames = 1);
    void step_backward(int64_t frames = 1);
    void set_speed(double speed);

    [[nodiscard]] SessionMetrics metrics() const;

    void set_on_state_changed(std::function<void()> callback) { on_state_changed_ = std::move(callback); }

private:
    PlaybackSession& session_;
    Rational sequence_fps_{24, 1};
    std::function<void()> on_state_changed_{};

    void notify_state_changed();
};

} // namespace ave
