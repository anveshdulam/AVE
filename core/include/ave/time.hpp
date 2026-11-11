#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

#if defined(_MSC_VER) && !defined(__clang__) && defined(_M_X64)
#include <intrin.h>
#endif

namespace ave {

namespace detail {

struct u128 {
    uint64_t lo{0};
    uint64_t hi{0};
};

[[nodiscard]] constexpr uint64_t abs_to_u64(int64_t value) noexcept {
    if (value >= 0) {
        return static_cast<uint64_t>(value);
    }
    return 0ULL - static_cast<uint64_t>(value);
}

[[nodiscard]] constexpr uint64_t gcd_u64(uint64_t a, uint64_t b) noexcept {
    while (b != 0) {
        const uint64_t r = a % b;
        a = b;
        b = r;
    }
    return a;
}

[[nodiscard]] constexpr bool u128_is_zero(u128 v) noexcept {
    return v.lo == 0 && v.hi == 0;
}

[[nodiscard]] constexpr int cmp_u128(u128 a, u128 b) noexcept {
    if (a.hi != b.hi) {
        return (a.hi < b.hi) ? -1 : 1;
    }
    if (a.lo != b.lo) {
        return (a.lo < b.lo) ? -1 : 1;
    }
    return 0;
}

[[nodiscard]] constexpr u128 add_u128(u128 a, u128 b) noexcept {
    const uint64_t lo = a.lo + b.lo;
    const uint64_t carry = (lo < a.lo) ? 1ULL : 0ULL;
    const uint64_t hi = a.hi + b.hi + carry;
    return u128{lo, hi};
}

[[nodiscard]] constexpr u128 sub_u128(u128 a, u128 b) noexcept {
    const uint64_t borrow = (a.lo < b.lo) ? 1ULL : 0ULL;
    const uint64_t lo = a.lo - b.lo;
    const uint64_t hi = a.hi - b.hi - borrow;
    return u128{lo, hi};
}

[[nodiscard]] constexpr u128 shl1_u128(u128 v, uint64_t low_bit) noexcept {
    return u128{(v.lo << 1) | (low_bit & 1ULL), (v.hi << 1) | (v.lo >> 63)};
}

[[nodiscard]] constexpr uint64_t bit_at_u128(u128 v, int bit_index) noexcept {
    if (bit_index >= 64) {
        return (v.hi >> (bit_index - 64)) & 1ULL;
    }
    return (v.lo >> bit_index) & 1ULL;
}

[[nodiscard]] constexpr u128 mul_u64_to_u128(uint64_t a, uint64_t b) noexcept {
#if defined(_MSC_VER) && !defined(__clang__) && defined(_M_X64)
    if (!std::is_constant_evaluated()) {
        unsigned __int64 hi = 0;
        const unsigned __int64 lo = _umul128(a, b, &hi);
        return u128{static_cast<uint64_t>(lo), static_cast<uint64_t>(hi)};
    }
#elif defined(__SIZEOF_INT128__)
    if (!std::is_constant_evaluated()) {
        const __uint128_t p = static_cast<__uint128_t>(a) * static_cast<__uint128_t>(b);
        return u128{static_cast<uint64_t>(p), static_cast<uint64_t>(p >> 64)};
    }
#endif
    const uint64_t a_lo = static_cast<uint32_t>(a);
    const uint64_t a_hi = a >> 32;
    const uint64_t b_lo = static_cast<uint32_t>(b);
    const uint64_t b_hi = b >> 32;

    const uint64_t p00 = a_lo * b_lo;
    const uint64_t p01 = a_lo * b_hi;
    const uint64_t p10 = a_hi * b_lo;
    const uint64_t p11 = a_hi * b_hi;

    const uint64_t mid = (p00 >> 32) + static_cast<uint32_t>(p01) + static_cast<uint32_t>(p10);
    const uint64_t lo = (p00 & 0xFFFFFFFFULL) | (mid << 32);
    const uint64_t hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    return u128{lo, hi};
}

[[nodiscard]] constexpr bool checked_mul_u128(u128 a, u128 b, u128& out) noexcept {
    if (a.hi != 0 && b.hi != 0) {
        return false;
    }
    const u128 p_lo = mul_u64_to_u128(a.lo, b.lo);
    if (a.hi == 0 && b.hi == 0) {
        out = p_lo;
        return true;
    }
    const uint64_t hi_factor = (a.hi != 0) ? a.hi : b.hi;
    const uint64_t lo_factor = (a.hi != 0) ? b.lo : a.lo;
    const u128 cross = mul_u64_to_u128(hi_factor, lo_factor);
    if (cross.hi != 0) {
        return false;
    }
    const uint64_t hi_sum = p_lo.hi + cross.lo;
    if (hi_sum < p_lo.hi) {
        return false;
    }
    out = u128{p_lo.lo, hi_sum};
    return true;
}

struct u128_divmod {
    u128 quotient{};
    u128 remainder{};
};

[[nodiscard]] constexpr u128_divmod divmod_u128_u128(u128 dividend, u128 divisor) {
    if (u128_is_zero(divisor)) {
        throw std::invalid_argument("Division by zero 128-bit integer");
    }
    if (cmp_u128(dividend, divisor) < 0) {
        return u128_divmod{u128{0, 0}, dividend};
    }

    if (divisor.hi == 0) {
#if defined(_MSC_VER) && !defined(__clang__) && defined(_M_X64)
        if (!std::is_constant_evaluated()) {
            const uint64_t d = divisor.lo;
            unsigned __int64 rem = 0;
            if (dividend.hi < d) {
                const unsigned __int64 q_lo = _udiv128(dividend.hi, dividend.lo, d, &rem);
                return u128_divmod{u128{static_cast<uint64_t>(q_lo), 0}, u128{static_cast<uint64_t>(rem), 0}};
            }
            const uint64_t q_hi = dividend.hi / d;
            const uint64_t rem_hi = dividend.hi % d;
            const unsigned __int64 q_lo = _udiv128(rem_hi, dividend.lo, d, &rem);
            return u128_divmod{u128{static_cast<uint64_t>(q_lo), q_hi}, u128{static_cast<uint64_t>(rem), 0}};
        }
#elif defined(__SIZEOF_INT128__)
        if (!std::is_constant_evaluated()) {
            const __uint128_t n = (static_cast<__uint128_t>(dividend.hi) << 64) | dividend.lo;
            const __uint128_t q = n / divisor.lo;
            const __uint128_t r = n % divisor.lo;
            return u128_divmod{
                u128{static_cast<uint64_t>(q), static_cast<uint64_t>(q >> 64)},
                u128{static_cast<uint64_t>(r), 0}
            };
        }
#endif
    }

#if defined(__SIZEOF_INT128__)
    if (!std::is_constant_evaluated()) {
        const __uint128_t n = (static_cast<__uint128_t>(dividend.hi) << 64) | dividend.lo;
        const __uint128_t d = (static_cast<__uint128_t>(divisor.hi) << 64) | divisor.lo;
        const __uint128_t q = n / d;
        const __uint128_t r = n % d;
        return u128_divmod{
            u128{static_cast<uint64_t>(q), static_cast<uint64_t>(q >> 64)},
            u128{static_cast<uint64_t>(r), static_cast<uint64_t>(r >> 64)}
        };
    }
#endif

    u128 quotient{};
    u128 remainder{};
    for (int bit = 127; bit >= 0; --bit) {
        remainder = shl1_u128(remainder, bit_at_u128(dividend, bit));
        if (cmp_u128(remainder, divisor) >= 0) {
            remainder = sub_u128(remainder, divisor);
            if (bit >= 64) {
                quotient.hi |= (1ULL << (bit - 64));
            } else {
                quotient.lo |= (1ULL << bit);
            }
        }
    }
    return u128_divmod{quotient, remainder};
}

[[nodiscard]] constexpr u128 divmod_u128_u64(u128 dividend, uint64_t divisor, uint64_t* rem_out = nullptr) {
    const auto res = divmod_u128_u128(dividend, u128{divisor, 0});
    if (rem_out) {
        *rem_out = res.remainder.lo;
    }
    return res.quotient;
}

[[nodiscard]] constexpr int64_t checked_u128_to_signed_i64(bool negative, u128 mag) {
    constexpr uint64_t kMaxPos = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    constexpr uint64_t kMaxNeg = kMaxPos + 1ULL; // |INT64_MIN| = 2^63

    if (mag.hi != 0) {
        throw std::overflow_error("Signed 64-bit integer overflow in rational arithmetic");
    }
    if (!negative) {
        if (mag.lo > kMaxPos) {
            throw std::overflow_error("Signed 64-bit integer overflow in rational arithmetic");
        }
        return static_cast<int64_t>(mag.lo);
    }
    if (mag.lo == 0) {
        return 0;
    }
    if (mag.lo > kMaxNeg) {
        throw std::overflow_error("Signed 64-bit integer underflow in rational arithmetic");
    }
    if (mag.lo == kMaxNeg) {
        return std::numeric_limits<int64_t>::min();
    }
    return -static_cast<int64_t>(mag.lo);
}

[[nodiscard]] constexpr uint64_t checked_u128_to_pos_i64_u64(u128 val) {
    constexpr uint64_t kMaxPos = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    if (val.hi != 0 || val.lo == 0 || val.lo > kMaxPos) {
        throw std::overflow_error("Rational denominator out of positive int64_t range");
    }
    return val.lo;
}

} // namespace detail

/**
 * @brief Canonical rational time representation for editorial timing.
 *
 * Uses exact checked 64-bit numerator and positive canonical denominator,
 * backed by overflow-safe 128-bit arithmetic. Never stores canonical
 * timeline cut positions as floating-point seconds.
 */
class Rational {
public:
    constexpr Rational() noexcept : m_num(0), m_den(1) {}
    constexpr Rational(int64_t num, int64_t den) : m_num(num), m_den(den) {
        if (m_den == 0) {
            throw std::invalid_argument("Rational denominator cannot be zero");
        }
        simplify();
    }

    [[nodiscard]] constexpr int64_t numerator() const noexcept { return m_num; }
    [[nodiscard]] constexpr int64_t denominator() const noexcept { return m_den; }

    [[nodiscard]] double to_seconds() const noexcept {
        return static_cast<double>(m_num) / static_cast<double>(m_den);
    }

    [[nodiscard]] std::string to_string() const {
        return std::to_string(m_num) + "/" + std::to_string(m_den);
    }

    [[nodiscard]] constexpr int64_t to_frames(const Rational& frame_rate) const {
        if (frame_rate.numerator() <= 0 || frame_rate.denominator() <= 0) {
            throw std::invalid_argument("Frame rate must be strictly positive");
        }
        if (m_num == 0) {
            return 0;
        }
        const bool neg = (m_num < 0);
        const uint64_t a = detail::abs_to_u64(m_num);
        const uint64_t b = static_cast<uint64_t>(m_den);
        const uint64_t fn = static_cast<uint64_t>(frame_rate.numerator());
        const uint64_t fd = static_cast<uint64_t>(frame_rate.denominator());

        // Cross-reduce before widening so exact truncated quotient stays identical
        const uint64_t g1 = detail::gcd_u64(a, fd);
        const uint64_t g2 = detail::gcd_u64(b, fn);

        const detail::u128 num_128 = detail::mul_u64_to_u128(a / g1, fn / g2);
        const detail::u128 den_128 = detail::mul_u64_to_u128(b / g2, fd / g1);
        const auto qr = detail::divmod_u128_u128(num_128, den_128);
        return detail::checked_u128_to_signed_i64(neg, qr.quotient);
    }

    constexpr bool operator==(const Rational& other) const noexcept {
        return m_num == other.m_num && m_den == other.m_den;
    }

    constexpr bool operator!=(const Rational& other) const noexcept {
        return !(*this == other);
    }

    constexpr bool operator<(const Rational& other) const noexcept {
        const bool lhs_neg = m_num < 0;
        const bool rhs_neg = other.m_num < 0;
        if (lhs_neg != rhs_neg) {
            return lhs_neg;
        }
        if (m_num == 0 || other.m_num == 0) {
            return m_num < other.m_num;
        }
        const detail::u128 lhs = detail::mul_u64_to_u128(
            detail::abs_to_u64(m_num), static_cast<uint64_t>(other.m_den));
        const detail::u128 rhs = detail::mul_u64_to_u128(
            detail::abs_to_u64(other.m_num), static_cast<uint64_t>(m_den));
        const int cmp = detail::cmp_u128(lhs, rhs);
        return lhs_neg ? (cmp > 0) : (cmp < 0);
    }

    constexpr bool operator<=(const Rational& other) const noexcept {
        return !(other < *this);
    }

    constexpr bool operator>(const Rational& other) const noexcept {
        return other < *this;
    }

    constexpr bool operator>=(const Rational& other) const noexcept {
        return !(*this < other);
    }

    constexpr Rational operator+(const Rational& other) const {
        return add_signed_parts(m_num, m_den, other.m_num < 0, detail::abs_to_u64(other.m_num), other.m_den);
    }

    constexpr Rational operator-(const Rational& other) const {
        return add_signed_parts(m_num, m_den, other.m_num > 0, detail::abs_to_u64(other.m_num), other.m_den);
    }

    constexpr Rational operator*(const Rational& other) const {
        if (m_num == 0 || other.m_num == 0) {
            return Rational{0, 1};
        }
        const bool neg = (m_num < 0) != (other.m_num < 0);
        const uint64_t n1 = detail::abs_to_u64(m_num);
        const uint64_t d1 = static_cast<uint64_t>(m_den);
        const uint64_t n2 = detail::abs_to_u64(other.m_num);
        const uint64_t d2 = static_cast<uint64_t>(other.m_den);

        const uint64_t g1 = detail::gcd_u64(n1, d2);
        const uint64_t g2 = detail::gcd_u64(n2, d1);

        const detail::u128 prod_num = detail::mul_u64_to_u128(n1 / g1, n2 / g2);
        const detail::u128 prod_den = detail::mul_u64_to_u128(d1 / g2, d2 / g1);

        const int64_t num_res = detail::checked_u128_to_signed_i64(neg, prod_num);
        const uint64_t den_res = detail::checked_u128_to_pos_i64_u64(prod_den);
        return from_canonical_parts(num_res, static_cast<int64_t>(den_res));
    }

    constexpr Rational operator*(int64_t scalar) const {
        if (m_num == 0 || scalar == 0) {
            return Rational{0, 1};
        }
        const bool neg = (m_num < 0) != (scalar < 0);
        const uint64_t u_num = detail::abs_to_u64(m_num);
        const uint64_t u_den = static_cast<uint64_t>(m_den);
        const uint64_t u_sca = detail::abs_to_u64(scalar);
        const uint64_t g = detail::gcd_u64(u_sca, u_den);

        const detail::u128 prod_num = detail::mul_u64_to_u128(u_num, u_sca / g);
        const int64_t num_res = detail::checked_u128_to_signed_i64(neg, prod_num);
        return from_canonical_parts(num_res, static_cast<int64_t>(u_den / g));
    }

    constexpr Rational operator/(const Rational& other) const {
        if (other.m_num == 0) {
            throw std::invalid_argument("Division by zero rational");
        }
        if (m_num == 0) {
            return Rational{0, 1};
        }
        const bool neg = (m_num < 0) != (other.m_num < 0);
        const uint64_t n1 = detail::abs_to_u64(m_num);
        const uint64_t d1 = static_cast<uint64_t>(m_den);
        const uint64_t n2 = detail::abs_to_u64(other.m_num);
        const uint64_t d2 = static_cast<uint64_t>(other.m_den);

        const uint64_t g1 = detail::gcd_u64(n1, n2);
        const uint64_t g2 = detail::gcd_u64(d2, d1);

        const detail::u128 prod_num = detail::mul_u64_to_u128(n1 / g1, d2 / g2);
        const detail::u128 prod_den = detail::mul_u64_to_u128(d1 / g2, n2 / g1);

        const int64_t num_res = detail::checked_u128_to_signed_i64(neg, prod_num);
        const uint64_t den_res = detail::checked_u128_to_pos_i64_u64(prod_den);
        return from_canonical_parts(num_res, static_cast<int64_t>(den_res));
    }

    constexpr Rational operator/(int64_t scalar) const {
        if (scalar == 0) {
            throw std::invalid_argument("Division by zero scalar");
        }
        if (m_num == 0) {
            return Rational{0, 1};
        }
        const bool neg = (m_num < 0) != (scalar < 0);
        const uint64_t u_num = detail::abs_to_u64(m_num);
        const uint64_t u_den = static_cast<uint64_t>(m_den);
        const uint64_t u_sca = detail::abs_to_u64(scalar);
        const uint64_t g = detail::gcd_u64(u_num, u_sca);

        const detail::u128 prod_den = detail::mul_u64_to_u128(u_den, u_sca / g);
        const uint64_t den_res = detail::checked_u128_to_pos_i64_u64(prod_den);
        const int64_t num_res = detail::checked_u128_to_signed_i64(neg, detail::u128{u_num / g, 0});
        return from_canonical_parts(num_res, static_cast<int64_t>(den_res));
    }

    friend constexpr Rational operator*(int64_t scalar, const Rational& r) {
        return r * scalar;
    }

private:
    int64_t m_num;
    int64_t m_den;

    struct CanonicalTag {};
    constexpr Rational(CanonicalTag, int64_t num, int64_t den) noexcept
        : m_num(num), m_den(den) {}

    [[nodiscard]] static constexpr Rational from_canonical_parts(int64_t num, int64_t den) noexcept {
        return Rational(CanonicalTag{}, num, den);
    }

    [[nodiscard]] static constexpr Rational add_signed_parts(
        int64_t a_num, int64_t a_den, bool b_neg, uint64_t b_abs_num, int64_t b_den) {
        if (b_abs_num == 0) {
            return from_canonical_parts(a_num, a_den);
        }
        if (a_num == 0) {
            const int64_t res_num = detail::checked_u128_to_signed_i64(b_neg, detail::u128{b_abs_num, 0});
            return from_canonical_parts(res_num, b_den);
        }

        const bool a_neg = a_num < 0;
        const uint64_t a_abs = detail::abs_to_u64(a_num);
        const uint64_t da = static_cast<uint64_t>(a_den);
        const uint64_t db = static_cast<uint64_t>(b_den);

        const uint64_t g = detail::gcd_u64(da, db);
        const uint64_t da_g = da / g;
        const uint64_t db_g = db / g;

        const detail::u128 term_a = detail::mul_u64_to_u128(a_abs, db_g);
        const detail::u128 term_b = detail::mul_u64_to_u128(b_abs_num, da_g);

        bool res_neg = false;
        detail::u128 num_mag{};
        if (a_neg == b_neg) {
            res_neg = a_neg;
            num_mag = detail::add_u128(term_a, term_b);
        } else {
            const int cmp = detail::cmp_u128(term_a, term_b);
            if (cmp == 0) {
                return Rational{0, 1};
            }
            if (cmp > 0) {
                res_neg = a_neg;
                num_mag = detail::sub_u128(term_a, term_b);
            } else {
                res_neg = b_neg;
                num_mag = detail::sub_u128(term_b, term_a);
            }
        }

        const detail::u128 common_den = detail::mul_u64_to_u128(da_g, db);
        uint64_t mod_g = 0;
        (void)detail::divmod_u128_u64(num_mag, g, &mod_g);
        const uint64_t k = detail::gcd_u64(mod_g, g);

        const detail::u128 reduced_num = (k > 1) ? detail::divmod_u128_u64(num_mag, k) : num_mag;
        const detail::u128 reduced_den = (k > 1) ? detail::divmod_u128_u64(common_den, k) : common_den;

        const int64_t num_res = detail::checked_u128_to_signed_i64(res_neg, reduced_num);
        const uint64_t den_res = detail::checked_u128_to_pos_i64_u64(reduced_den);
        return from_canonical_parts(num_res, static_cast<int64_t>(den_res));
    }

    constexpr void simplify() {
        if (m_num == 0) {
            m_den = 1;
            return;
        }

        const bool result_neg = (m_num < 0) != (m_den < 0);
        uint64_t u_num = detail::abs_to_u64(m_num);
        uint64_t u_den = detail::abs_to_u64(m_den);

        const uint64_t g = detail::gcd_u64(u_num, u_den);
        if (g > 1) {
            u_num /= g;
            u_den /= g;
        }

        const uint64_t pos_den = detail::checked_u128_to_pos_i64_u64(detail::u128{u_den, 0});
        m_num = detail::checked_u128_to_signed_i64(result_neg, detail::u128{u_num, 0});
        m_den = static_cast<int64_t>(pos_den);
    }
};

enum class TimeConversionStatus {
    Ok,                      // Finite seconds > 0 rounded to frame_index >= 1 and <= MAX_FRAME_INDEX
    Zero,                    // +0.0, -0.0, or positive < half-frame rounding cleanly to Rational{0, 1}
    InvalidFrameRate,        // fps_num <= 0, fps_den <= 0, or sub-1-frame-per-240h (MAX_FRAME_INDEX == 0)
    InvalidNaN,              // std::isnan(seconds)
    ClampedNegative,         // Finite seconds < 0.0 -> Rational{0, 1}
    ClampedNegativeInfinity, // -Inf -> Rational{0, 1}
    ClampedPositiveInfinity, // +Inf -> MAX_SAFE_PTS
    ClampedAboveMax,         // Finite seconds > 864000.0 or rounded > MAX_FRAME_INDEX -> MAX_SAFE_PTS
    Overflow                 // Unsupported extreme positive rate where MAX_FRAME_INDEX > 2^53 or product > INT64_MAX, or raw_frames overflow
};

struct TimeConversionResult {
    TimeConversionStatus status{TimeConversionStatus::Ok};
    Rational pts{0, 1};

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return status == TimeConversionStatus::Ok || status == TimeConversionStatus::Zero;
    }
};

/**
 * @brief Safely quantizes UI floating-point seconds to a frame-aligned Rational PTS.
 */
[[nodiscard]] TimeConversionResult ui_seconds_to_rational_pts_safe(
    double seconds,
    Rational frame_rate);

} // namespace ave
