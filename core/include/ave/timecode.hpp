#pragma once

#include "ave/time.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ave {

/**
 * @brief SMPTE Non-Drop-Frame (NDF) Timecode representation ("HH:MM:SS:FF").
 *
 * Computes exact HH:MM:SS:FF representation from Rational time and sequence frame rate
 * using checked 128-bit integer arithmetic. Drop-frame (";") notation is rejected in Phase 10.
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
     * @brief Returns true when this Timecode represents a valid Non-Drop-Frame value at frame_rate.
     */
    [[nodiscard]] bool is_valid(Rational frame_rate = Rational{24, 1}) const noexcept;

    /**
     * @brief Computes Non-Drop-Frame Timecode from exact Rational timeline time and sequence frame rate.
     */
    static Timecode from_rational(Rational time, Rational frame_rate);

    /**
     * @brief Non-throwing parser for strict SMPTE Non-Drop-Frame strings ("HH:MM:SS:FF").
     * Rejects non-11-character inputs, non-digit fields, out-of-range MM/SS, and drop-frame ";".
     */
    [[nodiscard]] static std::optional<Timecode> try_from_string(std::string_view str) noexcept;

    /**
     * @brief Parses a strict SMPTE Non-Drop-Frame timecode string ("HH:MM:SS:FF").
     * Throws std::invalid_argument on malformed input, out-of-range fields, or drop-frame ";".
     */
    static Timecode from_string(std::string_view str);

    /**
     * @brief Converts timecode back to exact Rational timeline time.
     * Throws std::invalid_argument on invalid fields, drop-frame flag, or non-positive frame_rate;
     * throws std::overflow_error on int64_t rational numerator overflow.
     */
    [[nodiscard]] Rational to_rational(Rational frame_rate) const;

    /**
     * @brief Formats as SMPTE Non-Drop-Frame string ("HH:MM:SS:FF").
     * Throws std::invalid_argument on drop-frame flag or negative/out-of-range fields.
     */
    [[nodiscard]] std::string to_string() const;

    /**
     * @brief Total frame count at given frame rate, or -1 if invalid or int64_t overflow occurs.
     */
    [[nodiscard]] int64_t total_frames(Rational frame_rate = Rational{24, 1}) const noexcept;
};

} // namespace ave
