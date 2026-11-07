#include "ave/media_pool.hpp"

#include <sstream>
#include <algorithm>
#include <stdexcept>

namespace ave {

// --- MediaClip Implementation ---

MediaClip::MediaClip(std::string id,
                     std::string asset_id,
                     std::string name,
                     Rational source_in,
                     Rational source_out)
    : id_(std::move(id)),
      asset_id_(std::move(asset_id)),
      name_(std::move(name)),
      source_in_(source_in),
      source_out_(source_out) {
    if (source_out_ < source_in_) {
        throw std::invalid_argument("Clip out_point cannot be earlier than in_point");
    }
}

void MediaClip::set_source_range(Rational in_point, Rational out_point) {
    if (out_point < in_point) {
        throw std::invalid_argument("Clip out_point cannot be earlier than in_point");
    }
    source_in_ = in_point;
    source_out_ = out_point;
}

// --- MediaBin Implementation ---

MediaBin::MediaBin(std::string id, std::string name, std::string parent_id)
    : id_(std::move(id)),
      name_(std::move(name)),
      parent_id_(std::move(parent_id)) {}

void MediaBin::add_clip(const std::string& clip_id) {
    if (std::find(clip_ids_.begin(), clip_ids_.end(), clip_id) == clip_ids_.end()) {
        clip_ids_.push_back(clip_id);
    }
}

bool MediaBin::remove_clip(const std::string& clip_id) {
    auto it = std::find(clip_ids_.begin(), clip_ids_.end(), clip_id);
    if (it != clip_ids_.end()) {
        clip_ids_.erase(it);
        return true;
    }
    return false;
}

void MediaBin::add_child_bin(const std::string& bin_id) {
    if (std::find(child_bin_ids_.begin(), child_bin_ids_.end(), bin_id) == child_bin_ids_.end()) {
        child_bin_ids_.push_back(bin_id);
    }
}

bool MediaBin::remove_child_bin(const std::string& bin_id) {
    auto it = std::find(child_bin_ids_.begin(), child_bin_ids_.end(), bin_id);
    if (it != child_bin_ids_.end()) {
        child_bin_ids_.erase(it);
        return true;
    }
    return false;
}

// --- MediaPool Implementation ---

MediaPool::MediaPool() {
    root_bin_id_ = "root-bin-" + UUID::generate();
    bins_[root_bin_id_] = MediaBin(root_bin_id_, "Master Bin", "");
}

const MediaAsset& MediaPool::register_asset(MediaAsset asset) {
    std::string id = asset.id();
    auto [it, _] = assets_.insert_or_assign(id, std::move(asset));
    return it->second;
}

const MediaAsset* MediaPool::find_asset(const std::string& asset_id) const noexcept {
    auto it = assets_.find(asset_id);
    return it != assets_.end() ? &it->second : nullptr;
}

bool MediaPool::remove_asset(const std::string& asset_id) {
    auto it = assets_.find(asset_id);
    if (it == assets_.end()) return false;

    // Remove any clips referencing this asset
    std::vector<std::string> clips_to_remove;
    for (const auto& [clip_id, clip] : clips_) {
        if (clip.asset_id() == asset_id) {
            clips_to_remove.push_back(clip_id);
        }
    }
    for (const auto& cid : clips_to_remove) {
        remove_clip(cid);
    }

    assets_.erase(it);
    return true;
}

std::string MediaPool::create_bin(const std::string& name, const std::string& parent_bin_id) {
    std::string parent = parent_bin_id.empty() ? root_bin_id_ : parent_bin_id;
    if (bins_.find(parent) == bins_.end()) {
        parent = root_bin_id_;
    }

    std::string new_id = "bin-" + UUID::generate();
    MediaBin new_bin(new_id, name, parent);
    bins_[new_id] = std::move(new_bin);

    bins_[parent].add_child_bin(new_id);
    return new_id;
}

const MediaBin* MediaPool::find_bin(const std::string& bin_id) const noexcept {
    auto it = bins_.find(bin_id);
    return it != bins_.end() ? &it->second : nullptr;
}

MediaBin* MediaPool::find_bin_mut(const std::string& bin_id) noexcept {
    auto it = bins_.find(bin_id);
    return it != bins_.end() ? &it->second : nullptr;
}

bool MediaPool::remove_bin(const std::string& bin_id) {
    if (bin_id == root_bin_id_) return false; // Cannot remove root bin
    auto it = bins_.find(bin_id);
    if (it == bins_.end()) return false;

    // Detach from parent
    if (!it->second.parent_id().empty()) {
        auto p_it = bins_.find(it->second.parent_id());
        if (p_it != bins_.end()) {
            p_it->second.remove_child_bin(bin_id);
        }
    }

    // Move child clips to root
    for (const auto& cid : it->second.clip_ids()) {
        bins_[root_bin_id_].add_clip(cid);
    }

    // Move child bins to root
    for (const auto& c_bin_id : it->second.child_bin_ids()) {
        auto cb_it = bins_.find(c_bin_id);
        if (cb_it != bins_.end()) {
            bins_[root_bin_id_].add_child_bin(c_bin_id);
        }
    }

    bins_.erase(it);
    return true;
}

std::string MediaPool::create_clip(const std::string& asset_id,
                                   const std::string& target_bin_id,
                                   std::optional<std::string> custom_name) {
    auto a_it = assets_.find(asset_id);
    if (a_it == assets_.end()) {
        throw std::invalid_argument("Cannot create clip: asset_id not found in pool: " + asset_id);
    }

    std::string clip_id = "clip-" + UUID::generate();
    std::string name = custom_name.value_or(a_it->second.display_name());
    Rational in_point{0, 1};
    Rational out_point = a_it->second.duration();

    MediaClip clip(clip_id, asset_id, std::move(name), in_point, out_point);
    clips_[clip_id] = std::move(clip);

    std::string bin_id = target_bin_id.empty() ? root_bin_id_ : target_bin_id;
    if (bins_.find(bin_id) == bins_.end()) {
        bin_id = root_bin_id_;
    }
    bins_[bin_id].add_clip(clip_id);

    return clip_id;
}

const MediaClip* MediaPool::find_clip(const std::string& clip_id) const noexcept {
    auto it = clips_.find(clip_id);
    return it != clips_.end() ? &it->second : nullptr;
}

MediaClip* MediaPool::find_clip_mut(const std::string& clip_id) noexcept {
    auto it = clips_.find(clip_id);
    return it != clips_.end() ? &it->second : nullptr;
}

bool MediaPool::move_clip(const std::string& clip_id, const std::string& destination_bin_id) {
    if (clips_.find(clip_id) == clips_.end()) return false;
    if (bins_.find(destination_bin_id) == bins_.end()) return false;

    // Remove from previous bin
    for (auto& [bid, bin] : bins_) {
        bin.remove_clip(clip_id);
    }

    bins_[destination_bin_id].add_clip(clip_id);
    return true;
}

bool MediaPool::remove_clip(const std::string& clip_id) {
    auto it = clips_.find(clip_id);
    if (it == clips_.end()) return false;

    for (auto& [bid, bin] : bins_) {
        bin.remove_clip(clip_id);
    }
    clips_.erase(it);
    return true;
}

std::vector<const MediaClip*> MediaPool::search_clips(const std::string& query) const {
    std::vector<const MediaClip*> results;
    if (query.empty()) {
        for (const auto& [_, clip] : clips_) {
            results.push_back(&clip);
        }
        return results;
    }

    auto to_lower_char = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::string q_lower = query;
    std::transform(q_lower.begin(), q_lower.end(), q_lower.begin(), to_lower_char);

    for (const auto& [_, clip] : clips_) {
        std::string n_lower = clip.name();
        std::transform(n_lower.begin(), n_lower.end(), n_lower.begin(), to_lower_char);

        std::string c_lower = clip.color_tag();
        std::transform(c_lower.begin(), c_lower.end(), c_lower.begin(), to_lower_char);

        if (n_lower.find(q_lower) != std::string::npos || c_lower.find(q_lower) != std::string::npos) {
            results.push_back(&clip);
        }
    }
    return results;
}

std::string MediaPool::to_json() const {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"root_bin_id\": \"" << root_bin_id_ << "\",\n";

    // Assets array
    ss << "  \"assets\": [\n";
    size_t a_idx = 0;
    for (const auto& [_, asset] : assets_) {
        ss << asset.to_json() << (a_idx + 1 < assets_.size() ? ",\n" : "\n");
        a_idx++;
    }
    ss << "  ],\n";

    // Clips array
    ss << "  \"clips\": [\n";
    size_t c_idx = 0;
    for (const auto& [_, clip] : clips_) {
        ss << "    {\n";
        ss << "      \"id\": \"" << clip.id() << "\",\n";
        ss << "      \"asset_id\": \"" << clip.asset_id() << "\",\n";
        ss << "      \"name\": \"" << clip.name() << "\",\n";
        ss << "      \"color_tag\": \"" << clip.color_tag() << "\",\n";
        ss << "      \"source_in\": {\"num\": " << clip.source_in().numerator() << ", \"den\": " << clip.source_in().denominator() << "},\n";
        ss << "      \"source_out\": {\"num\": " << clip.source_out().numerator() << ", \"den\": " << clip.source_out().denominator() << "}\n";
        ss << "    }" << (c_idx + 1 < clips_.size() ? ",\n" : "\n");
        c_idx++;
    }
    ss << "  ],\n";

    // Bins array
    ss << "  \"bins\": [\n";
    size_t b_idx = 0;
    for (const auto& [_, bin] : bins_) {
        ss << "    {\n";
        ss << "      \"id\": \"" << bin.id() << "\",\n";
        ss << "      \"name\": \"" << bin.name() << "\",\n";
        ss << "      \"parent_id\": \"" << bin.parent_id() << "\",\n";
        ss << "      \"clip_ids\": [";
        for (size_t i = 0; i < bin.clip_ids().size(); ++i) {
            ss << "\"" << bin.clip_ids()[i] << "\"" << (i + 1 < bin.clip_ids().size() ? ", " : "");
        }
        ss << "],\n";
        ss << "      \"child_bin_ids\": [";
        for (size_t i = 0; i < bin.child_bin_ids().size(); ++i) {
            ss << "\"" << bin.child_bin_ids()[i] << "\"" << (i + 1 < bin.child_bin_ids().size() ? ", " : "");
        }
        ss << "]\n";
        ss << "    }" << (b_idx + 1 < bins_.size() ? ",\n" : "\n");
        b_idx++;
    }
    ss << "  ]\n";
    ss << "}";
    return ss.str();
}

} // namespace ave
