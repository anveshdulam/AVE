#pragma once

#include "ave/media_asset.hpp"
#include "ave/time.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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
        if (width <= 0 || height <= 0) {
            return false;
        }
        const uint64_t expected_pixels = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
        if (expected_pixels > (std::numeric_limits<size_t>::max() / 4u)) {
            return false;
        }
        return rgba_pixels.size() == static_cast<size_t>(expected_pixels * 4u);
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
        if (channels <= 0 || channel_peaks.size() != static_cast<size_t>(channels)) {
            return false;
        }
        for (const auto& ch : channel_peaks) {
            if (ch.min_peaks.size() != ch.max_peaks.size()) {
                return false;
            }
        }
        return true;
    }
};

struct MediaCacheConfig {
    static constexpr uint32_t kDefaultFormatVersion = 1;
    static constexpr uint64_t kDefaultMaxCacheBytes = 512ULL * 1024ULL * 1024ULL;
    static constexpr uint64_t kDefaultMaxMemoryBytes = 128ULL * 1024ULL * 1024ULL;

    uint32_t format_version{kDefaultFormatVersion};
    uint64_t max_cache_bytes{kDefaultMaxCacheBytes};
    uint64_t max_memory_bytes{kDefaultMaxMemoryBytes};
};

struct MediaCacheStats {
    uint64_t memory_hits{0};
    uint64_t disk_hits{0};
    uint64_t misses{0};
    uint64_t corrupt_rejections{0};
    uint64_t version_rejections{0};
    uint64_t stale_rejections{0};
    uint64_t disk_writes{0};
    uint64_t write_failures{0};
    uint64_t sequence_superseded{0};
    uint64_t evictions{0};
    uint64_t disk_bytes_used{0};
    uint64_t memory_bytes_used{0};
    size_t thumbnail_entries{0};
    size_t waveform_entries{0};
    size_t probe_entries{0};
};

/**
 * @brief Thread-safe persistent + LRU memory cache for derived media artifacts
 * (thumbnails, audio waveforms, and probed container/stream metadata).
 */
class MediaCache {
public:
    static constexpr uint32_t kFormatVersion = MediaCacheConfig::kDefaultFormatVersion;
    static constexpr uint64_t kDefaultMaxCacheBytes = MediaCacheConfig::kDefaultMaxCacheBytes;

    explicit MediaCache(std::filesystem::path cache_dir, MediaCacheConfig config = {});

    [[nodiscard]] const std::filesystem::path& cache_dir() const noexcept { return cache_dir_; }
    [[nodiscard]] uint32_t format_version() const noexcept { return config_.format_version; }

    void set_max_cache_bytes(uint64_t max_bytes);
    [[nodiscard]] uint64_t max_cache_bytes() const;
    void set_max_memory_bytes(uint64_t max_bytes);
    [[nodiscard]] uint64_t max_memory_bytes() const;
    void evict_to_budget();

    // Canonical Source-Addressed Persistent APIs (preferred)
    void store_thumbnail(const MediaAsset& asset,
                         Rational timestamp,
                         int width,
                         int height,
                         ThumbnailData data,
                         std::shared_ptr<std::atomic<bool>> cancel_state = nullptr);
    [[nodiscard]] std::optional<ThumbnailData> get_thumbnail(
        const MediaAsset& asset, Rational timestamp, int width, int height) const;
    [[nodiscard]] std::optional<ThumbnailData> get_thumbnail_memory_only(
        const MediaAsset& asset, Rational timestamp, int width, int height) const;

    void store_waveform(const MediaAsset& asset,
                        WaveformData data,
                        std::shared_ptr<std::atomic<bool>> cancel_state = nullptr);
    [[nodiscard]] std::optional<WaveformData> get_waveform(
        const MediaAsset& asset, int peaks_per_second = 100) const;
    [[nodiscard]] std::optional<WaveformData> get_waveform_memory_only(
        const MediaAsset& asset, int peaks_per_second = 100) const;

    void store_probe(const std::filesystem::path& file_path,
                     const MediaAsset& probed_asset,
                     std::shared_ptr<std::atomic<bool>> cancel_state = nullptr);
    [[nodiscard]] std::optional<MediaAsset> get_probe(const std::filesystem::path& file_path) const;

    // Single-flight cache-backed extraction (deduplicates concurrent identical generation)
    [[nodiscard]] std::optional<ThumbnailData> get_or_extract_thumbnail(
        const MediaAsset& asset,
        Rational timestamp,
        int width,
        int height,
        const std::function<std::optional<ThumbnailData>(const std::shared_ptr<std::atomic<bool>>&)>& extractor,
        std::shared_ptr<std::atomic<bool>> cancel_state = nullptr,
        bool* cache_hit_out = nullptr);

    [[nodiscard]] std::optional<WaveformData> get_or_extract_waveform(
        const MediaAsset& asset,
        int peaks_per_second,
        const std::function<std::optional<WaveformData>(const std::shared_ptr<std::atomic<bool>>&)>& extractor,
        std::shared_ptr<std::atomic<bool>> cancel_state = nullptr,
        bool* cache_hit_out = nullptr);

    // Legacy synthetic asset-id RAM APIs (preserved for existing synthetic unit tests)
    void store_thumbnail(const std::string& asset_id, Rational timestamp, int width, int height, ThumbnailData data);
    [[nodiscard]] std::optional<ThumbnailData> get_thumbnail(const std::string& asset_id, Rational timestamp, int width, int height) const;

    void store_waveform(const std::string& asset_id, WaveformData data);
    [[nodiscard]] std::optional<WaveformData> get_waveform(const std::string& asset_id) const;

    // Maintenance & Diagnostics
    void clear();
    [[nodiscard]] size_t thumbnail_count() const;
    [[nodiscard]] size_t waveform_count() const;
    [[nodiscard]] size_t probe_count() const;
    [[nodiscard]] uint64_t disk_usage_bytes() const;
    [[nodiscard]] uint64_t memory_usage_bytes() const;
    [[nodiscard]] MediaCacheStats stats() const;
    void reset_stats();
    [[nodiscard]] std::string last_error() const;

    // Deterministic test seams
    [[nodiscard]] std::optional<std::filesystem::path> thumbnail_cache_path_for_test(
        const MediaAsset& asset, Rational timestamp, int width, int height) const;
    [[nodiscard]] std::optional<std::filesystem::path> waveform_cache_path_for_test(
        const MediaAsset& asset, int peaks_per_second = 100) const;
    [[nodiscard]] std::optional<std::filesystem::path> probe_cache_path_for_test(
        const std::filesystem::path& file_path) const;

    void set_pre_replace_hook_for_test(
        std::function<void(const std::string& cache_key, const std::filesystem::path& target_path)> hook);
    void set_simulate_write_failure_for_test(bool fail);

private:
    enum class ArtifactKind : uint32_t {
        Thumbnail = 1,
        Waveform = 2,
        Probe = 3
    };

    struct SourceIdentity {
        bool valid{false};
        std::string normalized_path{};
        uint64_t file_size{0};
        int64_t mtime_ns{0};
        std::string media_descriptor{};
        std::string cache_key{};
        std::filesystem::path disk_path{};
    };

    struct MemoryEntry {
        ArtifactKind kind{ArtifactKind::Thumbnail};
        std::string key{};
        uint64_t byte_size{0};
        uint64_t lru_tick{0};
        std::optional<ThumbnailData> thumbnail{};
        std::optional<WaveformData> waveform{};
        std::optional<MediaAsset> probe{};
    };

    struct DiskEntry {
        ArtifactKind kind{ArtifactKind::Thumbnail};
        std::string key{};
        std::filesystem::path file_path{};
        uint64_t byte_size{0};
        uint64_t lru_tick{0};
    };

    struct InFlightJob {
        std::mutex mu{};
        std::condition_variable cv{};
        bool finished{false};
        std::optional<ThumbnailData> thumbnail{};
        std::optional<WaveformData> waveform{};
    };

    class PinGuard {
    public:
        PinGuard(const MediaCache* owner, std::string key);
        ~PinGuard();
        PinGuard(const PinGuard&) = delete;
        PinGuard& operator=(const PinGuard&) = delete;

    private:
        const MediaCache* owner_{nullptr};
        std::string key_{};
    };

    [[nodiscard]] std::filesystem::path version_root_dir() const;
    [[nodiscard]] std::filesystem::path disk_path_for_key(ArtifactKind kind, const std::string& key) const;
    [[nodiscard]] SourceIdentity build_source_identity(
        ArtifactKind kind,
        const std::filesystem::path& file_path,
        const MediaAsset* asset,
        const std::string& params_json) const;
    [[nodiscard]] static std::string build_media_descriptor(const MediaAsset* asset);

    void scan_existing_disk_entries();
    void pin_key(const std::string& key) const;
    void unpin_key(const std::string& key) const;
    void record_error(std::string message) const;

    void put_memory_entry_locked(MemoryEntry entry) const;
    void evict_memory_lru_locked() const;
    void evict_disk_lru_locked() const;
    void touch_disk_entry_mtime(const std::filesystem::path& path) const;

    [[nodiscard]] bool write_artifact_atomic(
        const SourceIdentity& identity,
        ArtifactKind kind,
        const std::string& meta_json,
        const void* payload_ptr,
        size_t payload_size,
        std::shared_ptr<std::atomic<bool>> cancel_state,
        uint64_t assigned_seq) const;

    template <typename T>
    [[nodiscard]] std::optional<T> load_artifact_from_disk(
        const SourceIdentity& identity,
        ArtifactKind kind,
        const std::function<bool(const std::string& meta_json, const std::vector<uint8_t>& payload, T& out)>& decoder) const;

    std::filesystem::path cache_dir_{};
    MediaCacheConfig config_{};
    mutable std::mutex mutex_{};
    mutable std::mutex in_flight_mutex_{};
    mutable std::mutex publish_mutex_{};

    mutable std::unordered_map<std::string, MemoryEntry> memory_entries_{};
    mutable std::unordered_map<std::string, DiskEntry> disk_entries_{};
    mutable std::unordered_map<std::string, size_t> pinned_keys_{};
    mutable std::unordered_map<std::string, uint64_t> next_write_seq_by_key_{};
    mutable std::unordered_map<std::string, uint64_t> committed_seq_by_key_{};
    mutable std::unordered_map<std::string, std::shared_ptr<InFlightJob>> in_flight_jobs_{};

    // Legacy synthetic asset-id fallback maps (RAM-only, strictly isolated from real files)
    mutable std::unordered_map<std::string, ThumbnailData> legacy_thumbnail_cache_{};
    mutable std::unordered_map<std::string, WaveformData> legacy_waveform_cache_{};

    mutable uint64_t lru_tick_{0};
    mutable uint64_t memory_bytes_used_{0};
    mutable uint64_t disk_bytes_used_{0};
    mutable std::filesystem::file_time_type last_touch_time_{};
    mutable std::atomic<uint64_t> temp_counter_{0};
    mutable bool cache_dir_writable_{true};
    mutable MediaCacheStats stats_{};
    mutable std::string last_error_{};

    std::function<void(const std::string&, const std::filesystem::path&)> pre_replace_hook_{};
    std::atomic<bool> simulate_write_failure_{false};
};

} // namespace ave
