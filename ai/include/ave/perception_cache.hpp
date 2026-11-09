#pragma once

#include <ave/perception_report.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace ave {

/**
 * @brief Manages persistence and retrieval of perception results and raw probability curves.
 *
 * Cache keys are deterministically generated from:
 * asset content hash + analyzer ID & version + model SHA-256 + parameters.
 *
 * Employs atomic write-to-temp and rename to prevent file corruption.
 */
class PerceptionCache {
public:
    explicit PerceptionCache(std::filesystem::path cache_dir);

    [[nodiscard]] const std::filesystem::path& cache_dir() const noexcept { return m_cache_dir; }

    /**
     * @brief Computes a deterministic cache key from constituent provenance metadata.
     */
    [[nodiscard]] static std::string generate_key(
        const std::string& asset_content_hash,
        const std::string& analyzer_id,
        const std::string& analyzer_version,
        const std::string& model_sha256,
        const std::string& params_json = "{}"
    );

    /**
     * @brief Checks if a cached report exists for the given key.
     */
    [[nodiscard]] bool has_report(const std::string& key) const;

    /**
     * @brief Saves a PerceptionReport atomically to disk.
     */
    bool save_report(const std::string& key, const PerceptionReport& report);

    /**
     * @brief Loads a PerceptionReport from disk. Returns nullopt if missing or invalid.
     */
    [[nodiscard]] std::optional<PerceptionReport> load_report(const std::string& key) const;

    /**
     * @brief Invalidates a specific cached entry.
     */
    bool invalidate(const std::string& key);

    /**
     * @brief Clears all cached perception artifacts.
     */
    void clear();

    /**
     * @brief Resolves the absolute path for a cache key.
     */
    [[nodiscard]] std::filesystem::path resolve_path(const std::string& key) const;

private:
    std::filesystem::path m_cache_dir;
};

} // namespace ave
