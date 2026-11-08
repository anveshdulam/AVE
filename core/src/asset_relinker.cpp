#include "ave/asset_relinker.hpp"

#include <unordered_map>
#include <algorithm>
#include <cctype>

namespace ave {

namespace {

std::string to_lower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

} // namespace

std::vector<MissingAssetInfo> AssetRelinker::find_missing_assets(const MediaPool& pool) {
    std::vector<MissingAssetInfo> missing;

    for (const auto& [id, asset] : pool.assets()) {
        const auto& path = asset.file_path();
        std::error_code ec;
        if (path.empty() || !std::filesystem::exists(path, ec) || ec) {
            MissingAssetInfo info;
            info.asset_id = id;
            info.original_path = path;
            info.display_name = asset.display_name();
            info.file_size_bytes = asset.file_size_bytes();
            missing.push_back(std::move(info));
        }
    }

    return missing;
}

bool AssetRelinker::relink_asset(MediaPool& pool,
                                 const std::string& asset_id,
                                 const std::filesystem::path& new_path) {
    std::error_code ec;
    if (!std::filesystem::exists(new_path, ec) || ec) {
        return false;
    }

    MediaAsset* asset = pool.find_asset_mut(asset_id);
    if (!asset) {
        return false;
    }

    asset->set_file_path(new_path);
    uint64_t sz = std::filesystem::file_size(new_path, ec);
    if (!ec) {
        asset->set_file_size_bytes(sz);
    }

    return true;
}

RelinkReport AssetRelinker::auto_relink_directory(MediaPool& pool,
                                                  const std::filesystem::path& search_directory,
                                                  bool recursive) {
    RelinkReport report;
    std::error_code ec;

    if (!std::filesystem::exists(search_directory, ec) || !std::filesystem::is_directory(search_directory, ec) || ec) {
        return report;
    }

    auto missing = find_missing_assets(pool);
    report.total_checked = missing.size();
    if (missing.empty()) {
        return report;
    }

    // Build filename lookup map from directory
    std::unordered_map<std::string, std::filesystem::path> candidates;

    auto index_entry = [&](const std::filesystem::directory_entry& entry) {
        if (entry.is_regular_file(ec)) {
            std::string fname = to_lower_str(entry.path().filename().string());
            if (candidates.find(fname) == candidates.end()) {
                candidates[fname] = entry.path();
            }
        }
    };

    if (recursive) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(search_directory, std::filesystem::directory_options::skip_permission_denied, ec)) {
            index_entry(entry);
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(search_directory, std::filesystem::directory_options::skip_permission_denied, ec)) {
            index_entry(entry);
        }
    }

    // Attempt matching
    for (const auto& m : missing) {
        std::string filename = to_lower_str(m.original_path.filename().string());
        if (filename.empty()) {
            filename = to_lower_str(m.display_name);
        }

        RelinkResult res;
        res.asset_id = m.asset_id;
        res.old_path = m.original_path;

        auto it = candidates.find(filename);
        if (it != candidates.end()) {
            res.new_path = it->second;
            res.success = relink_asset(pool, m.asset_id, it->second);
            if (res.success) {
                report.relinked_count++;
            } else {
                report.still_missing_count++;
            }
        } else {
            res.success = false;
            report.still_missing_count++;
        }

        report.results.push_back(std::move(res));
    }

    return report;
}

} // namespace ave
