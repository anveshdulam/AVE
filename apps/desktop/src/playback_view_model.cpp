#include "ave/playback_view_model.hpp"

namespace ave {

PlaybackViewModel::PlaybackViewModel(PlaybackSession& session, Rational sequence_fps)
    : session_(session),
      sequence_fps_(sequence_fps.numerator() > 0 ? sequence_fps : Rational{24, 1}) {}

bool PlaybackViewModel::is_playing() const noexcept {
    return session_.state() == PlaybackState::Playing;
}

bool PlaybackViewModel::is_paused() const noexcept {
    return session_.state() == PlaybackState::Paused;
}

double PlaybackViewModel::speed() const noexcept {
    return session_.speed();
}

Rational PlaybackViewModel::current_pts() const noexcept {
    return session_.current_time();
}

std::string PlaybackViewModel::current_timecode() const {
    return Timecode::from_rational(session_.current_time(), sequence_fps_).to_string();
}

void PlaybackViewModel::play() {
    session_.play();
    notify_state_changed();
}

void PlaybackViewModel::pause() {
    session_.pause();
    notify_state_changed();
}

void PlaybackViewModel::toggle_play() {
    session_.toggle_play();
    notify_state_changed();
}

void PlaybackViewModel::stop() {
    session_.stop();
    notify_state_changed();
}

void PlaybackViewModel::seek(Rational target_pts) {
    session_.seek(target_pts);
    notify_state_changed();
}

void PlaybackViewModel::seek_timecode(const std::string& timecode_str) {
    Timecode tc = Timecode::from_string(timecode_str);
    seek(tc.to_rational(sequence_fps_));
}

void PlaybackViewModel::step_forward(int64_t frames) {
    session_.step(frames);
    notify_state_changed();
}

void PlaybackViewModel::step_backward(int64_t frames) {
    session_.step(-frames);
    notify_state_changed();
}

void PlaybackViewModel::set_speed(double speed) {
    session_.set_speed(speed);
    notify_state_changed();
}

SessionMetrics PlaybackViewModel::metrics() const {
    return session_.metrics();
}

void PlaybackViewModel::notify_state_changed() {
    if (on_state_changed_) {
        on_state_changed_();
    }
}

} // namespace ave

