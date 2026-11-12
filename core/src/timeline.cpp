#include "ave/timeline.hpp"

#include <sstream>
#include <stdexcept>
#include <algorithm>

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

std::string Timeline::to_json() const {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"id\": \"" << id_ << "\",\n";
    ss << "  \"name\": \"" << name_ << "\",\n";
    ss << "  \"frame_rate\": {\"num\": " << frame_rate_.numerator() << ", \"den\": " << frame_rate_.denominator() << "},\n";
    ss << "  \"width\": " << width_ << ",\n";
    ss << "  \"height\": " << height_ << ",\n";
    ss << "  \"sample_rate\": " << sample_rate_ << ",\n";

    auto serialize_tracks = [&](const std::vector<Track>& tracks, const char* array_name) {
        ss << "  \"" << array_name << "\": [\n";
        for (size_t t_i = 0; t_i < tracks.size(); ++t_i) {
            const auto& t = tracks[t_i];
            ss << "    {\n";
            ss << "      \"id\": \"" << t.id() << "\",\n";
            ss << "      \"name\": \"" << t.name() << "\",\n";
            ss << "      \"type\": \"" << to_string(t.type()) << "\",\n";
            ss << "      \"index\": " << t.index() << ",\n";
            ss << "      \"muted\": " << (t.muted() ? "true" : "false") << ",\n";
            ss << "      \"items\": [\n";
            for (size_t i = 0; i < t.items().size(); ++i) {
                const auto& it = t.items()[i];
                ss << "        {\n";
                ss << "          \"id\": \"" << it.id() << "\",\n";
                ss << "          \"clip_id\": \"" << it.clip_id() << "\",\n";
                ss << "          \"asset_id\": \"" << it.asset_id() << "\",\n";
                ss << "          \"name\": \"" << it.name() << "\",\n";
                ss << "          \"timeline_in\": {\"num\": " << it.timeline_in().numerator() << ", \"den\": " << it.timeline_in().denominator() << "},\n";
                ss << "          \"duration\": {\"num\": " << it.duration().numerator() << ", \"den\": " << it.duration().denominator() << "},\n";
                ss << "          \"source_in\": {\"num\": " << it.source_in().numerator() << ", \"den\": " << it.source_in().denominator() << "},\n";
                ss << "          \"speed\": {\"num\": " << it.speed().numerator() << ", \"den\": " << it.speed().denominator() << "},\n";
                ss << "          \"transform_x\": " << it.transform_x() << ",\n";
                ss << "          \"transform_y\": " << it.transform_y() << ",\n";
                ss << "          \"scale_x\": " << it.scale_x() << ",\n";
                ss << "          \"scale_y\": " << it.scale_y() << ",\n";
                ss << "          \"rotation\": " << it.rotation() << ",\n";
                ss << "          \"anchor_x\": " << it.anchor_x() << ",\n";
                ss << "          \"anchor_y\": " << it.anchor_y() << ",\n";
                ss << "          \"opacity\": " << it.opacity() << ",\n";
                ss << "          \"blend_mode\": \"" << it.blend_mode() << "\"\n";
                ss << "        }" << (i + 1 < t.items().size() ? "," : "") << "\n";
            }
            ss << "      ]\n";
            ss << "    }" << (t_i + 1 < tracks.size() ? "," : "") << "\n";
        }
        ss << "  ]";
    };

    serialize_tracks(video_tracks_, "video_tracks");
    ss << ",\n";
    serialize_tracks(audio_tracks_, "audio_tracks");
    ss << "\n}";
    return ss.str();
}

namespace {

std::string extract_string_field(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }
    if (pos >= json.length() || json[pos] != '"') return "";
    pos++; // Skip opening quote
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

int64_t extract_int_field(const std::string& json, const std::string& key, int64_t default_val = 0) {
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

float extract_float_field(const std::string& json, const std::string& key, float default_val = 0.0f) {
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
    while (end_pos < json.length() && (std::isdigit(static_cast<unsigned char>(json[end_pos])) || json[end_pos] == '.')) {
        end_pos++;
    }
    if (end_pos == pos) return default_val;
    try {
        return std::stof(json.substr(pos, end_pos - pos));
    } catch (...) {
        return default_val;
    }
}

Rational extract_rational_field(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return Rational{0, 1};
    size_t obj_start = json.find('{', pos);
    size_t obj_end = json.find('}', obj_start);
    if (obj_start == std::string::npos || obj_end == std::string::npos) return Rational{0, 1};
    std::string obj_sub = json.substr(obj_start, obj_end - obj_start + 1);
    int64_t num = extract_int_field(obj_sub, "num", 0);
    int64_t den = extract_int_field(obj_sub, "den", 1);
    return Rational{num, den};
}

} // namespace

Timeline Timeline::from_json(const std::string& json_text) {
    std::string id = extract_string_field(json_text, "id");
    std::string name = extract_string_field(json_text, "name");
    Rational frame_rate = extract_rational_field(json_text, "frame_rate");
    int width = static_cast<int>(extract_int_field(json_text, "width", 1920));
    int height = static_cast<int>(extract_int_field(json_text, "height", 1080));
    int sample_rate = static_cast<int>(extract_int_field(json_text, "sample_rate", 48000));

    Timeline tl(std::move(id), std::move(name), frame_rate, width, height, sample_rate);
    tl.video_tracks_.clear();
    tl.audio_tracks_.clear();

    auto deserialize_tracks = [&](const char* array_name, bool is_video) {
        std::string needle = std::string("\"") + array_name + "\":";
        size_t arr_pos = json_text.find(needle);
        if (arr_pos == std::string::npos) return;
        size_t arr_start = json_text.find('[', arr_pos);
        if (arr_start == std::string::npos) return;

        int bracket_depth = 0;
        size_t arr_end = std::string::npos;
        for (size_t i = arr_start; i < json_text.length(); ++i) {
            if (json_text[i] == '[') bracket_depth++;
            else if (json_text[i] == ']') {
                bracket_depth--;
                if (bracket_depth == 0) {
                    arr_end = i;
                    break;
                }
            }
        }
        if (arr_end == std::string::npos) return;

        size_t cursor = arr_start;
        while (true) {
            size_t track_start = json_text.find('{', cursor);
            if (track_start == std::string::npos || track_start >= arr_end) break;

            int brace_depth = 0;
            size_t track_end = std::string::npos;
            for (size_t i = track_start; i <= arr_end; ++i) {
                if (json_text[i] == '{') brace_depth++;
                else if (json_text[i] == '}') {
                    brace_depth--;
                    if (brace_depth == 0) {
                        track_end = i;
                        break;
                    }
                }
            }
            if (track_end == std::string::npos) break;

            std::string track_json = json_text.substr(track_start, track_end - track_start + 1);
            std::string t_id = extract_string_field(track_json, "id");
            std::string t_name = extract_string_field(track_json, "name");
            TrackType t_type = track_type_from_string(extract_string_field(track_json, "type"));
            int t_index = static_cast<int>(extract_int_field(track_json, "index", 0));

            Track track(std::move(t_id), std::move(t_name), t_type, t_index);

            size_t items_pos = track_json.find("\"items\":");
            const std::string track_header = (items_pos != std::string::npos)
                ? track_json.substr(0, items_pos)
                : track_json;
            if (track_header.find("\"muted\": true") != std::string::npos ||
                track_header.find("\"muted\":true") != std::string::npos) {
                track.set_muted(true);
            }
            if (items_pos != std::string::npos) {
                size_t i_arr_start = track_json.find('[', items_pos);
                size_t i_arr_end = track_json.rfind(']');
                if (i_arr_start != std::string::npos && i_arr_end != std::string::npos && i_arr_end > i_arr_start) {
                    size_t i_cursor = i_arr_start;
                    while (true) {
                        size_t it_start = track_json.find('{', i_cursor);
                        if (it_start == std::string::npos || it_start >= i_arr_end) break;

                        int it_brace_depth = 0;
                        size_t it_end = std::string::npos;
                        for (size_t i = it_start; i <= i_arr_end; ++i) {
                            if (track_json[i] == '{') it_brace_depth++;
                            else if (track_json[i] == '}') {
                                it_brace_depth--;
                                if (it_brace_depth == 0) {
                                    it_end = i;
                                    break;
                                }
                            }
                        }
                        if (it_end == std::string::npos) break;

                        std::string item_json = track_json.substr(it_start, it_end - it_start + 1);
                        std::string it_id = extract_string_field(item_json, "id");
                        std::string it_clip_id = extract_string_field(item_json, "clip_id");
                        std::string it_asset_id = extract_string_field(item_json, "asset_id");
                        std::string it_name = extract_string_field(item_json, "name");
                        Rational it_tin = extract_rational_field(item_json, "timeline_in");
                        Rational it_dur = extract_rational_field(item_json, "duration");
                        Rational it_sin = extract_rational_field(item_json, "source_in");
                        Rational it_spd = extract_rational_field(item_json, "speed");

                        TimelineItem item(std::move(it_id), std::move(it_clip_id), std::move(it_asset_id),
                                         std::move(it_name), it_tin, it_dur, it_sin, it_spd);
                        item.set_transform(extract_float_field(item_json, "transform_x", 0.0f),
                                           extract_float_field(item_json, "transform_y", 0.0f));
                        item.set_scale(extract_float_field(item_json, "scale_x", 1.0f),
                                       extract_float_field(item_json, "scale_y", 1.0f));
                        item.set_rotation(extract_float_field(item_json, "rotation", 0.0f));
                        item.set_anchor(extract_float_field(item_json, "anchor_x", 0.5f),
                                        extract_float_field(item_json, "anchor_y", 0.5f));
                        item.set_opacity(extract_float_field(item_json, "opacity", 1.0f));
                        std::string it_bm = extract_string_field(item_json, "blend_mode");
                        if (!it_bm.empty()) {
                            item.set_blend_mode(std::move(it_bm));
                        }

                        track.add_item(std::move(item));

                        i_cursor = it_end + 1;
                    }
                }
            }

            if (is_video) {
                tl.video_tracks_.push_back(std::move(track));
            } else {
                tl.audio_tracks_.push_back(std::move(track));
            }

            cursor = track_end + 1;
        }
    };

    deserialize_tracks("video_tracks", true);
    deserialize_tracks("audio_tracks", false);

    return tl;
}

} // namespace ave
