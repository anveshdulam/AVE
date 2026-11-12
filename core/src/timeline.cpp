#include "ave/timeline.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace ave {

std::string to_string(TrackType type) {
    switch (type) {
        case TrackType::Video: return "video";
        case TrackType::Audio: return "audio";
        default: return "unknown";
    }
}

TrackType track_type_from_string(std::string_view str) {
    if (str == "video") return TrackType::Video;
    if (str == "audio") return TrackType::Audio;
    return TrackType::Video;
}

// --- TimelineItem Implementation ---

TimelineItem::TimelineItem(std::string id,
                           std::string clip_id,
                           std::string asset_id,
                           std::string name,
                           Rational timeline_in,
                           Rational duration,
                           Rational source_in,
                           Rational speed)
    : id_(std::move(id)),
      clip_id_(std::move(clip_id)),
      asset_id_(std::move(asset_id)),
      name_(std::move(name)),
      timeline_in_(timeline_in),
      duration_(duration),
      source_in_(source_in),
      speed_(speed) {
    if (duration_.numerator() <= 0) {
        throw std::invalid_argument("TimelineItem duration must be positive");
    }
    if (speed_.numerator() <= 0) {
        throw std::invalid_argument("TimelineItem speed must be positive");
    }
}

void TimelineItem::set_duration(Rational dur) {
    if (dur.numerator() <= 0) {
        throw std::invalid_argument("TimelineItem duration must be positive");
    }
    duration_ = dur;
}

void TimelineItem::set_speed(Rational spd) {
    if (spd.numerator() <= 0) {
        throw std::invalid_argument("TimelineItem speed must be positive");
    }
    speed_ = spd;
}

Rational TimelineItem::source_time_at(Rational timeline_time) const {
    if (timeline_time < timeline_in_) {
        return source_in_;
    }
    Rational offset = timeline_time - timeline_in_;
    return source_in_ + (offset * speed_);
}

// --- Track Implementation ---

Track::Track(std::string id, std::string name, TrackType type, int index)
    : id_(std::move(id)),
      name_(std::move(name)),
      type_(type),
      index_(index) {}

void Track::sort_items() {
    std::sort(items_.begin(), items_.end(), [](const TimelineItem& a, const TimelineItem& b) {
        return a.timeline_in() < b.timeline_in();
    });
}

bool Track::add_item(TimelineItem item) {
    if (item.duration().numerator() <= 0) return false;

    // Verify no overlap on the same track
    Rational new_in = item.timeline_in();
    Rational new_out = item.timeline_out();

    for (const auto& existing : items_) {
        Rational ex_in = existing.timeline_in();
        Rational ex_out = existing.timeline_out();

        // Check intersection: max(in1, in2) < min(out1, out2)
        Rational max_in = (new_in > ex_in) ? new_in : ex_in;
        Rational min_out = (new_out < ex_out) ? new_out : ex_out;
        if (max_in < min_out) {
            return false; // Overlap detected on same track
        }
    }

    items_.push_back(std::move(item));
    sort_items();
    return true;
}

bool Track::remove_item(const std::string& item_id) {
    auto it = std::find_if(items_.begin(), items_.end(), [&](const TimelineItem& item) {
        return item.id() == item_id;
    });
    if (it != items_.end()) {
        items_.erase(it);
        return true;
    }
    return false;
}

const TimelineItem* Track::item_at(Rational time) const noexcept {
    for (const auto& item : items_) {
        if (item.contains_time(time)) {
            return &item;
        }
    }
    return nullptr;
}

TimelineItem* Track::item_at_mut(Rational time) noexcept {
    for (auto& item : items_) {
        if (item.contains_time(time)) {
            return &item;
        }
    }
    return nullptr;
}

const TimelineItem* Track::find_item(const std::string& item_id) const noexcept {
    for (const auto& item : items_) {
        if (item.id() == item_id) {
            return &item;
        }
    }
    return nullptr;
}

TimelineItem* Track::find_item_mut(const std::string& item_id) noexcept {
    for (auto& item : items_) {
        if (item.id() == item_id) {
            return &item;
        }
    }
    return nullptr;
}

Rational Track::duration() const noexcept {
    Rational max_out{0, 1};
    for (const auto& item : items_) {
        if (item.timeline_out() > max_out) {
            max_out = item.timeline_out();
        }
    }
    return max_out;
}

// --- Timeline Implementation ---

Timeline::Timeline()
    : Timeline("timeline-" + UUID::generate(), "Master Sequence", Rational{24, 1}) {}

Timeline::Timeline(std::string id, std::string name, Rational frame_rate,
                   int width, int height, int sample_rate)
    : id_(std::move(id)),
      name_(std::move(name)),
      frame_rate_(frame_rate),
      width_(width),
      height_(height),
      sample_rate_(sample_rate) {
    // Standard default template: V1 and A1 tracks
    add_video_track("V1");
    add_audio_track("A1");
}

Track& Timeline::add_video_track(std::string name) {
    int idx = static_cast<int>(video_tracks_.size());
    std::string t_name = name.empty() ? ("V" + std::to_string(idx + 1)) : std::move(name);
    std::string t_id = "track-v-" + UUID::generate();
    video_tracks_.emplace_back(t_id, std::move(t_name), TrackType::Video, idx);
    return video_tracks_.back();
}

Track& Timeline::add_audio_track(std::string name) {
    int idx = static_cast<int>(audio_tracks_.size());
    std::string t_name = name.empty() ? ("A" + std::to_string(idx + 1)) : std::move(name);
    std::string t_id = "track-a-" + UUID::generate();
    audio_tracks_.emplace_back(t_id, std::move(t_name), TrackType::Audio, idx);
    return audio_tracks_.back();
}

const Track* Timeline::find_track(const std::string& track_id) const noexcept {
    for (const auto& t : video_tracks_) {
        if (t.id() == track_id) return &t;
    }
    for (const auto& t : audio_tracks_) {
        if (t.id() == track_id) return &t;
    }
    return nullptr;
}

Track* Timeline::find_track_mut(const std::string& track_id) noexcept {
    for (auto& t : video_tracks_) {
        if (t.id() == track_id) return &t;
    }
    for (auto& t : audio_tracks_) {
        if (t.id() == track_id) return &t;
    }
    return nullptr;
}

const Track* Timeline::find_track_for_item(const std::string& item_id) const noexcept {
    for (const auto& t : video_tracks_) {
        if (t.find_item(item_id)) return &t;
    }
    for (const auto& t : audio_tracks_) {
        if (t.find_item(item_id)) return &t;
    }
    return nullptr;
}

Track* Timeline::find_track_for_item_mut(const std::string& item_id) noexcept {
    for (auto& t : video_tracks_) {
        if (t.find_item(item_id)) return &t;
    }
    for (auto& t : audio_tracks_) {
        if (t.find_item(item_id)) return &t;
    }
    return nullptr;
}

const TimelineItem* Timeline::find_item(const std::string& item_id) const noexcept {
    const Track* t = find_track_for_item(item_id);
    return t ? t->find_item(item_id) : nullptr;
}

TimelineItem* Timeline::find_item_mut(const std::string& item_id) noexcept {
    Track* t = find_track_for_item_mut(item_id);
    return t ? t->find_item_mut(item_id) : nullptr;
}

Rational Timeline::duration() const noexcept {
    Rational max_dur{0, 1};
    for (const auto& t : video_tracks_) {
        Rational d = t.duration();
        if (d > max_dur) max_dur = d;
    }
    for (const auto& t : audio_tracks_) {
        Rational d = t.duration();
        if (d > max_dur) max_dur = d;
    }
    return max_dur;
}

std::vector<const TimelineItem*> Timeline::active_items_at(Rational time) const {
    std::vector<const TimelineItem*> result;
    for (const auto& t : video_tracks_) {
        if (!t.muted()) {
            const auto* item = t.item_at(time);
            if (item && item->enabled()) {
                result.push_back(item);
            }
        }
    }
    for (const auto& t : audio_tracks_) {
        if (!t.muted()) {
            const auto* item = t.item_at(time);
            if (item && item->enabled()) {
                result.push_back(item);
            }
        }
    }
    return result;
}

std::pair<std::string, std::string> Timeline::split_item(const std::string& track_id,
                                                         const std::string& item_id,
                                                         Rational split_time) {
    Track* track = find_track_mut(track_id);
    if (!track) {
        throw std::invalid_argument("Track not found: " + track_id);
    }
    const TimelineItem* orig = track->find_item(item_id);
    if (!orig) {
        throw std::invalid_argument("Item not found on track: " + item_id);
    }

    if (split_time <= orig->timeline_in() || split_time >= orig->timeline_out()) {
        throw std::invalid_argument("Split time must be strictly inside clip bounds");
    }

    // Left slice
    Rational left_duration = split_time - orig->timeline_in();
    TimelineItem left_item(
        UUID::generate(),
        orig->clip_id(),
        orig->asset_id(),
        orig->name(),
        orig->timeline_in(),
        left_duration,
        orig->source_in(),
        orig->speed()
    );

    // Right slice
    Rational right_timeline_in = split_time;
    Rational right_duration = orig->timeline_out() - split_time;
    Rational right_source_in = orig->source_time_at(split_time);
    TimelineItem right_item(
        UUID::generate(),
        orig->clip_id(),
        orig->asset_id(),
        orig->name(),
        right_timeline_in,
        right_duration,
        right_source_in,
        orig->speed()
    );

    auto copy_attributes = [](const TimelineItem& src, TimelineItem& dst) {
        dst.set_enabled(src.enabled());
        dst.set_transform(src.transform_x(), src.transform_y());
        dst.set_scale(src.scale_x(), src.scale_y());
        dst.set_rotation(src.rotation());
        dst.set_anchor(src.anchor_x(), src.anchor_y());
        dst.set_opacity(src.opacity());
        dst.set_blend_mode(src.blend_mode());
    };
    copy_attributes(*orig, left_item);
    copy_attributes(*orig, right_item);

    std::string left_id = left_item.id();
    std::string right_id = right_item.id();

    track->remove_item(item_id);
    track->add_item(std::move(left_item));
    track->add_item(std::move(right_item));

    return {left_id, right_id};
}

namespace {

constexpr const char* kTimelineLegacySchema10 = "1.0.0";
constexpr const char* kTimelineCurrentSchema11 = "1.1.0";

nlohmann::json rational_to_json(Rational r) {
    return nlohmann::json{{"num", r.numerator()}, {"den", r.denominator()}};
}

std::string require_string(const nlohmann::json& obj, const char* key, const std::string& ctx, bool allow_empty = true) {
    if (!obj.contains(key) || !obj.at(key).is_string()) {
        throw std::runtime_error("Timeline validation error: " + ctx + " missing or invalid string '" + key + "'");
    }
    std::string val = obj.at(key).get<std::string>();
    if (!allow_empty && val.empty()) {
        throw std::runtime_error("Timeline validation error: " + ctx + "." + key + " must not be empty");
    }
    return val;
}

std::string optional_string(const nlohmann::json& obj, const char* key, const std::string& default_val, const std::string& ctx) {
    if (!obj.contains(key)) {
        return default_val;
    }
    if (!obj.at(key).is_string()) {
        throw std::runtime_error("Timeline validation error: " + ctx + "." + key + " must be a string");
    }
    return obj.at(key).get<std::string>();
}

bool optional_bool(const nlohmann::json& obj, const char* key, bool default_val, const std::string& ctx) {
    if (!obj.contains(key)) {
        return default_val;
    }
    if (!obj.at(key).is_boolean()) {
        throw std::runtime_error("Timeline validation error: " + ctx + "." + key + " must be a boolean");
    }
    return obj.at(key).get<bool>();
}

int optional_int(const nlohmann::json& obj, const char* key, int default_val, const std::string& ctx) {
    if (!obj.contains(key)) {
        return default_val;
    }
    const auto& v = obj.at(key);
    if (!v.is_number_integer()) {
        throw std::runtime_error("Timeline validation error: " + ctx + "." + key + " must be an integer");
    }
    const int64_t val = v.get<int64_t>();
    if (val < std::numeric_limits<int>::min() || val > std::numeric_limits<int>::max()) {
        throw std::runtime_error("Timeline validation error: " + ctx + "." + key + " out of int range");
    }
    return static_cast<int>(val);
}

float optional_float(const nlohmann::json& obj, const char* key, float default_val, const std::string& ctx) {
    if (!obj.contains(key)) {
        return default_val;
    }
    const auto& v = obj.at(key);
    if (!v.is_number()) {
        throw std::runtime_error("Timeline validation error: " + ctx + "." + key + " must be a number");
    }
    const double val = v.get<double>();
    if (!std::isfinite(val)) {
        throw std::runtime_error("Timeline validation error: " + ctx + "." + key + " must be finite");
    }
    return static_cast<float>(val);
}

Rational parse_rational(const nlohmann::json& obj,
                        const char* key,
                        const std::string& ctx,
                        bool required,
                        Rational default_val = Rational{0, 1}) {
    if (!obj.contains(key)) {
        if (required) {
            throw std::runtime_error("Timeline validation error: " + ctx + " missing required rational '" + key + "'");
        }
        return default_val;
    }
    const auto& r = obj.at(key);
    if (!r.is_object() || !r.contains("num") || !r.contains("den")) {
        throw std::runtime_error("Timeline validation error: " + ctx + "." + key + " must be an object with 'num' and 'den'");
    }
    if (!r.at("num").is_number_integer() || !r.at("den").is_number_integer()) {
        throw std::runtime_error("Timeline validation error: " + ctx + "." + key + " 'num' and 'den' must be integers");
    }
    const int64_t num = r.at("num").get<int64_t>();
    const int64_t den = r.at("den").get<int64_t>();
    if (den <= 0) {
        throw std::runtime_error("Timeline validation error: " + ctx + "." + key + " denominator must be > 0");
    }
    return Rational{num, den};
}

} // namespace

std::string Timeline::to_json() const {
    auto serialize_tracks = [](const std::vector<Track>& tracks) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& t : tracks) {
            nlohmann::json items_arr = nlohmann::json::array();
            for (const auto& it : t.items()) {
                items_arr.push_back(nlohmann::json{
                    {"id", it.id()},
                    {"clip_id", it.clip_id()},
                    {"asset_id", it.asset_id()},
                    {"name", it.name()},
                    {"timeline_in", rational_to_json(it.timeline_in())},
                    {"duration", rational_to_json(it.duration())},
                    {"source_in", rational_to_json(it.source_in())},
                    {"speed", rational_to_json(it.speed())},
                    {"enabled", it.enabled()},
                    {"transform_x", it.transform_x()},
                    {"transform_y", it.transform_y()},
                    {"scale_x", it.scale_x()},
                    {"scale_y", it.scale_y()},
                    {"rotation", it.rotation()},
                    {"anchor_x", it.anchor_x()},
                    {"anchor_y", it.anchor_y()},
                    {"opacity", it.opacity()},
                    {"blend_mode", it.blend_mode()}
                });
            }
            arr.push_back(nlohmann::json{
                {"id", t.id()},
                {"name", t.name()},
                {"type", to_string(t.type())},
                {"index", t.index()},
                {"muted", t.muted()},
                {"soloed", t.soloed()},
                {"locked", t.locked()},
                {"items", std::move(items_arr)}
            });
        }
        return arr;
    };

    nlohmann::json root{
        {"schema_version", kTimelineCurrentSchema11},
        {"id", id_},
        {"name", name_},
        {"frame_rate", rational_to_json(frame_rate_)},
        {"width", width_},
        {"height", height_},
        {"sample_rate", sample_rate_},
        {"duration", rational_to_json(duration())},
        {"video_tracks", serialize_tracks(video_tracks_)},
        {"audio_tracks", serialize_tracks(audio_tracks_)}
    };
    return root.dump(2);
}

Timeline Timeline::from_json(const std::string& json_text) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(std::string("Timeline JSON parse error: ") + e.what());
    }

    if (!root.is_object()) {
        throw std::runtime_error("Timeline validation error: root JSON value must be an object");
    }

    if (root.contains("schema_version")) {
        if (!root.at("schema_version").is_string()) {
            throw std::runtime_error("Timeline validation error: 'schema_version' must be a string");
        }
        const std::string ver = root.at("schema_version").get<std::string>();
        if (ver != kTimelineLegacySchema10 && ver != kTimelineCurrentSchema11) {
            throw std::runtime_error(
                "Timeline schema error: unsupported schema_version '" + ver +
                "' (supported versions: 1.0.0, 1.1.0)");
        }
    }

    std::string id = optional_string(root, "id", "", "timeline");
    if (id.empty()) {
        id = "timeline-" + UUID::generate();
    }
    std::string name = optional_string(root, "name", "Master Sequence", "timeline");
    Rational frame_rate = parse_rational(root, "frame_rate", "timeline", true);
    if (frame_rate.numerator() <= 0) {
        throw std::runtime_error("Timeline validation error: timeline.frame_rate must be positive");
    }

    int width = optional_int(root, "width", 1920, "timeline");
    int height = optional_int(root, "height", 1080, "timeline");
    int sample_rate = optional_int(root, "sample_rate", 48000, "timeline");
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Timeline validation error: timeline width and height must be positive");
    }
    if (sample_rate <= 0) {
        throw std::runtime_error("Timeline validation error: timeline sample_rate must be positive");
    }

    if (root.contains("duration")) {
        Rational seq_dur = parse_rational(root, "duration", "timeline", false);
        if (seq_dur.numerator() < 0) {
            throw std::runtime_error("Timeline validation error: timeline.duration cannot be negative");
        }
    }

    Timeline tl(std::move(id), std::move(name), frame_rate, width, height, sample_rate);
    tl.video_tracks_.clear();
    tl.audio_tracks_.clear();

    auto deserialize_tracks = [&](const char* array_name, bool is_video, std::vector<Track>& out_tracks) {
        if (!root.contains(array_name)) {
            return;
        }
        const auto& arr = root.at(array_name);
        if (!arr.is_array()) {
            throw std::runtime_error(std::string("Timeline validation error: '") + array_name + "' must be an array");
        }

        const TrackType expected_type = is_video ? TrackType::Video : TrackType::Audio;
        const std::string expected_type_str = to_string(expected_type);

        for (size_t t_idx = 0; t_idx < arr.size(); ++t_idx) {
            const std::string trk_ctx = std::string(array_name) + "[" + std::to_string(t_idx) + "]";
            const auto& t_obj = arr[t_idx];
            if (!t_obj.is_object()) {
                throw std::runtime_error("Timeline validation error: " + trk_ctx + " must be an object");
            }

            std::string t_id = require_string(t_obj, "id", trk_ctx, false);
            const std::string default_name = (is_video ? "V" : "A") + std::to_string(t_idx + 1);
            std::string t_name = optional_string(t_obj, "name", default_name, trk_ctx);

            std::string t_type_str = optional_string(t_obj, "type", expected_type_str, trk_ctx);
            if (t_type_str != "video" && t_type_str != "audio") {
                throw std::runtime_error("Timeline validation error: " + trk_ctx + ".type invalid ('" + t_type_str + "')");
            }
            if (t_type_str != expected_type_str) {
                throw std::runtime_error(
                    "Timeline validation error: " + trk_ctx + ".type '" + t_type_str +
                    "' does not match container '" + array_name + "'");
            }

            int t_index = optional_int(t_obj, "index", static_cast<int>(t_idx), trk_ctx);
            if (t_index < 0) {
                throw std::runtime_error("Timeline validation error: " + trk_ctx + ".index must be >= 0");
            }

            Track track(std::move(t_id), std::move(t_name), expected_type, t_index);
            track.set_muted(optional_bool(t_obj, "muted", false, trk_ctx));
            track.set_soloed(optional_bool(t_obj, "soloed", false, trk_ctx));
            track.set_locked(optional_bool(t_obj, "locked", false, trk_ctx));

            if (t_obj.contains("items")) {
                const auto& items_arr = t_obj.at("items");
                if (!items_arr.is_array()) {
                    throw std::runtime_error("Timeline validation error: " + trk_ctx + ".items must be an array");
                }
                for (size_t i_idx = 0; i_idx < items_arr.size(); ++i_idx) {
                    const std::string it_ctx = trk_ctx + ".items[" + std::to_string(i_idx) + "]";
                    const auto& it_obj = items_arr[i_idx];
                    if (!it_obj.is_object()) {
                        throw std::runtime_error("Timeline validation error: " + it_ctx + " must be an object");
                    }

                    std::string it_id = require_string(it_obj, "id", it_ctx, false);
                    std::string it_clip_id = optional_string(it_obj, "clip_id", "", it_ctx);
                    std::string it_asset_id = optional_string(it_obj, "asset_id", "", it_ctx);
                    std::string it_name = optional_string(it_obj, "name", "Untitled Item", it_ctx);

                    Rational it_tin = parse_rational(it_obj, "timeline_in", it_ctx, true);
                    if (it_tin < Rational{0, 1}) {
                        throw std::runtime_error("Timeline validation error: " + it_ctx + ".timeline_in must be >= 0");
                    }
                    Rational it_dur = parse_rational(it_obj, "duration", it_ctx, true);
                    if (it_dur.numerator() <= 0) {
                        throw std::runtime_error("Timeline validation error: " + it_ctx + ".duration must be > 0");
                    }
                    Rational it_sin = parse_rational(it_obj, "source_in", it_ctx, true);
                    Rational it_spd = parse_rational(it_obj, "speed", it_ctx, false, Rational{1, 1});
                    if (it_spd.numerator() <= 0) {
                        throw std::runtime_error("Timeline validation error: " + it_ctx + ".speed must be > 0");
                    }

                    TimelineItem item(std::move(it_id), std::move(it_clip_id), std::move(it_asset_id),
                                      std::move(it_name), it_tin, it_dur, it_sin, it_spd);
                    item.set_enabled(optional_bool(it_obj, "enabled", true, it_ctx));
                    item.set_transform(optional_float(it_obj, "transform_x", 0.0f, it_ctx),
                                       optional_float(it_obj, "transform_y", 0.0f, it_ctx));
                    item.set_scale(optional_float(it_obj, "scale_x", 1.0f, it_ctx),
                                   optional_float(it_obj, "scale_y", 1.0f, it_ctx));
                    item.set_rotation(optional_float(it_obj, "rotation", 0.0f, it_ctx));
                    item.set_anchor(optional_float(it_obj, "anchor_x", 0.5f, it_ctx),
                                    optional_float(it_obj, "anchor_y", 0.5f, it_ctx));

                    const float opacity = optional_float(it_obj, "opacity", 1.0f, it_ctx);
                    if (opacity < 0.0f || opacity > 1.0f) {
                        throw std::runtime_error("Timeline validation error: " + it_ctx + ".opacity must be in [0.0, 1.0]");
                    }
                    item.set_opacity(opacity);

                    std::string bm = optional_string(it_obj, "blend_mode", "Over", it_ctx);
                    item.set_blend_mode(bm.empty() ? "Over" : std::move(bm));

                    if (!track.add_item(std::move(item))) {
                        throw std::runtime_error("Timeline validation error: " + it_ctx + " overlaps existing item on track");
                    }
                }
            }

            out_tracks.push_back(std::move(track));
        }
    };

    deserialize_tracks("video_tracks", true, tl.video_tracks_);
    deserialize_tracks("audio_tracks", false, tl.audio_tracks_);

    return tl;
}

} // namespace ave
