#include "ave/media_pool.hpp"

#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <cctype>

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

namespace {

std::string pool_extract_string(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }
    if (pos >= json.length() || json[pos] != '"') return "";
    pos++;
    std::string result;
    while (pos < json.length()) {
        if (json[pos] == '\\' && pos + 1 < json.length()) {
            pos++;
            switch (json[pos]) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                default: result += json[pos]; break;
            }
        } else if (json[pos] == '"') {
            break;
        } else {
            result += json[pos];
        }
        pos++;
    }
    return result;
}

int64_t pool_extract_int(const std::string& json, const std::string& key, int64_t default_val = 0) {
    std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return default_val;
    pos += needle.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }
    if (pos >= json.length()) return default_val;
    size_t end_pos = pos;
    if (json[end_pos] == '-' || json[end_pos] == '+') end_pos++;
    while (end_pos < json.length() && std::isdigit(static_cast<unsigned char>(json[end_pos]))) {
        end_pos++;
    }
    if (end_pos == pos) return default_val;
    try {
        return std::stoll(json.substr(pos, end_pos - pos));
    } catch (...) {
        return default_val;
    }
}

Rational pool_extract_rational(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return Rational{0, 1};
    size_t obj_start = json.find('{', pos);
    size_t obj_end = json.find('}', obj_start);
    if (obj_start == std::string::npos || obj_end == std::string::npos) return Rational{0, 1};
    std::string obj_sub = json.substr(obj_start, obj_end - obj_start + 1);
    int64_t num = pool_extract_int(obj_sub, "num", 0);
    int64_t den = pool_extract_int(obj_sub, "den", 1);
    return Rational{num, den};
}

} // namespace

MediaPool MediaPool::from_json(const std::string& json_text) {
    MediaPool pool;
    pool.bins_.clear();
    pool.assets_.clear();
    pool.clips_.clear();

    std::string root_id = pool_extract_string(json_text, "root_bin_id");
    if (!root_id.empty()) {
        pool.root_bin_id_ = root_id;
    }

    // 1. Parse Assets
    size_t assets_pos = json_text.find("\"assets\":");
    if (assets_pos != std::string::npos) {
        size_t arr_start = json_text.find('[', assets_pos);
        if (arr_start != std::string::npos) {
            size_t cursor = arr_start + 1;
            while (cursor < json_text.length()) {
                size_t obj_start = json_text.find('{', cursor);
                if (obj_start == std::string::npos) break;

                // Match braces
                int brace_count = 0;
                size_t obj_end = obj_start;
                for (; obj_end < json_text.length(); ++obj_end) {
                    if (json_text[obj_end] == '{') brace_count++;
                    else if (json_text[obj_end] == '}') {
                        brace_count--;
                        if (brace_count == 0) break;
                    }
                }
                if (brace_count != 0) break;

                std::string asset_json = json_text.substr(obj_start, obj_end - obj_start + 1);
                try {
                    MediaAsset asset = MediaAsset::from_json(asset_json);
                    pool.assets_[asset.id()] = std::move(asset);
                } catch (...) {}

                cursor = obj_end + 1;
                size_t next_bracket = json_text.find(']', cursor);
                size_t next_brace = json_text.find('{', cursor);
                if (next_bracket != std::string::npos && (next_brace == std::string::npos || next_bracket < next_brace)) {
                    break;
                }
            }
        }
    }

    // 2. Parse Clips
    size_t clips_pos = json_text.find("\"clips\":");
    if (clips_pos != std::string::npos) {
        size_t arr_start = json_text.find('[', clips_pos);
        if (arr_start != std::string::npos) {
            size_t cursor = arr_start + 1;
            while (cursor < json_text.length()) {
                size_t obj_start = json_text.find('{', cursor);
                if (obj_start == std::string::npos) break;

                int brace_count = 0;
                size_t obj_end = obj_start;
                for (; obj_end < json_text.length(); ++obj_end) {
                    if (json_text[obj_end] == '{') brace_count++;
                    else if (json_text[obj_end] == '}') {
                        brace_count--;
                        if (brace_count == 0) break;
                    }
                }
                if (brace_count != 0) break;

                std::string clip_json = json_text.substr(obj_start, obj_end - obj_start + 1);
                std::string cid = pool_extract_string(clip_json, "id");
                std::string aid = pool_extract_string(clip_json, "asset_id");
                std::string name = pool_extract_string(clip_json, "name");
                std::string color = pool_extract_string(clip_json, "color_tag");
                Rational s_in = pool_extract_rational(clip_json, "source_in");
                Rational s_out = pool_extract_rational(clip_json, "source_out");

                if (!cid.empty() && s_out >= s_in) {
                    MediaClip clip(cid, aid, name, s_in, s_out);
                    if (!color.empty()) clip.set_color_tag(color);
                    pool.clips_[cid] = std::move(clip);
                }

                cursor = obj_end + 1;
                size_t next_bracket = json_text.find(']', cursor);
                size_t next_brace = json_text.find('{', cursor);
                if (next_bracket != std::string::npos && (next_brace == std::string::npos || next_bracket < next_brace)) {
                    break;
                }
            }
        }
    }

    // 3. Parse Bins
    size_t bins_pos = json_text.find("\"bins\":");
    if (bins_pos != std::string::npos) {
        size_t arr_start = json_text.find('[', bins_pos);
        if (arr_start != std::string::npos) {
            size_t cursor = arr_start + 1;
            while (cursor < json_text.length()) {
                size_t obj_start = json_text.find('{', cursor);
                if (obj_start == std::string::npos) break;

                int brace_count = 0;
                size_t obj_end = obj_start;
                for (; obj_end < json_text.length(); ++obj_end) {
                    if (json_text[obj_end] == '{') brace_count++;
                    else if (json_text[obj_end] == '}') {
                        brace_count--;
                        if (brace_count == 0) break;
                    }
                }
                if (brace_count != 0) break;

                std::string bin_json = json_text.substr(obj_start, obj_end - obj_start + 1);
                std::string bid = pool_extract_string(bin_json, "id");
                std::string bname = pool_extract_string(bin_json, "name");
                std::string parent = pool_extract_string(bin_json, "parent_id");

                if (!bid.empty()) {
                    MediaBin bin(bid, bname, parent);

                    // Parse clip_ids
                    size_t c_pos = bin_json.find("\"clip_ids\":");
                    if (c_pos != std::string::npos) {
                        size_t c_arr_start = bin_json.find('[', c_pos);
                        size_t c_arr_end = bin_json.find(']', c_arr_start);
                        if (c_arr_start != std::string::npos && c_arr_end != std::string::npos) {
                            size_t c_cur = c_arr_start;
                            while (c_cur < c_arr_end) {
                                size_t q1 = bin_json.find('"', c_cur);
                                if (q1 == std::string::npos || q1 >= c_arr_end) break;
                                size_t q2 = bin_json.find('"', q1 + 1);
                                if (q2 == std::string::npos || q2 > c_arr_end) break;
                                bin.add_clip(bin_json.substr(q1 + 1, q2 - q1 - 1));
                                c_cur = q2 + 1;
                            }
                        }
                    }

                    // Parse child_bin_ids
                    size_t b_pos = bin_json.find("\"child_bin_ids\":");
                    if (b_pos != std::string::npos) {
                        size_t b_arr_start = bin_json.find('[', b_pos);
                        size_t b_arr_end = bin_json.find(']', b_arr_start);
                        if (b_arr_start != std::string::npos && b_arr_end != std::string::npos) {
                            size_t b_cur = b_arr_start;
                            while (b_cur < b_arr_end) {
                                size_t q1 = bin_json.find('"', b_cur);
                                if (q1 == std::string::npos || q1 >= b_arr_end) break;
                                size_t q2 = bin_json.find('"', q1 + 1);
                                if (q2 == std::string::npos || q2 > b_arr_end) break;
                                bin.add_child_bin(bin_json.substr(q1 + 1, q2 - q1 - 1));
                                b_cur = q2 + 1;
                            }
                        }
                    }

                    pool.bins_[bid] = std::move(bin);
                }

                cursor = obj_end + 1;
                size_t next_bracket = json_text.find(']', cursor);
                size_t next_brace = json_text.find('{', cursor);
                if (next_bracket != std::string::npos && (next_brace == std::string::npos || next_bracket < next_brace)) {
                    break;
                }
            }
        }
    }

    if (pool.bins_.find(pool.root_bin_id_) == pool.bins_.end()) {
        pool.bins_[pool.root_bin_id_] = MediaBin(pool.root_bin_id_, "Master Bin", "");
    }

    return pool;
}

} // namespace ave
