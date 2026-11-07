#pragma once

#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <string>

namespace ave {

/**
 * @brief Canonical rational time representation for editorial timing.
 *
 * Uses exact 64-bit numerator and denominator. Never stores canonical
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

    [[nodiscard]] int64_t to_frames(const Rational& frame_rate) const {
        // frames = (m_num / m_den) * (frame_rate.num / frame_rate.den)
        // frames = (m_num * frame_rate.num) / (m_den * frame_rate.den)
        int64_t n = m_num * frame_rate.numerator();
        int64_t d = m_den * frame_rate.denominator();
        return n / d;
    }

    constexpr bool operator==(const Rational& other) const noexcept {
        return m_num == other.m_num && m_den == other.m_den;
    }

    constexpr bool operator!=(const Rational& other) const noexcept {
        return !(*this == other);
    }

    constexpr bool operator<(const Rational& other) const noexcept {
        return (m_num * other.m_den) < (other.m_num * m_den);
    }

    constexpr Rational operator+(const Rational& other) const {
        int64_t num = m_num * other.m_den + other.m_num * m_den;
        int64_t den = m_den * other.m_den;
        return Rational(num, den);
    }

    constexpr Rational operator-(const Rational& other) const {
        int64_t num = m_num * other.m_den - other.m_num * m_den;
        int64_t den = m_den * other.m_den;
        return Rational(num, den);
    }

    constexpr Rational operator*(const Rational& other) const {
        return Rational(m_num * other.m_num, m_den * other.m_den);
    }

    constexpr Rational operator*(int64_t scalar) const {
        return Rational(m_num * scalar, m_den);
    }

    constexpr Rational operator/(const Rational& other) const {
        if (other.m_num == 0) {
            throw std::invalid_argument("Division by zero rational");
        }
        return Rational(m_num * other.m_den, m_den * other.m_num);
    }

    constexpr Rational operator/(int64_t scalar) const {
        if (scalar == 0) {
            throw std::invalid_argument("Division by zero scalar");
        }
        return Rational(m_num, m_den * scalar);
    }

    friend constexpr Rational operator*(int64_t scalar, const Rational& r) {
        return r * scalar;
    }

private:
    int64_t m_num;
    int64_t m_den;

    constexpr void simplify() noexcept {
        if (m_den < 0) {
            m_num = -m_num;
            m_den = -m_den;
        }
        int64_t g = std::gcd(m_num, m_den);
        if (g > 1) {
            m_num /= g;
            m_den /= g;
        }
    }
};

} // namespace ave
