#pragma once

#include "ave/media_asset.hpp"
#include "ave/time.hpp"

#include <vector>
#include <string>
#include <optional>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <mutex>

namespace ave {

/**
 * @brief Represents decoded RGBA thumbnail image data.
 */
struct ThumbnailData {
    int width{0};
    int height{0};
    Rational timestamp{0, 1};
    std::vector<uint8_t> rgba_pixels{}; // 4 bytes per pixel (R, G, B, A)

    [[nodiscard]] bool is_valid() const noexcept {
        return width > 0 && height > 0 && rgba_pixels.size() == static_cast<size_t>(width * height * 4);
    }
};

/**
 * @brief Min/Max peak envelope for an audio channel.
 */
struct WaveformChannel {
    std::vector<float> min_peaks{};
    std::vector<float> max_peaks{};

    [[nodiscard]] size_t size() const noexcept { return min_peaks.size(); }
};

/**
 * @brief Downsampled audio waveform peaks for instant timeline rendering.
 */
struct WaveformData {
    int channels{0};
    int sample_rate{0};
    int peaks_per_second{100}; // standard resolution: 100 peak pairs per second
    Rational duration{0, 1};
    std::vector<WaveformChannel> channel_peaks{};

    [[nodiscard]] bool is_valid() const noexcept {
        return channels > 0 && !channel_peaks.empty() && channel_peaks.size() == static_cast<size_t>(channels);
    }
};

/**
 * @brief Thread-safe cache store for media thumbnails, waveforms, and proxies.
 */
class MediaCache {
public:
    explicit MediaCache(std::filesystem::path cache_dir);

    [[nodiscard]] const std::filesystem::path& cache_dir() const noexcept { return cache_dir_; }

    // Thumbnail Cache
    void store_thumbnail(const std::string& asset_id, Rational timestamp, int width, int height, ThumbnailData data);
    [[nodiscard]] std::optional<ThumbnailData> get_thumbnail(const std::string& asset_id, Rational timestamp, int width, int height) const;

    // Waveform Cache
    void store_waveform(const std::string& asset_id, WaveformData data);
    [[nodiscard]] std::optional<WaveformData> get_waveform(const std::string& asset_id) const;

    // Cache maintenance
    void clear();
    [[nodiscard]] size_t thumbnail_count() const;
    [[nodiscard]] size_t waveform_count() const;

private:
    std::filesystem::path cache_dir_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ThumbnailData> thumbnail_cache_;
    std::unordered_map<std::string, WaveformData> waveform_cache_;

    [[nodiscard]] static std::string make_thumbnail_key(const std::string& asset_id, Rational timestamp, int width, int height);
};

} // namespace ave
