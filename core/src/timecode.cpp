#include "ave/timecode.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace ave {

Timecode Timecode::from_rational(Rational time, Rational frame_rate) {
    if (frame_rate.numerator() <= 0 || frame_rate.denominator() <= 0) {
        return Timecode{};
    }

    // frames = round(time * frame_rate)
    double sec = time.to_seconds();
    double fps = frame_rate.to_seconds();
    int64_t total_f = static_cast<int64_t>(std::llround(sec * fps));
    if (total_f < 0) total_f = 0;

    int fps_int = static_cast<int>(std::llround(fps));
    if (fps_int <= 0) fps_int = 24;

    int64_t f = total_f % fps_int;
    int64_t total_s = total_f / fps_int;
    int64_t s = total_s % 60;
    int64_t total_m = total_s / 60;
    int64_t m = total_m % 60;
    int64_t h = total_m / 60;

    return Timecode{static_cast<int>(h), static_cast<int>(m), static_cast<int>(s), static_cast<int>(f), false};
}

Timecode Timecode::from_string(std::string_view str) {
    if (str.size() < 11) {
        return Timecode{};
    }

    int h = 0, m = 0, s = 0, f = 0;
    char sep = ':';

    // Parse HH:MM:SS:FF or HH:MM:SS;FF
    int count = std::sscanf(str.data(), "%d:%d:%d%c%d", &h, &m, &s, &sep, &f);
    if (count < 5) {
        return Timecode{};
    }

    bool df = (sep == ';');
    return Timecode{h, m, s, f, df};
}

Rational Timecode::to_rational(Rational frame_rate) const {
    if (frame_rate.numerator() <= 0 || frame_rate.denominator() <= 0) {
        return Rational{0, 1};
    }

    int64_t frames_tot = total_frames(frame_rate);
    return Rational{frames_tot * frame_rate.denominator(), frame_rate.numerator()};
}

std::string Timecode::to_string() const {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d%c%02d",
                  hours, minutes, seconds, drop_frame ? ';' : ':', frames);
    return std::string(buf);
}

int64_t Timecode::total_frames(Rational frame_rate) const noexcept {
    int fps = static_cast<int>(std::llround(frame_rate.to_seconds()));
    if (fps <= 0) fps = 24;
    int64_t total_sec = static_cast<int64_t>(hours) * 3600 +
                        static_cast<int64_t>(minutes) * 60 +
                        static_cast<int64_t>(seconds);
    return (total_sec * fps) + frames;
}

} // namespace ave
