#include "ave/otio_interchange.hpp"
#include "ave/uuid.hpp"

#include <sstream>
#include <iomanip>
#include <algorithm>

namespace ave {

namespace {

std::string otio_extract_string(const std::string& json, const std::string& key) {
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

double otio_extract_double(const std::string& json, const std::string& key, double default_val = 0.0) {
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
        return std::stod(json.substr(pos, end_pos - pos));
    } catch (...) {
        return default_val;
    }
}

} // namespace

std::string OtioInterchange::export_to_json(const Timeline& timeline,
                                            const MediaPool& pool,
                                            OtioLossReport& report) {
    std::ostringstream ss;
    double fps = timeline.frame_rate().to_seconds();
    if (fps <= 0.0) fps = 24.0;

    ss << "{\n";
    ss << "  \"OTIO_SCHEMA\": \"Timeline.1\",\n";
    ss << "  \"name\": \"" << timeline.name() << "\",\n";
    ss << "  \"global_start_time\": {\n";
    ss << "    \"OTIO_SCHEMA\": \"RationalTime.1\",\n";
    ss << "    \"rate\": " << fps << ",\n";
    ss << "    \"value\": 0\n";
    ss << "  },\n";
    ss << "  \"tracks\": {\n";
    ss << "    \"OTIO_SCHEMA\": \"Stack.1\",\n";
    ss << "    \"children\": [\n";

    auto serialize_otio_tracks = [&](const std::vector<Track>& tracks, const std::string& kind_str, bool has_more) {
        for (size_t t_i = 0; t_i < tracks.size(); ++t_i) {
            const auto& trk = tracks[t_i];
            ss << "      {\n";
            ss << "        \"OTIO_SCHEMA\": \"Track.1\",\n";
            ss << "        \"name\": \"" << trk.name() << "\",\n";
            ss << "        \"kind\": \"" << kind_str << "\",\n";
            ss << "        \"children\": [\n";

            Rational cursor_time{0, 1};
            const auto& items = trk.items();

            for (size_t i = 0; i < items.size(); ++i) {
                const auto& it = items[i];

                // Gap handling
                if (it.timeline_in() > cursor_time) {
                    Rational gap_dur = it.timeline_in() - cursor_time;
                    double gap_start_val = cursor_time.to_seconds() * fps;
                    double gap_dur_val = gap_dur.to_seconds() * fps;

                    ss << "          {\n";
                    ss << "            \"OTIO_SCHEMA\": \"Gap.1\",\n";
                    ss << "            \"source_range\": {\n";
                    ss << "              \"OTIO_SCHEMA\": \"TimeRange.1\",\n";
                    ss << "              \"start_time\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": " << fps << ", \"value\": " << gap_start_val << "},\n";
                    ss << "              \"duration\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": " << fps << ", \"value\": " << gap_dur_val << "}\n";
                    ss << "            }\n";
                    ss << "          },\n";
                }

                // Clip handling
                double clip_src_in = it.source_in().to_seconds() * fps;
                double clip_dur = it.duration().to_seconds() * fps;

                std::string target_url = "";
                const auto* asset = pool.find_asset(it.asset_id());
                if (asset) {
                    target_url = asset->file_path().string();
                }

                // Check for non-standard visual transforms
                if (it.blend_mode() != "Over" || it.opacity() < 0.999f || it.scale_x() != 1.0f || it.scale_y() != 1.0f) {
                    report.warnings.push_back("Clip '" + it.name() + "' has visual transform/blend mode preserved in metadata");
                }

                ss << "          {\n";
                ss << "            \"OTIO_SCHEMA\": \"Clip.1\",\n";
                ss << "            \"name\": \"" << it.name() << "\",\n";
                ss << "            \"source_range\": {\n";
                ss << "              \"OTIO_SCHEMA\": \"TimeRange.1\",\n";
                ss << "              \"start_time\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": " << fps << ", \"value\": " << clip_src_in << "},\n";
                ss << "              \"duration\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": " << fps << ", \"value\": " << clip_dur << "}\n";
                ss << "            },\n";
                ss << "            \"media_reference\": {\n";
                ss << "              \"OTIO_SCHEMA\": \"ExternalReference.1\",\n";
                ss << "              \"target_url\": \"" << target_url << "\"\n";
                ss << "            },\n";
                ss << "            \"metadata\": {\n";
                ss << "              \"ave\": {\n";
                ss << "                \"asset_id\": \"" << it.asset_id() << "\",\n";
                ss << "                \"clip_id\": \"" << it.clip_id() << "\",\n";
                ss << "                \"transform_x\": " << it.transform_x() << ",\n";
                ss << "                \"transform_y\": " << it.transform_y() << ",\n";
                ss << "                \"scale_x\": " << it.scale_x() << ",\n";
                ss << "                \"scale_y\": " << it.scale_y() << ",\n";
                ss << "                \"rotation\": " << it.rotation() << ",\n";
                ss << "                \"opacity\": " << it.opacity() << ",\n";
                ss << "                \"blend_mode\": \"" << it.blend_mode() << "\"\n";
                ss << "              }\n";
                ss << "            }\n";
                ss << "          }" << (i + 1 < items.size() ? "," : "") << "\n";

                cursor_time = it.timeline_out();
            }

            ss << "        ]\n";
            ss << "      }" << ((t_i + 1 < tracks.size() || has_more) ? "," : "") << "\n";
        }
    };

    serialize_otio_tracks(timeline.video_tracks(), "Video", !timeline.audio_tracks().empty());
    serialize_otio_tracks(timeline.audio_tracks(), "Audio", false);

    ss << "    ]\n";
    ss << "  }\n";
    ss << "}";

    return ss.str();
}

bool OtioInterchange::import_from_json(const std::string& otio_json,
                                       Timeline& timeline,
                                       MediaPool& pool,
                                       OtioLossReport& report) {
    if (otio_json.find("\"OTIO_SCHEMA\": \"Timeline.1\"") == std::string::npos &&
        otio_json.find("\"OTIO_SCHEMA\":\"Timeline.1\"") == std::string::npos) {
        report.unsupported_features.push_back("Missing required Timeline.1 schema identifier");
        return false;
    }

    std::string tl_name = otio_extract_string(otio_json, "name");
    double rate = otio_extract_double(otio_json, "rate", 24.0);
    if (rate <= 0.0) rate = 24.0;

    int fps_int = static_cast<int>(std::round(rate));
    if (fps_int <= 0) fps_int = 24;

    timeline = Timeline(UUID::generate(), tl_name.empty() ? "Imported Timeline" : tl_name, Rational{fps_int, 1});

    // Locate tracks array
    size_t tracks_pos = otio_json.find("\"tracks\":");
    if (tracks_pos == std::string::npos) {
        return true;
    }

    size_t children_pos = otio_json.find("\"children\":", tracks_pos);
    if (children_pos == std::string::npos) {
        return true;
    }

    size_t arr_start = otio_json.find('[', children_pos);
    if (arr_start == std::string::npos) {
        return true;
    }

    size_t cursor = arr_start + 1;
    while (cursor < otio_json.length()) {
        size_t trk_start = otio_json.find('{', cursor);
        if (trk_start == std::string::npos) break;

        int brace_count = 0;
        size_t trk_end = trk_start;
        for (; trk_end < otio_json.length(); ++trk_end) {
            if (otio_json[trk_end] == '{') brace_count++;
            else if (otio_json[trk_end] == '}') {
                brace_count--;
                if (brace_count == 0) break;
            }
        }
        if (brace_count != 0) break;

        std::string trk_json = otio_json.substr(trk_start, trk_end - trk_start + 1);
        std::string trk_name = otio_extract_string(trk_json, "name");
        std::string kind = otio_extract_string(trk_json, "kind");

        Track* target_track = nullptr;
        if (kind == "Audio") {
            if (!timeline.audio_tracks().empty() && timeline.audio_tracks()[0].items().empty()) {
                target_track = timeline.find_track_mut(timeline.audio_tracks()[0].id());
                target_track->set_name(trk_name.empty() ? "A1" : trk_name);
            } else {
                target_track = &timeline.add_audio_track(trk_name);
            }
        } else {
            if (!timeline.video_tracks().empty() && timeline.video_tracks()[0].items().empty()) {
                target_track = timeline.find_track_mut(timeline.video_tracks()[0].id());
                target_track->set_name(trk_name.empty() ? "V1" : trk_name);
            } else {
                target_track = &timeline.add_video_track(trk_name);
            }
        }

        // Parse track children (clips & gaps)
        size_t ch_pos = trk_json.find("\"children\":");
        if (ch_pos != std::string::npos) {
            size_t c_arr = trk_json.find('[', ch_pos);
            if (c_arr != std::string::npos) {
                size_t c_cursor = c_arr + 1;
                Rational current_pts{0, 1};

                while (c_cursor < trk_json.length()) {
                    size_t item_start = trk_json.find('{', c_cursor);
                    if (item_start == std::string::npos) break;

                    int c_braces = 0;
                    size_t item_end = item_start;
                    for (; item_end < trk_json.length(); ++item_end) {
                        if (trk_json[item_end] == '{') c_braces++;
                        else if (trk_json[item_end] == '}') {
                            c_braces--;
                            if (c_braces == 0) break;
                        }
                    }
                    if (c_braces != 0) break;

                    std::string item_json = trk_json.substr(item_start, item_end - item_start + 1);
                    std::string schema = otio_extract_string(item_json, "OTIO_SCHEMA");

                    if (schema == "Gap.1") {
                        // Extract duration
                        size_t dur_pos = item_json.find("\"duration\":");
                        if (dur_pos != std::string::npos) {
                            double dur_val = otio_extract_double(item_json.substr(dur_pos), "value", 0.0);
                            current_pts = current_pts + Rational{static_cast<int64_t>(std::round(dur_val)), fps_int};
                        }
                    } else if (schema == "Clip.1") {
                        std::string clip_name = otio_extract_string(item_json, "name");
                        std::string target_url = otio_extract_string(item_json, "target_url");

                        double src_in_val = 0.0;
                        size_t st_pos = item_json.find("\"start_time\":");
                        if (st_pos != std::string::npos) {
                            src_in_val = otio_extract_double(item_json.substr(st_pos), "value", 0.0);
                        }

                        double dur_val = 24.0;
                        size_t dur_pos = item_json.find("\"duration\":");
                        if (dur_pos != std::string::npos) {
                            dur_val = otio_extract_double(item_json.substr(dur_pos), "value", 24.0);
                        }

                        Rational src_in{static_cast<int64_t>(std::round(src_in_val)), fps_int};
                        Rational duration{static_cast<int64_t>(std::round(dur_val)), fps_int};
                        if (duration.numerator() <= 0) duration = Rational{1, fps_int};

                        std::string asset_id = otio_extract_string(item_json, "asset_id");
                        if (asset_id.empty()) {
                            asset_id = "asset-" + UUID::generate();
                            if (!target_url.empty()) {
                                MediaAsset asset(asset_id, target_url, clip_name, 0, "mp4", duration, {});
                                pool.register_asset(std::move(asset));
                            }
                        }

                        std::string clip_id = otio_extract_string(item_json, "clip_id");
                        if (clip_id.empty()) clip_id = "clip-" + UUID::generate();

                        TimelineItem item(UUID::generate(), clip_id, asset_id, clip_name, current_pts, duration, src_in);

                        float sx = static_cast<float>(otio_extract_double(item_json, "scale_x", 1.0));
                        float sy = static_cast<float>(otio_extract_double(item_json, "scale_y", 1.0));
                        item.set_scale(sx, sy);

                        float tx = static_cast<float>(otio_extract_double(item_json, "transform_x", 0.0));
                        float ty = static_cast<float>(otio_extract_double(item_json, "transform_y", 0.0));
                        item.set_transform(tx, ty);

                        float rot = static_cast<float>(otio_extract_double(item_json, "rotation", 0.0));
                        item.set_rotation(rot);

                        float op = static_cast<float>(otio_extract_double(item_json, "opacity", 1.0));
                        item.set_opacity(op);

                        std::string bm = otio_extract_string(item_json, "blend_mode");
                        if (!bm.empty()) item.set_blend_mode(bm);

                        target_track->add_item(std::move(item));
                        current_pts = current_pts + duration;
                    }

                    c_cursor = item_end + 1;
                    size_t nb = trk_json.find(']', c_cursor);
                    size_t nbr = trk_json.find('{', c_cursor);
                    if (nb != std::string::npos && (nbr == std::string::npos || nb < nbr)) break;
                }
            }
        }

        cursor = trk_end + 1;
        size_t next_bracket = otio_json.find(']', cursor);
        size_t next_brace = otio_json.find('{', cursor);
        if (next_bracket != std::string::npos && (next_brace == std::string::npos || next_bracket < next_brace)) {
            break;
        }
    }

    return true;
}

} // namespace ave

