#pragma once

#include "ave/media_cache.hpp"
#include "ave/time.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace ave {

/**
 * @brief Decodes video frames at arbitrary timestamps and converts to RGBA thumbnails.
 */
class ThumbnailExtractor {
public:
    [[nodiscard]] static std::optional<ThumbnailData> extract(
        const std::filesystem::path& file_path,
        Rational timestamp,
        int target_width = 160,
        int target_height = 90,
        std::shared_ptr<std::atomic<bool>> cancel_state = nullptr
    );
};

/**
 * @brief Decodes audio packets and computes peak envelope min/max pairs.
 */
class WaveformExtractor {
public:
    [[nodiscard]] static std::optional<WaveformData> extract(
        const std::filesystem::path& file_path,
        int peaks_per_second = 100,
        std::shared_ptr<std::atomic<bool>> cancel_state = nullptr
    );
};

} // namespace ave

