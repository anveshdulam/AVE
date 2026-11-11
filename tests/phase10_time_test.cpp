#include <ave/time.hpp>
#include <ave/timecode.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

// Compile-time verification proving portable constexpr 128-bit fallback works identically
constexpr ave::Rational kConstExprSum = ave::Rational{1, 6} + ave::Rational{1, 10};
static_assert(kConstExprSum.numerator() == 4 && kConstExprSum.denominator() == 15);

constexpr ave::Rational kConstExprInt64MinDiff =
    ave::Rational{-1, 1} - ave::Rational{std::numeric_limits<int64_t>::min(), 1};
static_assert(kConstExprInt64MinDiff.numerator() == std::numeric_limits<int64_t>::max() &&
              kConstExprInt64MinDiff.denominator() == 1);

constexpr ave::Rational kConstExprNeg2Shift =
    ave::Rational{std::numeric_limits<int64_t>::min(), -2};
static_assert(kConstExprNeg2Shift.numerator() == 4611686018427387904LL &&
              kConstExprNeg2Shift.denominator() == 1);

constexpr int64_t kConstExprFrames = ave::Rational{1001, 30000}.to_frames(ave::Rational{30000, 1001});
static_assert(kConstExprFrames == 1);

constexpr ave::detail::u128 kConstProd = ave::detail::mul_u64_to_u128(0xFFFFFFFFULL, 0xFFFFFFFFULL);
static_assert(kConstProd.lo == 0xFFFFFFFE00000001ULL && kConstProd.hi == 0ULL);

constexpr ave::detail::u128 kConstLargeProd =
    ave::detail::mul_u64_to_u128(1000000000000ULL, 1000000000000ULL);
constexpr ave::detail::u128_divmod kConstLargeDiv =
    ave::detail::divmod_u128_u128(kConstLargeProd, ave::detail::u128{7ULL, 0ULL});
static_assert(kConstLargeDiv.quotient.hi == 7744ULL &&
              kConstLargeDiv.quotient.lo == 5556750336089428553ULL &&
              kConstLargeDiv.remainder.lo == 1ULL);

} // namespace

TEST(Phase10TimeTest, RationalInitializationZeroDenominatorAndSignNormalization) {
    EXPECT_THROW((void)ave::Rational(1, 0), std::invalid_argument);
    EXPECT_THROW((void)ave::Rational(0, 0), std::invalid_argument);

    const ave::Rational zero_neg_den(0, -42);
    EXPECT_EQ(zero_neg_den.numerator(), 0);
    EXPECT_EQ(zero_neg_den.denominator(), 1);

    const ave::Rational neg_den(3, -6);
    EXPECT_EQ(neg_den.numerator(), -1);
    EXPECT_EQ(neg_den.denominator(), 2);

    const ave::Rational dbl_neg(-6, -12);
    EXPECT_EQ(dbl_neg.numerator(), 1);
    EXPECT_EQ(dbl_neg.denominator(), 2);

    const ave::Rational ntsc(60000, 2002);
    EXPECT_EQ(ntsc.numerator(), 30000);
    EXPECT_EQ(ntsc.denominator(), 1001);
}

TEST(Phase10TimeTest, RationalInt64MinSafetyAndSimplification) {
    constexpr int64_t kMin = std::numeric_limits<int64_t>::min();
    constexpr int64_t kMax = std::numeric_limits<int64_t>::max();

    const ave::Rational min_unit(kMin, 1);
    EXPECT_EQ(min_unit.numerator(), kMin);
    EXPECT_EQ(min_unit.denominator(), 1);

    const ave::Rational min_div_neg2(kMin, -2);
    EXPECT_EQ(min_div_neg2.numerator(), 4611686018427387904LL);
    EXPECT_EQ(min_div_neg2.denominator(), 1);

    const ave::Rational min_over_min(kMin, kMin);
    EXPECT_EQ(min_over_min, ave::Rational(1, 1));

    const ave::Rational two_over_min(2, kMin);
    EXPECT_EQ(two_over_min.numerator(), -1);
    EXPECT_EQ(two_over_min.denominator(), 4611686018427387904LL);

    EXPECT_THROW((void)ave::Rational(kMin, -1), std::overflow_error);
    EXPECT_THROW((void)ave::Rational(1, kMin), std::overflow_error);

    // Subtraction with INT64_MIN must not negate INT64_MIN with UB
    EXPECT_EQ(ave::Rational(-1, 1) - min_unit, ave::Rational(kMax, 1));
    EXPECT_EQ(min_unit + ave::Rational(1, 1), ave::Rational(kMin + 1, 1));
    EXPECT_THROW((void)(ave::Rational(0, 1) - min_unit), std::overflow_error);
    EXPECT_THROW((void)(min_unit * ave::Rational(-1, 1)), std::overflow_error);
    EXPECT_THROW((void)(min_unit / ave::Rational(-1, 1)), std::overflow_error);
    EXPECT_EQ(min_unit / -2, ave::Rational(4611686018427387904LL, 1));
}

TEST(Phase10TimeTest, RationalCheckedOperatorsAndComparisons) {
    constexpr int64_t kMin = std::numeric_limits<int64_t>::min();
    constexpr int64_t kMax = std::numeric_limits<int64_t>::max();

    // Large cross-product comparisons requiring 128-bit arithmetic
    const ave::Rational big_a(kMax - 1, kMax);
    const ave::Rational big_b(kMax - 2, kMax - 1);
    EXPECT_TRUE(big_b < big_a);
    EXPECT_FALSE(big_a < big_b);

    const ave::Rational neg_a(-(kMax - 1), kMax);
    const ave::Rational neg_b(-(kMax - 2), kMax - 1);
    EXPECT_TRUE(neg_a < neg_b);

    // Addition / subtraction overflow detection and valid cancellation
    EXPECT_EQ(ave::Rational(1, 6) + ave::Rational(1, 10), ave::Rational(4, 15));
    EXPECT_THROW((void)(ave::Rational(kMax, 1) + ave::Rational(1, 1)), std::overflow_error);
    EXPECT_THROW((void)(ave::Rational(kMin, 1) - ave::Rational(1, 1)), std::overflow_error);
    EXPECT_THROW((void)(ave::Rational(1, kMax) + ave::Rational(1, kMax - 1)), std::overflow_error);

    // Cross-reduced multiplication avoiding spurious overflow, plus real overflow detection
    EXPECT_EQ(ave::Rational(kMax, 6) * ave::Rational(3, 1), ave::Rational(kMax, 2));
    EXPECT_EQ(ave::Rational(3, 1) * ave::Rational(kMax, 6), ave::Rational(kMax, 2));
    EXPECT_EQ(2 * ave::Rational(3, 4), ave::Rational(3, 2));
    EXPECT_EQ(ave::Rational(3, 4) * 2, ave::Rational(3, 2));

    EXPECT_THROW((void)(ave::Rational(kMax, 1) * ave::Rational(2, 1)), std::overflow_error);
    EXPECT_THROW((void)(ave::Rational(kMax, 1) * 2), std::overflow_error);
    EXPECT_THROW((void)(2 * ave::Rational(kMax, 1)), std::overflow_error);

    EXPECT_THROW((void)(ave::Rational(1, 1) / ave::Rational(0, 1)), std::invalid_argument);
    EXPECT_THROW((void)(ave::Rational(1, 1) / 0), std::invalid_argument);
    EXPECT_THROW((void)(ave::Rational(1, kMax) / 2), std::overflow_error);
}

TEST(Phase10TimeTest, RationalToFramesChecked) {
    EXPECT_THROW((void)ave::Rational(1, 1).to_frames(ave::Rational(0, 1)), std::invalid_argument);
    EXPECT_THROW((void)ave::Rational(1, 1).to_frames(ave::Rational(-24, 1)), std::invalid_argument);

    EXPECT_EQ(ave::Rational(1001, 30000).to_frames(ave::Rational(30000, 1001)), 1);
    EXPECT_EQ(ave::Rational(-25, 24).to_frames(ave::Rational(24, 1)), -25);
    EXPECT_EQ(ave::Rational(std::numeric_limits<int64_t>::min(), 24).to_frames(ave::Rational(24, 1)),
              std::numeric_limits<int64_t>::min());

    // Large cross-reduced rational to_frames without intermediate 64-bit overflow
    EXPECT_EQ(ave::Rational(1000000000000LL, 30000).to_frames(ave::Rational(30000, 1001)),
              999000999LL);

    EXPECT_THROW(
        (void)ave::Rational(std::numeric_limits<int64_t>::max(), 1).to_frames(ave::Rational(2, 1)),
        std::overflow_error);
}

TEST(Phase10TimeTest, Checked128BitRuntimeParityWithConstexpr) {
    const uint64_t a = 1000000000000ULL;
    const uint64_t b = 1000000000000ULL;
    const ave::detail::u128 prod = ave::detail::mul_u64_to_u128(a, b);
    EXPECT_EQ(prod.lo, kConstLargeProd.lo);
    EXPECT_EQ(prod.hi, kConstLargeProd.hi);

    // Exercise dividend.hi >= divisor (two-step _udiv128 path on MSVC x64)
    uint64_t rem64 = 0;
    const ave::detail::u128 q_two_step = ave::detail::divmod_u128_u64(prod, 7ULL, &rem64);
    EXPECT_EQ(q_two_step.hi, kConstLargeDiv.quotient.hi);
    EXPECT_EQ(q_two_step.lo, kConstLargeDiv.quotient.lo);
    EXPECT_EQ(rem64, 1ULL);

    // Exercise dividend.hi < divisor (single _udiv128 path on MSVC x64)
    const ave::detail::u128 small_hi_dividend = ave::detail::mul_u64_to_u128(1ULL << 40, 25894105ULL);
    uint64_t rem_single = 0;
    const ave::detail::u128 q_single =
        ave::detail::divmod_u128_u64(small_hi_dividend, (1ULL << 40), &rem_single);
    EXPECT_EQ(q_single.hi, 0ULL);
    EXPECT_EQ(q_single.lo, 25894105ULL);
    EXPECT_EQ(rem_single, 0ULL);

    // Exercise 128-bit divisor (divisor.hi != 0)
    const auto qr_128 = ave::detail::divmod_u128_u128(prod, ave::detail::u128{0ULL, 100ULL});
    EXPECT_GT(qr_128.quotient.lo, 0ULL);
    EXPECT_EQ(qr_128.quotient.hi, 0ULL);
}

TEST(Phase10TimeTest, SafePtsZeroAndNegativeZero) {
    const ave::Rational fps{24, 1};

    const auto pos_zero = ave::ui_seconds_to_rational_pts_safe(0.0, fps);
    EXPECT_EQ(pos_zero.status, ave::TimeConversionStatus::Zero);
    EXPECT_EQ(pos_zero.pts, ave::Rational(0, 1));
    EXPECT_TRUE(pos_zero.is_valid());

    const auto neg_zero = ave::ui_seconds_to_rational_pts_safe(-0.0, fps);
    EXPECT_EQ(neg_zero.status, ave::TimeConversionStatus::Zero);
    EXPECT_EQ(neg_zero.pts, ave::Rational(0, 1));
    EXPECT_TRUE(neg_zero.is_valid());

    const auto tiny = ave::ui_seconds_to_rational_pts_safe(1e-6, fps);
    EXPECT_EQ(tiny.status, ave::TimeConversionStatus::Zero);
    EXPECT_EQ(tiny.pts, ave::Rational(0, 1));
    EXPECT_TRUE(tiny.is_valid());
}

TEST(Phase10TimeTest, SafePts24FpsHalfFrameBoundaries) {
    const ave::Rational fps{24, 1};
    const double half_frame_sec = 1.0 / 48.0;

    const auto below = ave::ui_seconds_to_rational_pts_safe(half_frame_sec - 1e-9, fps);
    EXPECT_EQ(below.status, ave::TimeConversionStatus::Zero);
    EXPECT_EQ(below.pts, ave::Rational(0, 1));
    EXPECT_TRUE(below.is_valid());

    const auto exact = ave::ui_seconds_to_rational_pts_safe(half_frame_sec, fps);
    EXPECT_EQ(exact.status, ave::TimeConversionStatus::Ok);
    EXPECT_EQ(exact.pts, ave::Rational(1, 24));
    EXPECT_TRUE(exact.is_valid());

    const auto above = ave::ui_seconds_to_rational_pts_safe(half_frame_sec + 1e-9, fps);
    EXPECT_EQ(above.status, ave::TimeConversionStatus::Ok);
    EXPECT_EQ(above.pts, ave::Rational(1, 24));
    EXPECT_TRUE(above.is_valid());

    const auto one_sec = ave::ui_seconds_to_rational_pts_safe(1.0, fps);
    EXPECT_EQ(one_sec.status, ave::TimeConversionStatus::Ok);
    EXPECT_EQ(one_sec.pts, ave::Rational(1, 1));
}

TEST(Phase10TimeTest, SafePtsInvalidNaNInfinitiesNegativesAndAboveMax) {
    const ave::Rational fps{24, 1};
    const ave::Rational expected_max{864000, 1};

    const auto qnan = ave::ui_seconds_to_rational_pts_safe(
        std::numeric_limits<double>::quiet_NaN(), fps);
    EXPECT_EQ(qnan.status, ave::TimeConversionStatus::InvalidNaN);
    EXPECT_EQ(qnan.pts, ave::Rational(0, 1));
    EXPECT_FALSE(qnan.is_valid());

    const auto snan = ave::ui_seconds_to_rational_pts_safe(
        std::numeric_limits<double>::signaling_NaN(), fps);
    EXPECT_EQ(snan.status, ave::TimeConversionStatus::InvalidNaN);
    EXPECT_EQ(snan.pts, ave::Rational(0, 1));
    EXPECT_FALSE(snan.is_valid());

    const auto neg = ave::ui_seconds_to_rational_pts_safe(-0.001, fps);
    EXPECT_EQ(neg.status, ave::TimeConversionStatus::ClampedNegative);
    EXPECT_EQ(neg.pts, ave::Rational(0, 1));
    EXPECT_FALSE(neg.is_valid());

    const auto neg_inf = ave::ui_seconds_to_rational_pts_safe(
        -std::numeric_limits<double>::infinity(), fps);
    EXPECT_EQ(neg_inf.status, ave::TimeConversionStatus::ClampedNegativeInfinity);
    EXPECT_EQ(neg_inf.pts, ave::Rational(0, 1));
    EXPECT_FALSE(neg_inf.is_valid());

    const auto pos_inf = ave::ui_seconds_to_rational_pts_safe(
        std::numeric_limits<double>::infinity(), fps);
    EXPECT_EQ(pos_inf.status, ave::TimeConversionStatus::ClampedPositiveInfinity);
    EXPECT_EQ(pos_inf.pts, expected_max);
    EXPECT_FALSE(pos_inf.is_valid());

    const auto above_max = ave::ui_seconds_to_rational_pts_safe(864000.1, fps);
    EXPECT_EQ(above_max.status, ave::TimeConversionStatus::ClampedAboveMax);
    EXPECT_EQ(above_max.pts, expected_max);
    EXPECT_FALSE(above_max.is_valid());

    const auto huge_finite = ave::ui_seconds_to_rational_pts_safe(1e20, fps);
    EXPECT_EQ(huge_finite.status, ave::TimeConversionStatus::ClampedAboveMax);
    EXPECT_EQ(huge_finite.pts, expected_max);
}

TEST(Phase10TimeTest, SafePtsNtsc30000Over1001Ceiling240Hours) {
    const ave::Rational ntsc_fps{30000, 1001};
    // MAX_FRAME_INDEX = floor(864000 * 30000 / 1001) = 25,894,105
    // MAX_SAFE_PTS = (25894105 * 1001) / 30000 = 5183999821 / 6000
    const ave::Rational expected_max_safe_pts{5183999821LL, 6000LL};

    // 864000.0 s at 30000/1001 computes raw_frames ~ 25894105.89 -> rounds to 25894106 > MAX_FRAME_INDEX
    const auto at_ceiling_seconds = ave::ui_seconds_to_rational_pts_safe(864000.0, ntsc_fps);
    EXPECT_EQ(at_ceiling_seconds.status, ave::TimeConversionStatus::ClampedAboveMax);
    EXPECT_EQ(at_ceiling_seconds.pts, expected_max_safe_pts);

    const auto at_pos_inf = ave::ui_seconds_to_rational_pts_safe(
        std::numeric_limits<double>::infinity(), ntsc_fps);
    EXPECT_EQ(at_pos_inf.status, ave::TimeConversionStatus::ClampedPositiveInfinity);
    EXPECT_EQ(at_pos_inf.pts, expected_max_safe_pts);

    const double exact_max_frame_secs = (25894105.0 * 1001.0) / 30000.0;
    const auto exact_max = ave::ui_seconds_to_rational_pts_safe(exact_max_frame_secs, ntsc_fps);
    EXPECT_EQ(exact_max.status, ave::TimeConversionStatus::Ok);
    EXPECT_EQ(exact_max.pts, expected_max_safe_pts);
    EXPECT_TRUE(exact_max.is_valid());

    const auto one_frame = ave::ui_seconds_to_rational_pts_safe(1001.0 / 30000.0, ntsc_fps);
    EXPECT_EQ(one_frame.status, ave::TimeConversionStatus::Ok);
    EXPECT_EQ(one_frame.pts, ave::Rational(1001, 30000));
}

TEST(Phase10TimeTest, SafePtsInvalidAndExtremeOverflowFrameRates) {
    EXPECT_EQ(ave::ui_seconds_to_rational_pts_safe(1.0, ave::Rational(0, 1)).status,
              ave::TimeConversionStatus::InvalidFrameRate);
    EXPECT_EQ(ave::ui_seconds_to_rational_pts_safe(1.0, ave::Rational(-24, 1)).status,
              ave::TimeConversionStatus::InvalidFrameRate);

    // Sub-1-frame-per-240h (MAX_FRAME_INDEX == 0)
    EXPECT_EQ(ave::ui_seconds_to_rational_pts_safe(1.0, ave::Rational(1, 1000000)).status,
              ave::TimeConversionStatus::InvalidFrameRate);

    // Extreme high rate where MAX_FRAME_INDEX > 2^53 (864000 * 30e9 = 2.592e16 > 9.007e15)
    EXPECT_EQ(ave::ui_seconds_to_rational_pts_safe(1.0, ave::Rational(30000000000LL, 1)).status,
              ave::TimeConversionStatus::Overflow);

    // Extreme denominator where MAX_FRAME_INDEX * fps_den > INT64_MAX
    constexpr int64_t kHugeDen = std::numeric_limits<int64_t>::max() / 24;
    EXPECT_EQ(ave::ui_seconds_to_rational_pts_safe(1.0, ave::Rational(std::numeric_limits<int64_t>::max(), kHugeDen)).status,
              ave::TimeConversionStatus::Overflow);
}

TEST(Phase10TimeTest, TimecodeNonDropFrameExactRoundTrips) {
    struct Case {
        ave::Rational fps;
        std::string text;
        int64_t expected_frames;
    };

    const Case cases[] = {
        {ave::Rational{24, 1}, "00:00:00:00", 0},
        {ave::Rational{24, 1}, "00:00:00:12", 12},
        {ave::Rational{24, 1}, "01:02:03:04", (1LL * 3600 + 2 * 60 + 3) * 24 + 4},
        {ave::Rational{25, 1}, "00:10:00:24", (10LL * 60) * 25 + 24},
        {ave::Rational{30, 1}, "00:00:59:29", 59LL * 30 + 29},
        {ave::Rational{30000, 1001}, "01:02:03:29", (1LL * 3600 + 2 * 60 + 3) * 30 + 29},
        {ave::Rational{30000, 1001}, "23:59:59:29", (23LL * 3600 + 59 * 60 + 59) * 30 + 29},
        {ave::Rational{24000, 1001}, "02:00:00:23", (2LL * 3600) * 24 + 23},
        {ave::Rational{60000, 1001}, "00:01:00:59", 60LL * 60 + 59},
    };

    for (const auto& c : cases) {
        const ave::Timecode tc = ave::Timecode::from_string(c.text);
        EXPECT_FALSE(tc.drop_frame);
        EXPECT_TRUE(tc.is_valid(c.fps));
        EXPECT_EQ(tc.total_frames(c.fps), c.expected_frames);

        const ave::Rational pts = tc.to_rational(c.fps);
        const ave::Timecode back = ave::Timecode::from_rational(pts, c.fps);
        EXPECT_FALSE(back.drop_frame);
        EXPECT_EQ(back.to_string(), c.text);
    }

    // Negative time clamps to 00:00:00:00; invalid frame_rate throws
    const ave::Timecode neg = ave::Timecode::from_rational(ave::Rational(-5, 1), ave::Rational(24, 1));
    EXPECT_EQ(neg.to_string(), "00:00:00:00");
    EXPECT_FALSE(neg.drop_frame);
    EXPECT_THROW((void)ave::Timecode::from_rational(ave::Rational(1, 1), ave::Rational(0, 1)),
                 std::invalid_argument);
}

TEST(Phase10TimeTest, TimecodeStrictParsingAndNonNullTerminatedSlice) {
    // Non-null-terminated buffer with trailing numeric digits directly after index 10
    const char raw_buf[15] = {'0', '1', ':', '0', '2', ':', '0', '3', ':', '0', '4', '9', '9', '9', '9'};
    const std::string_view slice(raw_buf, 11);

    const auto opt = ave::Timecode::try_from_string(slice);
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->hours, 1);
    EXPECT_EQ(opt->minutes, 2);
    EXPECT_EQ(opt->seconds, 3);
    EXPECT_EQ(opt->frames, 4);
    EXPECT_FALSE(opt->drop_frame);
    EXPECT_EQ(ave::Timecode::from_string(slice).to_string(), "01:02:03:04");

    // Malformed strings must all be rejected cleanly
    const char* bad_inputs[] = {
        "",
        "00:00:00:1",
        "00:00:00:123",
        "00:00:00:12extra",
        " 0:00:00:00",
        "-1:00:00:00",
        "+1:00:00:00",
        "00:00:00:ab",
        "00-00-00-00",
        "00:60:00:00",
        "00:00:60:00"
    };
    for (const char* bad : bad_inputs) {
        EXPECT_FALSE(ave::Timecode::try_from_string(bad).has_value()) << "Input: " << bad;
        EXPECT_THROW((void)ave::Timecode::from_string(bad), std::invalid_argument) << "Input: " << bad;
    }
}

TEST(Phase10TimeTest, TimecodeDropFrameRejectedConsistently) {
    EXPECT_FALSE(ave::Timecode::try_from_string("01:02:03;04").has_value());
    EXPECT_THROW((void)ave::Timecode::from_string("01:02:03;04"), std::invalid_argument);

    const ave::Timecode df_tc(1, 2, 3, 4, true);
    const ave::Rational ntsc{30000, 1001};
    EXPECT_FALSE(df_tc.is_valid(ntsc));
    EXPECT_EQ(df_tc.total_frames(ntsc), -1);
    EXPECT_THROW((void)df_tc.to_rational(ntsc), std::invalid_argument);
    EXPECT_THROW((void)df_tc.to_string(), std::invalid_argument);
}

TEST(Phase10TimeTest, TimecodeFieldValidationAndOverflow) {
    const ave::Rational fps24{24, 1};
    const ave::Rational ntsc{30000, 1001};

    // Frame limit checks: valid < ceil(fps), invalid >= ceil(fps)
    const ave::Timecode f24_bad(0, 0, 0, 24, false);
    EXPECT_FALSE(f24_bad.is_valid(fps24));
    EXPECT_EQ(f24_bad.total_frames(fps24), -1);
    EXPECT_THROW((void)f24_bad.to_rational(fps24), std::invalid_argument);

    const ave::Timecode f29_ntsc(0, 0, 0, 29, false);
    EXPECT_TRUE(f29_ntsc.is_valid(ntsc));
    EXPECT_EQ(f29_ntsc.total_frames(ntsc), 29);

    const ave::Timecode f30_ntsc_bad(0, 0, 0, 30, false);
    EXPECT_FALSE(f30_ntsc_bad.is_valid(ntsc));
    EXPECT_EQ(f30_ntsc_bad.total_frames(ntsc), -1);
    EXPECT_THROW((void)f30_ntsc_bad.to_rational(ntsc), std::invalid_argument);

    // Negative / out-of-range fields
    EXPECT_FALSE(ave::Timecode(-1, 0, 0, 0).is_valid(fps24));
    EXPECT_FALSE(ave::Timecode(0, 60, 0, 0).is_valid(fps24));
    EXPECT_FALSE(ave::Timecode(0, 0, 60, 0).is_valid(fps24));
    EXPECT_FALSE(ave::Timecode(0, 0, 0, -1).is_valid(fps24));
    EXPECT_THROW((void)ave::Timecode(-1, 0, 0, 0).to_string(), std::invalid_argument);

    // Overflow in 128-bit total frame count / rational numerator
    const ave::Timecode huge_tc(2000000000, 0, 0, 0, false);
    const ave::Rational high_fps{1000000000, 1};
    EXPECT_EQ(huge_tc.total_frames(high_fps), -1);
    EXPECT_THROW((void)huge_tc.to_rational(high_fps), std::overflow_error);

    // total_frames fits in int64_t, but total_frames * fps_den overflows int64_t inside to_rational()
    const ave::Rational ultra_slow_fps{1, 2000000};
    EXPECT_EQ(huge_tc.total_frames(ultra_slow_fps), 7200000000000LL);
    EXPECT_THROW((void)huge_tc.to_rational(ultra_slow_fps), std::overflow_error);
}
