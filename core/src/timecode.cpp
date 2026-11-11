#include "ave/timecode.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace ave {

namespace {

[[nodiscard]] bool ceil_fps_from_rational(Rational frame_rate, uint64_t& out_ceil_fps) noexcept {
    const int64_t fn = frame_rate.numerator();
    const int64_t fd = frame_rate.denominator();
    if (fn <= 0 || fd <= 0) {
        return false;
    }

    const detail::u128 num_plus = detail::add_u128(
        detail::u128{static_cast<uint64_t>(fn), 0},
        detail::u128{static_cast<uint64_t>(fd - 1), 0});
    const detail::u128 q = detail::divmod_u128_u64(num_plus, static_cast<uint64_t>(fd));
    if (q.hi != 0 || q.lo == 0) {
        return false;
    }
    out_ceil_fps = q.lo;
    return true;
}

[[nodiscard]] bool parse_two_ascii_digits(char c0, char c1, int& out_val) noexcept {
    if (c0 < '0' || c0 > '9' || c1 < '0' || c1 > '9') {
        return false;
    }
    out_val = (c0 - '0') * 10 + (c1 - '0');
    return true;
}

} // namespace

bool Timecode::is_valid(Rational frame_rate) const noexcept {
    if (drop_frame) {
        return false;
    }
    if (hours < 0 || minutes < 0 || minutes >= 60 || seconds < 0 || seconds >= 60 || frames < 0) {
        return false;
    }
    uint64_t ceil_fps = 0;
    if (!ceil_fps_from_rational(frame_rate, ceil_fps)) {
        return false;
    }
    return static_cast<uint64_t>(frames) < ceil_fps;
}

Timecode Timecode::from_rational(Rational time, Rational frame_rate) {
    uint64_t ceil_fps = 0;
    if (!ceil_fps_from_rational(frame_rate, ceil_fps)) {
        throw std::invalid_argument("Frame rate must be strictly positive");
    }

    if (time.numerator() <= 0) {
        return Timecode{0, 0, 0, 0, false};
    }

    const uint64_t t_num = static_cast<uint64_t>(time.numerator());
    const uint64_t t_den = static_cast<uint64_t>(time.denominator());
    const uint64_t fn = static_cast<uint64_t>(frame_rate.numerator());
    const uint64_t fd = static_cast<uint64_t>(frame_rate.denominator());

    const uint64_t g1 = detail::gcd_u64(t_num, fd);
    const uint64_t g2 = detail::gcd_u64(t_den, fn);

    const detail::u128 num_128 = detail::mul_u64_to_u128(t_num / g1, fn / g2);
    const detail::u128 den_128 = detail::mul_u64_to_u128(t_den / g2, fd / g1);
    const detail::u128 total_f = detail::divmod_u128_u128(num_128, den_128).quotient;

    uint64_t f_rem = 0;
    const detail::u128 total_s = detail::divmod_u128_u64(total_f, ceil_fps, &f_rem);

    uint64_t s_rem = 0;
    const detail::u128 total_m = detail::divmod_u128_u64(total_s, 60ULL, &s_rem);

    uint64_t m_rem = 0;
    const detail::u128 total_h = detail::divmod_u128_u64(total_m, 60ULL, &m_rem);

    constexpr uint64_t kMaxInt = static_cast<uint64_t>(std::numeric_limits<int>::max());
    if (total_h.hi != 0 || total_h.lo > kMaxInt || f_rem > kMaxInt) {
        throw std::overflow_error("Timecode hours or frames overflow 32-bit signed int");
    }

    return Timecode{
        static_cast<int>(total_h.lo),
        static_cast<int>(m_rem),
        static_cast<int>(s_rem),
        static_cast<int>(f_rem),
        false
    };
}

std::optional<Timecode> Timecode::try_from_string(std::string_view str) noexcept {
    // Strictly validate exact HH:MM:SS:FF length before copying into a bounded null-terminated buffer
    if (str.size() != 11) {
        return std::nullopt;
    }

    std::array<char, 12> buf{};
    std::memcpy(buf.data(), str.data(), 11);
    buf[11] = '\0';

    if (buf[2] != ':' || buf[5] != ':') {
        return std::nullopt;
    }
    if (buf[8] != ':') {
        return std::nullopt; // Includes explicit rejection of drop-frame ';'
    }

    int h = 0;
    int m = 0;
    int s = 0;
    int f = 0;
    if (!parse_two_ascii_digits(buf[0], buf[1], h) ||
        !parse_two_ascii_digits(buf[3], buf[4], m) ||
        !parse_two_ascii_digits(buf[6], buf[7], s) ||
        !parse_two_ascii_digits(buf[9], buf[10], f)) {
        return std::nullopt;
    }

    if (m >= 60 || s >= 60) {
        return std::nullopt;
    }

    return Timecode{h, m, s, f, false};
}

Timecode Timecode::from_string(std::string_view str) {
    if (str.size() == 11 && str[8] == ';') {
        throw std::invalid_argument("Drop-frame ';' timecode notation is unsupported; expected Non-Drop-Frame HH:MM:SS:FF");
    }
    const auto parsed = try_from_string(str);
    if (!parsed.has_value()) {
        throw std::invalid_argument("Invalid SMPTE Non-Drop-Frame timecode string; expected HH:MM:SS:FF");
    }
    return *parsed;
}

Rational Timecode::to_rational(Rational frame_rate) const {
    if (frame_rate.numerator() <= 0 || frame_rate.denominator() <= 0) {
        throw std::invalid_argument("Frame rate must be strictly positive");
    }
    if (drop_frame) {
        throw std::invalid_argument("Drop-frame timecode is unsupported in Phase 10");
    }
    if (!is_valid(frame_rate)) {
        throw std::invalid_argument("Timecode fields out of valid range for frame rate");
    }

    const int64_t frames_tot = total_frames(frame_rate);
    if (frames_tot < 0) {
        throw std::overflow_error("Total frame count overflows int64_t");
    }
    if (frames_tot == 0) {
        return Rational{0, 1};
    }

    const uint64_t fn = static_cast<uint64_t>(frame_rate.numerator());
    const uint64_t fd = static_cast<uint64_t>(frame_rate.denominator());
    const uint64_t u_frames = static_cast<uint64_t>(frames_tot);
    const uint64_t g = detail::gcd_u64(u_frames, fn);

    const detail::u128 num_128 = detail::mul_u64_to_u128(u_frames / g, fd);
    const int64_t num_res = detail::checked_u128_to_signed_i64(false, num_128);
    return Rational{num_res, static_cast<int64_t>(fn / g)};
}

std::string Timecode::to_string() const {
    if (drop_frame) {
        throw std::invalid_argument("Drop-frame timecode formatting is unsupported in Phase 10");
    }
    if (hours < 0 || minutes < 0 || minutes >= 60 || seconds < 0 || seconds >= 60 || frames < 0) {
        throw std::invalid_argument("Cannot format out-of-range Timecode fields");
    }

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d:%02d", hours, minutes, seconds, frames);
    return std::string(buf);
}

int64_t Timecode::total_frames(Rational frame_rate) const noexcept {
    if (!is_valid(frame_rate)) {
        return -1;
    }

    uint64_t ceil_fps = 0;
    if (!ceil_fps_from_rational(frame_rate, ceil_fps)) {
        return -1;
    }

    const uint64_t total_sec =
        static_cast<uint64_t>(hours) * 3600ULL +
        static_cast<uint64_t>(minutes) * 60ULL +
        static_cast<uint64_t>(seconds);

    const detail::u128 prod = detail::mul_u64_to_u128(total_sec, ceil_fps);
    const detail::u128 sum = detail::add_u128(prod, detail::u128{static_cast<uint64_t>(frames), 0});
    if (detail::cmp_u128(sum, prod) < 0) {
        return -1; // 128-bit wrap guard
    }
    if (sum.hi != 0 || sum.lo > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return -1;
    }
    return static_cast<int64_t>(sum.lo);
}

} // namespace ave
