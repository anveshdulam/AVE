#pragma once

#include "ave/time.hpp"

#include <vector>
#include <cstdint>

namespace ave {

/**
 * @brief Canonical decoded video frame ready for caching, compositing, and presentation.
 */
struct VideoFrame {
    uint64_t seek_generation{0};
    Rational pts{0, 1};
    Rational duration{0, 1};
    int width{0};
    int height{0};
    std::vector<uint8_t> rgba_data{};
    bool is_keyframe{false};

    [[nodiscard]] Rational pts_end() const noexcept {
        return pts + duration;
    }

    [[nodiscard]] bool operator==(const VideoFrame& other) const = default;
};

} // namespace ave
