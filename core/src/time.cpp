#include <ave/time.hpp>

#include <cmath>

namespace ave {

TimeConversionResult ui_seconds_to_rational_pts_safe(double seconds, Rational frame_rate) {
    constexpr uint64_t kMaxTimelineSeconds = 864000ULL;           // 240 hours
    constexpr uint64_t kMaxExactDoubleInt = 9007199254740992ULL;  // 2^53
    constexpr double kMaxTimelineSecondsDbl = 864000.0;
    constexpr double kI64LimitDbl = 9223372036854775808.0;        // 0x1p63

    const int64_t fps_num = frame_rate.numerator();
    const int64_t fps_den = frame_rate.denominator();

    if (fps_num <= 0 || fps_den <= 0) {
        return {TimeConversionStatus::InvalidFrameRate, Rational{0, 1}};
    }

    const detail::u128 num_frames_u128 =
        detail::mul_u64_to_u128(kMaxTimelineSeconds, static_cast<uint64_t>(fps_num));
    const detail::u128 max_frame_u128 =
        detail::divmod_u128_u64(num_frames_u128, static_cast<uint64_t>(fps_den));

    if (detail::u128_is_zero(max_frame_u128)) {
        return {TimeConversionStatus::InvalidFrameRate, Rational{0, 1}};
    }

    if (max_frame_u128.hi != 0 || max_frame_u128.lo > kMaxExactDoubleInt) {
        return {TimeConversionStatus::Overflow, Rational{0, 1}};
    }

    const detail::u128 max_pts_num_u128 =
        detail::mul_u64_to_u128(max_frame_u128.lo, static_cast<uint64_t>(fps_den));
    if (max_pts_num_u128.hi != 0 ||
        max_pts_num_u128.lo > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return {TimeConversionStatus::Overflow, Rational{0, 1}};
    }

    const int64_t max_frame_index = static_cast<int64_t>(max_frame_u128.lo);
    const Rational max_safe_pts(static_cast<int64_t>(max_pts_num_u128.lo), fps_num);

    if (std::isnan(seconds)) {
        return {TimeConversionStatus::InvalidNaN, Rational{0, 1}};
    }

    if (std::isinf(seconds)) {
        if (seconds < 0.0) {
            return {TimeConversionStatus::ClampedNegativeInfinity, Rational{0, 1}};
        }
        return {TimeConversionStatus::ClampedPositiveInfinity, max_safe_pts};
    }

    if (seconds < 0.0) {
        return {TimeConversionStatus::ClampedNegative, Rational{0, 1}};
    }

    if (seconds > kMaxTimelineSecondsDbl) {
        return {TimeConversionStatus::ClampedAboveMax, max_safe_pts};
    }

    const double raw_frames =
        (seconds * static_cast<double>(fps_num)) / static_cast<double>(fps_den);
    if (!std::isfinite(raw_frames) || raw_frames < 0.0) {
        return {TimeConversionStatus::Overflow, Rational{0, 1}};
    }

    const double fl = std::floor(raw_frames);
    const double rounded = fl + ((raw_frames - fl) >= 0.5 ? 1.0 : 0.0);

    if (!std::isfinite(rounded) || rounded < 0.0 || rounded >= kI64LimitDbl) {
        return {TimeConversionStatus::Overflow, Rational{0, 1}};
    }

    if (rounded > static_cast<double>(max_frame_index)) {
        return {TimeConversionStatus::ClampedAboveMax, max_safe_pts};
    }

    const int64_t frame_index = static_cast<int64_t>(rounded);
    if (frame_index == 0) {
        return {TimeConversionStatus::Zero, Rational{0, 1}};
    }

    const detail::u128 pts_num_u128 =
        detail::mul_u64_to_u128(static_cast<uint64_t>(frame_index), static_cast<uint64_t>(fps_den));
    if (pts_num_u128.hi != 0 ||
        pts_num_u128.lo > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return {TimeConversionStatus::Overflow, Rational{0, 1}};
    }

    return {TimeConversionStatus::Ok, Rational(static_cast<int64_t>(pts_num_u128.lo), fps_num)};
}

} // namespace ave
