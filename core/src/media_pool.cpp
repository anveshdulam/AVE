#include "ave/media_pool.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

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

MediaAsset* MediaPool::find_asset_mut(const std::string& asset_id) noexcept {
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
            cb_it->second.set_parent_id(root_bin_id_);
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

namespace {

constexpr const char* kMediaPoolSchema10 = "1.0.0";
constexpr const char* kMediaPoolSchema11 = "1.1.0";
constexpr const char* kMediaPoolSchema12 = "1.2.0";

nlohmann::json pool_rational_to_json(Rational r) {
    return nlohmann::json{{"num", r.numerator()}, {"den", r.denominator()}};
}

std::string pool_require_non_empty_string(const nlohmann::json& obj, const char* key, const std::string& ctx) {
    if (!obj.contains(key) || !obj.at(key).is_string()) {
        throw std::runtime_error("MediaPool validation error: " + ctx + " missing or invalid string '" + key + "'");
    }
    std::string val = obj.at(key).get<std::string>();
    if (val.empty()) {
        throw std::runtime_error("MediaPool validation error: " + ctx + "." + key + " must not be empty");
    }
    return val;
}

std::string pool_optional_string(const nlohmann::json& obj,
                                 const char* key,
                                 const std::string& default_val,
                                 const std::string& ctx) {
    if (!obj.contains(key)) {
        return default_val;
    }
    if (!obj.at(key).is_string()) {
        throw std::runtime_error("MediaPool validation error: " + ctx + "." + key + " must be a string");
    }
    return obj.at(key).get<std::string>();
}

Rational pool_optional_rational(const nlohmann::json& obj,
                                const char* key,
                                Rational default_val,
                                const std::string& ctx) {
    if (!obj.contains(key)) {
        return default_val;
    }
    const auto& r = obj.at(key);
    if (!r.is_object() || !r.contains("num") || !r.contains("den")) {
        throw std::runtime_error("MediaPool validation error: " + ctx + "." + key + " must be an object with 'num' and 'den'");
    }
    if (!r.at("num").is_number_integer() || !r.at("den").is_number_integer()) {
        throw std::runtime_error("MediaPool validation error: " + ctx + "." + key + " 'num' and 'den' must be integers");
    }
    const int64_t num = r.at("num").get<int64_t>();
    const int64_t den = r.at("den").get<int64_t>();
    if (den <= 0) {
        throw std::runtime_error("MediaPool validation error: " + ctx + "." + key + " denominator must be > 0");
    }
    return Rational{num, den};
}

std::vector<std::string> sorted_keys(const auto& map) {
    std::vector<std::string> keys;
    keys.reserve(map.size());
    for (const auto& [k, _] : map) {
        keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

} // namespace

std::string MediaPool::to_json() const {
    nlohmann::json assets_arr = nlohmann::json::array();
    for (const std::string& aid : sorted_keys(assets_)) {
        assets_arr.push_back(nlohmann::json::parse(assets_.at(aid).to_json()));
    }

    nlohmann::json clips_arr = nlohmann::json::array();
    for (const std::string& cid : sorted_keys(clips_)) {
        const MediaClip& clip = clips_.at(cid);
        clips_arr.push_back(nlohmann::json{
            {"id", clip.id()},
            {"asset_id", clip.asset_id()},
            {"name", clip.name()},
            {"color_tag", clip.color_tag()},
            {"source_in", pool_rational_to_json(clip.source_in())},
            {"source_out", pool_rational_to_json(clip.source_out())}
        });
    }

    nlohmann::json bins_arr = nlohmann::json::array();
    for (const std::string& bid : sorted_keys(bins_)) {
        const MediaBin& bin = bins_.at(bid);
        bins_arr.push_back(nlohmann::json{
            {"id", bin.id()},
            {"name", bin.name()},
            {"parent_id", bin.parent_id()},
            {"clip_ids", bin.clip_ids()},
            {"child_bin_ids", bin.child_bin_ids()}
        });
    }

    nlohmann::json root{
        {"schema_version", kMediaPoolSchema12},
        {"root_bin_id", root_bin_id_},
        {"assets", std::move(assets_arr)},
        {"clips", std::move(clips_arr)},
        {"bins", std::move(bins_arr)}
    };
    return root.dump(2);
}

MediaPool MediaPool::from_json(const std::string& json_text) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(std::string("MediaPool JSON parse error: ") + e.what());
    }

    if (!root.is_object()) {
        throw std::runtime_error("MediaPool validation error: root JSON value must be an object");
    }

    if (root.contains("schema_version")) {
        if (!root.at("schema_version").is_string()) {
            throw std::runtime_error("MediaPool validation error: 'schema_version' must be a string");
        }
        const std::string ver = root.at("schema_version").get<std::string>();
        if (ver != kMediaPoolSchema10 && ver != kMediaPoolSchema11 && ver != kMediaPoolSchema12) {
            throw std::runtime_error(
                "MediaPool schema error: unsupported schema_version '" + ver +
                "' (supported versions: 1.0.0, 1.1.0, 1.2.0)");
        }
    }

    MediaPool pool;
    pool.bins_.clear();
    pool.assets_.clear();
    pool.clips_.clear();
    pool.root_bin_id_ = "root";

    bool has_root_bin_id = false;
    if (root.contains("root_bin_id")) {
        pool.root_bin_id_ = pool_require_non_empty_string(root, "root_bin_id", "media_pool");
        has_root_bin_id = true;
    }

    // 1. Parse and validate Assets
    if (root.contains("assets")) {
        const auto& assets_arr = root.at("assets");
        if (!assets_arr.is_array()) {
            throw std::runtime_error("MediaPool validation error: media_pool.assets must be an array");
        }
        for (size_t i = 0; i < assets_arr.size(); ++i) {
            const auto& a_obj = assets_arr[i];
            if (!a_obj.is_object()) {
                throw std::runtime_error("MediaPool validation error: media_pool.assets[" + std::to_string(i) + "] must be an object");
            }
            MediaAsset asset = MediaAsset::from_json(a_obj.dump());
            const std::string aid = asset.id();
            if (!pool.assets_.emplace(aid, std::move(asset)).second) {
                throw std::runtime_error("MediaPool validation error: duplicate asset id '" + aid + "'");
            }
        }
    }

    // 2. Parse and validate Clips (must reference existing Assets)
    if (root.contains("clips")) {
        const auto& clips_arr = root.at("clips");
        if (!clips_arr.is_array()) {
            throw std::runtime_error("MediaPool validation error: media_pool.clips must be an array");
        }
        for (size_t i = 0; i < clips_arr.size(); ++i) {
            const std::string c_ctx = "media_pool.clips[" + std::to_string(i) + "]";
            const auto& c_obj = clips_arr[i];
            if (!c_obj.is_object()) {
                throw std::runtime_error("MediaPool validation error: " + c_ctx + " must be an object");
            }

            std::string cid = pool_require_non_empty_string(c_obj, "id", c_ctx);
            if (pool.clips_.contains(cid)) {
                throw std::runtime_error("MediaPool validation error: duplicate clip id '" + cid + "'");
            }

            std::string aid = pool_require_non_empty_string(c_obj, "asset_id", c_ctx);
            const auto asset_it = pool.assets_.find(aid);
            if (asset_it == pool.assets_.end()) {
                throw std::runtime_error(
                    "MediaPool validation error: clip '" + cid + "' references missing asset_id '" + aid + "'");
            }

            const std::string fallback_name = asset_it->second.display_name().empty()
                ? std::string("Untitled Clip")
                : asset_it->second.display_name();
            std::string cname = pool_optional_string(c_obj, "name", fallback_name, c_ctx);
            if (cname.empty()) {
                cname = fallback_name;
            }

            std::string ctag = pool_optional_string(c_obj, "color_tag", "default", c_ctx);
            if (ctag.empty()) {
                ctag = "default";
            }

            const Rational s_in = pool_optional_rational(c_obj, "source_in", Rational{0, 1}, c_ctx);
            if (s_in.numerator() < 0) {
                throw std::runtime_error("MediaPool validation error: " + c_ctx + ".source_in cannot be negative");
            }

            const Rational default_out = (asset_it->second.duration() > s_in)
                ? asset_it->second.duration()
                : s_in;
            const Rational s_out = pool_optional_rational(c_obj, "source_out", default_out, c_ctx);
            if (s_out.numerator() < 0) {
                throw std::runtime_error("MediaPool validation error: " + c_ctx + ".source_out cannot be negative");
            }
            if (s_out < s_in) {
                throw std::runtime_error(
                    "MediaPool validation error: clip '" + cid + "' has source_out < source_in");
            }

            MediaClip clip(cid, std::move(aid), std::move(cname), s_in, s_out);
            clip.set_color_tag(std::move(ctag));
            pool.clips_.emplace(std::move(cid), std::move(clip));
        }
    }

    // 3. Parse and validate Bins + Hierarchy
    bool has_non_empty_bins = false;
    std::unordered_map<std::string, bool> bin_had_explicit_children;
    std::unordered_set<std::string> assigned_clips;

    if (root.contains("bins")) {
        const auto& bins_arr = root.at("bins");
        if (!bins_arr.is_array()) {
            throw std::runtime_error("MediaPool validation error: media_pool.bins must be an array");
        }
        has_non_empty_bins = !bins_arr.empty();

        for (size_t i = 0; i < bins_arr.size(); ++i) {
            const std::string b_ctx = "media_pool.bins[" + std::to_string(i) + "]";
            const auto& b_obj = bins_arr[i];
            if (!b_obj.is_object()) {
                throw std::runtime_error("MediaPool validation error: " + b_ctx + " must be an object");
            }

            std::string bid = pool_require_non_empty_string(b_obj, "id", b_ctx);
            if (pool.bins_.contains(bid)) {
                throw std::runtime_error("MediaPool validation error: duplicate bin id '" + bid + "'");
            }

            std::string default_bname = (bid == pool.root_bin_id_) ? std::string("Master Bin") : std::string("Untitled Bin");
            std::string bname = pool_optional_string(b_obj, "name", default_bname, b_ctx);
            std::string parent_id = pool_optional_string(b_obj, "parent_id", "", b_ctx);

            MediaBin bin(bid, std::move(bname), std::move(parent_id));

            if (b_obj.contains("clip_ids")) {
                const auto& cids_arr = b_obj.at("clip_ids");
                if (!cids_arr.is_array()) {
                    throw std::runtime_error("MediaPool validation error: " + b_ctx + ".clip_ids must be an array");
                }
                for (size_t j = 0; j < cids_arr.size(); ++j) {
                    if (!cids_arr[j].is_string()) {
                        throw std::runtime_error("MediaPool validation error: " + b_ctx + ".clip_ids entries must be strings");
                    }
                    const std::string cid = cids_arr[j].get<std::string>();
                    if (cid.empty() || !pool.clips_.contains(cid)) {
                        throw std::runtime_error(
                            "MediaPool validation error: bin '" + bid + "' references missing clip_id '" + cid + "'");
                    }
                    if (!assigned_clips.insert(cid).second) {
                        throw std::runtime_error(
                            "MediaPool validation error: duplicate bin membership for clip_id '" + cid + "'");
                    }
                    bin.add_clip(cid);
                }
            }

            bool explicit_children = false;
            if (b_obj.contains("child_bin_ids")) {
                explicit_children = true;
                const auto& cbids_arr = b_obj.at("child_bin_ids");
                if (!cbids_arr.is_array()) {
                    throw std::runtime_error("MediaPool validation error: " + b_ctx + ".child_bin_ids must be an array");
                }
                std::unordered_set<std::string> seen_children;
                for (size_t j = 0; j < cbids_arr.size(); ++j) {
                    if (!cbids_arr[j].is_string()) {
                        throw std::runtime_error("MediaPool validation error: " + b_ctx + ".child_bin_ids entries must be strings");
                    }
                    const std::string cbid = cbids_arr[j].get<std::string>();
                    if (cbid.empty()) {
                        throw std::runtime_error("MediaPool validation error: " + b_ctx + ".child_bin_ids contains empty string");
                    }
                    if (!seen_children.insert(cbid).second) {
                        throw std::runtime_error(
                            "MediaPool validation error: duplicate child_bin_id '" + cbid + "' in bin '" + bid + "'");
                    }
                    bin.add_child_bin(cbid);
                }
            }

            bin_had_explicit_children[bid] = explicit_children;
            pool.bins_.emplace(std::move(bid), std::move(bin));
        }
    }

    if (!has_non_empty_bins) {
        MediaBin root_bin(pool.root_bin_id_, "Master Bin", "");
        for (const std::string& cid : sorted_keys(pool.clips_)) {
            root_bin.add_clip(cid);
        }
        pool.bins_.emplace(pool.root_bin_id_, std::move(root_bin));
        return pool;
    }

    // Resolve and validate root_bin_id
    if (has_root_bin_id) {
        const auto root_it = pool.bins_.find(pool.root_bin_id_);
        if (root_it == pool.bins_.end()) {
            throw std::runtime_error(
                "MediaPool validation error: root_bin_id '" + pool.root_bin_id_ + "' not found in bins");
        }
        if (!root_it->second.parent_id().empty()) {
            throw std::runtime_error(
                "MediaPool validation error: root bin '" + pool.root_bin_id_ + "' must not specify parent_id");
        }
    } else {
        std::vector<std::string> root_candidates;
        for (const std::string& bid : sorted_keys(pool.bins_)) {
            if (pool.bins_.at(bid).parent_id().empty()) {
                root_candidates.push_back(bid);
            }
        }
        if (root_candidates.size() != 1) {
            throw std::runtime_error("MediaPool validation error: ambiguous or missing root bin");
        }
        pool.root_bin_id_ = root_candidates.front();
    }

    // Validate every bin's parent reference and check for cycles
    const std::vector<std::string> all_bin_ids = sorted_keys(pool.bins_);
    for (const std::string& bid : all_bin_ids) {
        const MediaBin& bin = pool.bins_.at(bid);
        if (bid != pool.root_bin_id_) {
            if (bin.parent_id().empty()) {
                throw std::runtime_error(
                    "MediaPool validation error: non-root bin '" + bid + "' missing parent_id");
            }
            if (!pool.bins_.contains(bin.parent_id())) {
                throw std::runtime_error(
                    "MediaPool validation error: bin '" + bid + "' references missing parent_id '" + bin.parent_id() + "'");
            }
        }

        std::unordered_set<std::string> visited;
        std::string curr = bid;
        while (curr != pool.root_bin_id_) {
            if (!visited.insert(curr).second) {
                throw std::runtime_error(
                    "MediaPool validation error: cycle detected in bin hierarchy at '" + curr + "'");
            }
            const auto curr_it = pool.bins_.find(curr);
            if (curr_it == pool.bins_.end() || curr_it->second.parent_id().empty()) {
                throw std::runtime_error(
                    "MediaPool validation error: disconnected bin '" + bid + "'");
            }
            curr = curr_it->second.parent_id();
        }
    }

    // Validate explicit child_bin_ids and reconcile missing child_bin_ids deterministically
    std::unordered_map<std::string, std::string> child_to_claimed_parent;
    for (const std::string& bid : all_bin_ids) {
        if (!bin_had_explicit_children[bid]) {
            continue;
        }
        const MediaBin& bin = pool.bins_.at(bid);
        for (const std::string& cbid : bin.child_bin_ids()) {
            const auto c_it = pool.bins_.find(cbid);
            if (c_it == pool.bins_.end()) {
                throw std::runtime_error(
                    "MediaPool validation error: bin '" + bid + "' references missing child_bin_id '" + cbid + "'");
            }
            if (cbid == bid || c_it->second.parent_id() != bid) {
                throw std::runtime_error(
                    "MediaPool validation error: bin '" + bid + "' lists child '" + cbid +
                    "' whose parent_id does not match");
            }
            if (!child_to_claimed_parent.emplace(cbid, bid).second) {
                throw std::runtime_error(
                    "MediaPool validation error: child bin '" + cbid + "' listed under multiple parents");
            }
        }
    }

    for (const std::string& bid : all_bin_ids) {
        if (bid == pool.root_bin_id_) {
            continue;
        }
        const std::string& parent_id = pool.bins_.at(bid).parent_id();
        if (bin_had_explicit_children[parent_id]) {
            const auto& parent_children = pool.bins_.at(parent_id).child_bin_ids();
            if (std::find(parent_children.begin(), parent_children.end(), bid) == parent_children.end()) {
                throw std::runtime_error(
                    "MediaPool validation error: bin '" + bid + "' declares parent '" + parent_id +
                    "' which omits it from child_bin_ids");
            }
        } else {
            pool.bins_.at(parent_id).add_child_bin(bid);
        }
    }

    // Place any unbinned valid clips (e.g., when clip_ids was omitted on a minimal fixture) into root bin
    for (const std::string& cid : sorted_keys(pool.clips_)) {
        if (!assigned_clips.contains(cid)) {
            pool.bins_.at(pool.root_bin_id_).add_clip(cid);
        }
    }

    return pool;
}

} // namespace ave
