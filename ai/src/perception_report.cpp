#include <ave/perception_report.hpp>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace ave {

namespace {

std::string json_escape(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        if (c == '"') o << "\\\"";
        else if (c == '\\') o << "\\\\";
        else if (c == '\b') o << "\\b";
        else if (c == '\f') o << "\\f";
        else if (c == '\n') o << "\\n";
        else if (c == '\r') o << "\\r";
        else if (c == '\t') o << "\\t";
        else if (static_cast<unsigned char>(c) <= 0x1f) {
            o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        } else {
            o << c;
        }
    }
    return o.str();
}

size_t find_matching_bracket(const std::string& str, size_t open_pos) {
    if (open_pos == std::string::npos || open_pos >= str.length() || str[open_pos] != '[') {
        return std::string::npos;
    }
    int count = 0;
    bool in_str = false;
    for (size_t i = open_pos; i < str.length(); ++i) {
        if (str[i] == '"' && (i == 0 || str[i - 1] != '\\')) {
            in_str = !in_str;
        } else if (!in_str) {
            if (str[i] == '[') count++;
            else if (str[i] == ']') {
                count--;
                if (count == 0) return i;
            }
        }
    }
    return std::string::npos;
}

size_t find_matching_brace(const std::string& str, size_t open_pos) {
    if (open_pos == std::string::npos || open_pos >= str.length() || str[open_pos] != '{') {
        return std::string::npos;
    }
    int count = 0;
    bool in_str = false;
    for (size_t i = open_pos; i < str.length(); ++i) {
        if (str[i] == '"' && (i == 0 || str[i - 1] != '\\')) {
            in_str = !in_str;
        } else if (!in_str) {
            if (str[i] == '{') count++;
            else if (str[i] == '}') {
                count--;
                if (count == 0) return i;
            }
        }
    }
    return std::string::npos;
}

std::string extract_string(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        needle = "\"" + key + "\" :";
        pos = json.find(needle);
        if (pos == std::string::npos) return "";
    }
    pos += needle.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }
    if (pos >= json.length() || json[pos] != '"') return "";
    pos++; // skip quote
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

double extract_double(const std::string& json, const std::string& key, double default_val = 0.0) {
    std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        needle = "\"" + key + "\" :";
        pos = json.find(needle);
        if (pos == std::string::npos) return default_val;
    }
    pos += needle.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }
    if (pos >= json.length()) return default_val;
    size_t end_pos = pos;
    if (json[end_pos] == '-' || json[end_pos] == '+') end_pos++;
    while (end_pos < json.length() && (std::isdigit(static_cast<unsigned char>(json[end_pos])) || json[end_pos] == '.' || json[end_pos] == 'e' || json[end_pos] == 'E' || json[end_pos] == '+' || json[end_pos] == '-')) {
        end_pos++;
    }
    try {
        return std::stod(json.substr(pos, end_pos - pos));
    } catch (...) {
        return default_val;
    }
}

int64_t extract_int64(const std::string& json, const std::string& key, int64_t default_val = 0) {
    std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        needle = "\"" + key + "\" :";
        pos = json.find(needle);
        if (pos == std::string::npos) return default_val;
    }
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
    try {
        return std::stoll(json.substr(pos, end_pos - pos));
    } catch (...) {
        return default_val;
    }
}

Rational extract_rational(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return Rational(0, 1);
    size_t obj_start = json.find('{', pos);
    if (obj_start == std::string::npos) return Rational(0, 1);
    size_t obj_end = json.find('}', obj_start);
    if (obj_end == std::string::npos) return Rational(0, 1);
    std::string sub = json.substr(obj_start, obj_end - obj_start + 1);
    int64_t num = extract_int64(sub, "num", 0);
    int64_t den = extract_int64(sub, "den", 1);
    if (den == 0) den = 1;
    return Rational(num, den);
}

void append_rational(std::ostringstream& oss, const std::string& key, const Rational& r) {
    oss << "\"" << key << "\":{\"num\":" << r.numerator() << ",\"den\":" << r.denominator() << "}";
}

} // namespace

std::string shot_transition_type_to_string(ShotTransitionType type) {
    switch (type) {
        case ShotTransitionType::Cut: return "cut";
        case ShotTransitionType::Dissolve: return "dissolve";
        case ShotTransitionType::FadeIn: return "fade_in";
        case ShotTransitionType::FadeOut: return "fade_out";
        default: return "unknown";
    }
}

ShotTransitionType string_to_shot_transition_type(const std::string& str) {
    if (str == "cut") return ShotTransitionType::Cut;
    if (str == "dissolve") return ShotTransitionType::Dissolve;
    if (str == "fade_in") return ShotTransitionType::FadeIn;
    if (str == "fade_out") return ShotTransitionType::FadeOut;
    return ShotTransitionType::Unknown;
}

const RawProbabilityCurve* PerceptionReport::find_raw_curve(const std::string& name) const {
    for (const auto& curve : raw_curves) {
        if (curve.name == name) {
            return &curve;
        }
    }
    return nullptr;
}

void PerceptionReport::add_raw_curve(std::string name, std::vector<std::pair<Rational, double>> points) {
    for (auto& curve : raw_curves) {
        if (curve.name == name) {
            curve.points = std::move(points);
            return;
        }
    }
    raw_curves.push_back({std::move(name), std::move(points)});
}

std::string PerceptionReport::to_json() const {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"schema_version\": " << schema_version << ",\n";
    oss << "  \"asset_id\": \"" << json_escape(asset_id) << "\",\n";
    oss << "  \"asset_hash\": \"" << json_escape(asset_hash) << "\",\n";
    oss << "  "; append_rational(oss, "duration", duration); oss << ",\n";
    oss << "  "; append_rational(oss, "time_base", time_base); oss << ",\n";

    // Shots
    oss << "  \"shots\": [";
    for (size_t i = 0; i < shots.size(); ++i) {
        const auto& s = shots[i];
        if (i > 0) oss << ",";
        oss << "\n    {";
        append_rational(oss, "start", s.start_pts); oss << ",";
        append_rational(oss, "end", s.end_pts); oss << ",";
        oss << "\"type\":\"" << shot_transition_type_to_string(s.type) << "\",";
        oss << "\"confidence\":" << s.confidence << ",";
        oss << "\"keyframes\":[";
        for (size_t k = 0; k < s.keyframes.size(); ++k) {
            if (k > 0) oss << ",";
            append_rational(oss, "pts", s.keyframes[k]);
        }
        oss << "]}";
    }
    if (!shots.empty()) oss << "\n  ";
    oss << "],\n";

    // Rhythm
    oss << "  \"rhythm\": {\n";
    oss << "    \"primary_tempo_bpm\": " << rhythm.primary_tempo_bpm << ",\n";
    oss << "    \"integrated_loudness_lufs\": " << rhythm.integrated_loudness_lufs << ",\n";
    oss << "    \"alternate_tempos\": [";
    for (size_t i = 0; i < rhythm.alternate_tempos.size(); ++i) {
        if (i > 0) oss << ",";
        oss << rhythm.alternate_tempos[i];
    }
    oss << "],\n";
    oss << "    \"beats\": [";
    for (size_t i = 0; i < rhythm.beats.size(); ++i) {
        const auto& b = rhythm.beats[i];
        if (i > 0) oss << ",";
        oss << "\n      {";
        append_rational(oss, "pts", b.pts); oss << ",";
        oss << "\"downbeat\":" << (b.is_downbeat ? "true" : "false") << ",";
        oss << "\"confidence\":" << b.confidence << "}";
    }
    if (!rhythm.beats.empty()) oss << "\n    ";
    oss << "],\n";
    oss << "    \"sections\": [";
    for (size_t i = 0; i < rhythm.sections.size(); ++i) {
        const auto& sec = rhythm.sections[i];
        if (i > 0) oss << ",";
        oss << "\n      {";
        append_rational(oss, "start", sec.start_pts); oss << ",";
        append_rational(oss, "end", sec.end_pts); oss << ",";
        oss << "\"label\":\"" << json_escape(sec.label) << "\"}";
    }
    if (!rhythm.sections.empty()) oss << "\n    ";
    oss << "]\n";
    oss << "  },\n";

    // Speech
    oss << "  \"speech\": {\n";
    oss << "    \"segments\": [";
    for (size_t i = 0; i < speech.segments.size(); ++i) {
        const auto& seg = speech.segments[i];
        if (i > 0) oss << ",";
        oss << "\n      {";
        append_rational(oss, "start", seg.start_pts); oss << ",";
        append_rational(oss, "end", seg.end_pts); oss << ",";
        oss << "\"confidence\":" << seg.confidence << "}";
    }
    if (!speech.segments.empty()) oss << "\n    ";
    oss << "],\n";
    oss << "    \"words\": [";
    for (size_t i = 0; i < speech.words.size(); ++i) {
        const auto& w = speech.words[i];
        if (i > 0) oss << ",";
        oss << "\n      {\"word\":\"" << json_escape(w.word) << "\",";
        append_rational(oss, "start", w.start_pts); oss << ",";
        append_rational(oss, "end", w.end_pts); oss << ",";
        oss << "\"confidence\":" << w.confidence << "}";
    }
    if (!speech.words.empty()) oss << "\n    ";
    oss << "]\n";
    oss << "  },\n";

    // Visual
    oss << "  \"visual\": {\n";
    oss << "    \"average_motion_energy\": " << visual.average_motion_energy << ",\n";
    oss << "    \"color_look\": {\"lab_l\":" << visual.color_look.lab_l
        << ",\"lab_a\":" << visual.color_look.lab_a
        << ",\"lab_b\":" << visual.color_look.lab_b
        << ",\"contrast\":" << visual.color_look.contrast << "}\n";
    oss << "  },\n";

    // Raw Curves
    oss << "  \"raw_curves\": [";
    for (size_t i = 0; i < raw_curves.size(); ++i) {
        const auto& c = raw_curves[i];
        if (i > 0) oss << ",";
        oss << "\n    {\"name\":\"" << json_escape(c.name) << "\",\"points\":[";
        for (size_t p = 0; p < c.points.size(); ++p) {
            if (p > 0) oss << ",";
            oss << "{\"num\":" << c.points[p].first.numerator()
                << ",\"den\":" << c.points[p].first.denominator()
                << ",\"val\":" << c.points[p].second << "}";
        }
        oss << "]}";
    }
    if (!raw_curves.empty()) oss << "\n  ";
    oss << "]\n";

    oss << "}\n";
    return oss.str();
}

PerceptionReport PerceptionReport::from_json(const std::string& json_str) {
    PerceptionReport report;
    report.schema_version = static_cast<int>(extract_int64(json_str, "schema_version", 1));
    report.asset_id = extract_string(json_str, "asset_id");
    report.asset_hash = extract_string(json_str, "asset_hash");
    report.duration = extract_rational(json_str, "duration");
    report.time_base = extract_rational(json_str, "time_base");

    // Parse shots
    size_t shots_pos = json_str.find("\"shots\":");
    if (shots_pos != std::string::npos) {
        size_t arr_start = json_str.find('[', shots_pos);
        size_t arr_end = find_matching_bracket(json_str, arr_start);
        if (arr_start != std::string::npos && arr_end != std::string::npos) {
            size_t cursor = arr_start + 1;
            while (cursor < arr_end) {
                size_t obj_start = json_str.find('{', cursor);
                if (obj_start == std::string::npos || obj_start >= arr_end) break;
                size_t obj_end = find_matching_brace(json_str, obj_start);
                if (obj_end == std::string::npos || obj_end > arr_end) break;

                std::string item_json = json_str.substr(obj_start, obj_end - obj_start + 1);
                ShotBoundary shot;
                shot.start_pts = extract_rational(item_json, "start");
                shot.end_pts = extract_rational(item_json, "end");
                shot.type = string_to_shot_transition_type(extract_string(item_json, "type"));
                shot.confidence = extract_double(item_json, "confidence", 1.0);
                report.shots.push_back(shot);

                cursor = obj_end + 1;
            }
        }
    }

    // Parse rhythm
    size_t rhythm_pos = json_str.find("\"rhythm\":");
    if (rhythm_pos != std::string::npos) {
        report.rhythm.primary_tempo_bpm = extract_double(json_str.substr(rhythm_pos), "primary_tempo_bpm", 120.0);
        report.rhythm.integrated_loudness_lufs = extract_double(json_str.substr(rhythm_pos), "integrated_loudness_lufs", -14.0);

        // Parse beats
        size_t beats_pos = json_str.find("\"beats\":", rhythm_pos);
        if (beats_pos != std::string::npos) {
            size_t arr_start = json_str.find('[', beats_pos);
            size_t arr_end = find_matching_bracket(json_str, arr_start);
            if (arr_start != std::string::npos && arr_end != std::string::npos) {
                size_t cursor = arr_start + 1;
                while (cursor < arr_end) {
                    size_t obj_start = json_str.find('{', cursor);
                    if (obj_start == std::string::npos || obj_start >= arr_end) break;
                    size_t obj_end = find_matching_brace(json_str, obj_start);
                    if (obj_end == std::string::npos || obj_end > arr_end) break;

                    std::string item_json = json_str.substr(obj_start, obj_end - obj_start + 1);
                    BeatMarker beat;
                    beat.pts = extract_rational(item_json, "pts");
                    beat.is_downbeat = item_json.find("\"downbeat\":true") != std::string::npos ||
                                       item_json.find("\"downbeat\": true") != std::string::npos;
                    beat.confidence = extract_double(item_json, "confidence", 1.0);
                    report.rhythm.beats.push_back(beat);

                    cursor = obj_end + 1;
                }
            }
        }
    }

    // Parse speech
    size_t speech_pos = json_str.find("\"speech\":");
    if (speech_pos != std::string::npos) {
        size_t segs_pos = json_str.find("\"segments\":", speech_pos);
        if (segs_pos != std::string::npos) {
            size_t arr_start = json_str.find('[', segs_pos);
            size_t arr_end = find_matching_bracket(json_str, arr_start);
            if (arr_start != std::string::npos && arr_end != std::string::npos) {
                size_t cursor = arr_start + 1;
                while (cursor < arr_end) {
                    size_t obj_start = json_str.find('{', cursor);
                    if (obj_start == std::string::npos || obj_start >= arr_end) break;
                    size_t obj_end = find_matching_brace(json_str, obj_start);
                    if (obj_end == std::string::npos || obj_end > arr_end) break;

                    std::string item_json = json_str.substr(obj_start, obj_end - obj_start + 1);
                    SpeechSegment seg;
                    seg.start_pts = extract_rational(item_json, "start");
                    seg.end_pts = extract_rational(item_json, "end");
                    seg.confidence = extract_double(item_json, "confidence", 1.0);
                    report.speech.segments.push_back(seg);

                    cursor = obj_end + 1;
                }
            }
        }

        size_t words_pos = json_str.find("\"words\":", speech_pos);
        if (words_pos != std::string::npos) {
            size_t arr_start = json_str.find('[', words_pos);
            size_t arr_end = find_matching_bracket(json_str, arr_start);
            if (arr_start != std::string::npos && arr_end != std::string::npos) {
                size_t cursor = arr_start + 1;
                while (cursor < arr_end) {
                    size_t obj_start = json_str.find('{', cursor);
                    if (obj_start == std::string::npos || obj_start >= arr_end) break;
                    size_t obj_end = find_matching_brace(json_str, obj_start);
                    if (obj_end == std::string::npos || obj_end > arr_end) break;

                    std::string item_json = json_str.substr(obj_start, obj_end - obj_start + 1);
                    WordTimestamp word;
                    word.word = extract_string(item_json, "word");
                    word.start_pts = extract_rational(item_json, "start");
                    word.end_pts = extract_rational(item_json, "end");
                    word.confidence = extract_double(item_json, "confidence", 1.0);
                    report.speech.words.push_back(word);

                    cursor = obj_end + 1;
                }
            }
        }
    }

    // Parse visual
    size_t visual_pos = json_str.find("\"visual\":");
    if (visual_pos != std::string::npos) {
        report.visual.average_motion_energy = extract_double(json_str.substr(visual_pos), "average_motion_energy", 0.0);
        report.visual.color_look.lab_l = extract_double(json_str.substr(visual_pos), "lab_l", 50.0);
        report.visual.color_look.lab_a = extract_double(json_str.substr(visual_pos), "lab_a", 0.0);
        report.visual.color_look.lab_b = extract_double(json_str.substr(visual_pos), "lab_b", 0.0);
        report.visual.color_look.contrast = extract_double(json_str.substr(visual_pos), "contrast", 1.0);
    }

    // Parse raw_curves
    size_t curves_pos = json_str.find("\"raw_curves\":");
    if (curves_pos != std::string::npos) {
        size_t arr_start = json_str.find('[', curves_pos);
        size_t arr_end = find_matching_bracket(json_str, arr_start);
        if (arr_start != std::string::npos && arr_end != std::string::npos) {
            size_t cursor = arr_start + 1;
            while (cursor < arr_end) {
                size_t c_start = json_str.find('{', cursor);
                if (c_start == std::string::npos || c_start >= arr_end) break;
                size_t c_end = find_matching_brace(json_str, c_start);
                if (c_end == std::string::npos || c_end > arr_end) break;

                std::string c_json = json_str.substr(c_start, c_end - c_start + 1);
                std::string c_name = extract_string(c_json, "name");

                size_t points_pos = c_json.find("\"points\":");
                std::vector<std::pair<Rational, double>> pts;
                if (points_pos != std::string::npos) {
                    size_t p_arr_start = c_json.find('[', points_pos);
                    size_t p_arr_end = find_matching_bracket(c_json, p_arr_start);
                    if (p_arr_start != std::string::npos && p_arr_end != std::string::npos) {
                        size_t p_cursor = p_arr_start + 1;
                        while (p_cursor < p_arr_end) {
                            size_t p_start = c_json.find('{', p_cursor);
                            if (p_start == std::string::npos || p_start >= p_arr_end) break;
                            size_t p_end = find_matching_brace(c_json, p_start);
                            if (p_end == std::string::npos || p_end > p_arr_end) break;

                            std::string p_json = c_json.substr(p_start, p_end - p_start + 1);
                            int64_t num = extract_int64(p_json, "num", 0);
                            int64_t den = extract_int64(p_json, "den", 1);
                            double val = extract_double(p_json, "val", 0.0);
                            pts.push_back({Rational(num, den), val});

                            p_cursor = p_end + 1;
                        }
                    }
                }

                report.add_raw_curve(c_name, std::move(pts));
                cursor = c_end + 1;
            }
        }
    }

    return report;
}

} // namespace ave
