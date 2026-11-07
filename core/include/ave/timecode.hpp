#pragma once

#include "ave/time.hpp"
#include <string>
#include <string_view>
#include <cstdint>

namespace ave {

/**
 * @brief SMPTE Non-Drop Frame and Drop Frame Timecode representation.
 * Computes exact HH:MM:SS:FF representation from Rational time and sequence frame rate.
 */
class Timecode {
public:
    int hours{0};
    int minutes{0};
    int seconds{0};
    int frames{0};
    bool drop_frame{false};

    constexpr Timecode() = default;
    constexpr Timecode(int h, int m, int s, int f, bool df = false)
        : hours(h), minutes(m), seconds(s), frames(f), drop_frame(df) {}

    /**
     * @brief Computes Timecode from exact Rational timeline time and sequence frame rate.
     */
    static Timecode from_rational(Rational time, Rational frame_rate);

    /**
     * @brief Parses an SMPTE timecode string ("HH:MM:SS:FF" or "HH:MM:SS;FF").
     */
    static Timecode from_string(std::string_view str);

    /**
     * @brief Converts timecode back to exact Rational timeline time.
     */
    [[nodiscard]] Rational to_rational(Rational frame_rate) const;

    /**
     * @brief Formats as standard SMPTE string ("HH:MM:SS:FF" or "HH:MM:SS;FF" for drop frame).
     */
    [[nodiscard]] std::string to_string() const;

    /**
     * @brief Total frame count at given frame rate.
     */
    [[nodiscard]] int64_t total_frames(Rational frame_rate = Rational{24, 1}) const noexcept;
};

} // namespace ave
