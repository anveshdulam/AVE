#pragma once

#include "ave/media_pool.hpp"

#include <string>
#include <vector>
#include <filesystem>

namespace ave {

struct MissingAssetInfo {
    std::string asset_id{};
    std::filesystem::path original_path{};
    std::string display_name{};
    uint64_t file_size_bytes{0};
};

struct RelinkResult {
    std::string asset_id{};
    std::filesystem::path old_path{};
    std::filesystem::path new_path{};
    bool success{false};
};

struct RelinkReport {
    size_t total_checked{0};
    size_t relinked_count{0};
    size_t still_missing_count{0};
    std::vector<RelinkResult> results{};
};

/**
 * @brief Manages asset integrity, missing/offline media detection, and path relinking.
 */
class AssetRelinker {
public:
    /**
     * @brief Identifies assets whose media files no longer exist on disk.
     */
    [[nodiscard]] static std::vector<MissingAssetInfo> find_missing_assets(const MediaPool& pool);

    /**
     * @brief Non-destructively relinks a specific asset to a new filesystem path.
     */
    static bool relink_asset(MediaPool& pool,
                             const std::string& asset_id,
                             const std::filesystem::path& new_path);

    /**
     * @brief Searches a directory (optionally recursive) for matching filenames to relink missing assets.
     */
    static RelinkReport auto_relink_directory(MediaPool& pool,
                                             const std::filesystem::path& search_directory,
                                             bool recursive = true);
};

} // namespace ave
