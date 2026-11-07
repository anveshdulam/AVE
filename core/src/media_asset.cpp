#include "ave/media_asset.hpp"

#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace ave {

std::string to_string(StreamType type) {
    switch (type) {
        case StreamType::Video: return "video";
        case StreamType::Audio: return "audio";
        case StreamType::Subtitle: return "subtitle";
        case StreamType::Data: return "data";
        case StreamType::Unknown:
        default: return "unknown";
    }
}

StreamType stream_type_from_string(std::string_view str) {
    if (str == "video") return StreamType::Video;
    if (str == "audio") return StreamType::Audio;
    if (str == "subtitle") return StreamType::Subtitle;
    if (str == "data") return StreamType::Data;
    return StreamType::Unknown;
}

SourceStream::SourceStream(int index, StreamType type, std::string codec_name,
                           Rational timebase, int64_t duration_ticks)
    : index_(index),
      type_(type),
      codec_name_(std::move(codec_name)),
      timebase_(timebase),
      duration_ticks_(duration_ticks) {}

MediaAsset::MediaAsset(std::string id,
                       std::filesystem::path file_path,
                       std::string display_name,
                       uint64_t file_size_bytes,
                       std::string container_format,
                       Rational duration_seconds,
                       std::vector<SourceStream> streams)
    : id_(std::move(id)),
      file_path_(std::move(file_path)),
      display_name_(std::move(display_name)),
      file_size_bytes_(file_size_bytes),
      container_format_(std::move(container_format)),
      duration_seconds_(duration_seconds),
      streams_(std::move(streams)) {}

const SourceStream* MediaAsset::primary_video_stream() const noexcept {
    for (const auto& stream : streams_) {
        if (stream.type() == StreamType::Video) {
            return &stream;
        }
    }
    return nullptr;
}

const SourceStream* MediaAsset::primary_audio_stream() const noexcept {
    for (const auto& stream : streams_) {
        if (stream.type() == StreamType::Audio) {
            return &stream;
        }
    }
    return nullptr;
}

std::vector<const SourceStream*> MediaAsset::video_streams() const {
    std::vector<const SourceStream*> res;
    for (const auto& stream : streams_) {
        if (stream.type() == StreamType::Video) {
            res.push_back(&stream);
        }
    }
    return res;
}

std::vector<const SourceStream*> MediaAsset::audio_streams() const {
    std::vector<const SourceStream*> res;
    for (const auto& stream : streams_) {
        if (stream.type() == StreamType::Audio) {
            res.push_back(&stream);
        }
    }
    return res;
}

namespace {

std::string escape_json(const std::string& str) {
    std::ostringstream ss;
    for (char c : str) {
        switch (c) {
            case '"': ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default: ss << c; break;
        }
    }
    return ss.str();
}

} // namespace

std::string MediaAsset::to_json() const {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"id\": \"" << escape_json(id_) << "\",\n";
    ss << "  \"file_path\": \"" << escape_json(file_path_.generic_string()) << "\",\n";
    ss << "  \"display_name\": \"" << escape_json(display_name_) << "\",\n";
    ss << "  \"file_size_bytes\": " << file_size_bytes_ << ",\n";
    ss << "  \"container_format\": \"" << escape_json(container_format_) << "\",\n";
    ss << "  \"duration_seconds\": {\"num\": " << duration_seconds_.numerator()
       << ", \"den\": " << duration_seconds_.denominator() << "},\n";
    ss << "  \"streams\": [\n";

    for (size_t i = 0; i < streams_.size(); ++i) {
        const auto& s = streams_[i];
        ss << "    {\n";
        ss << "      \"index\": " << s.index() << ",\n";
        ss << "      \"type\": \"" << to_string(s.type()) << "\",\n";
        ss << "      \"codec_name\": \"" << escape_json(s.codec_name()) << "\",\n";
        ss << "      \"timebase\": {\"num\": " << s.timebase().numerator()
           << ", \"den\": " << s.timebase().denominator() << "},\n";
        ss << "      \"duration_ticks\": " << s.duration_ticks();

        if (s.video_info().has_value()) {
            const auto& v = *s.video_info();
            ss << ",\n";
            ss << "      \"video_info\": {\n";
            ss << "        \"width\": " << v.width << ",\n";
            ss << "        \"height\": " << v.height << ",\n";
            ss << "        \"frame_rate\": {\"num\": " << v.frame_rate.numerator()
               << ", \"den\": " << v.frame_rate.denominator() << "},\n";
            ss << "        \"pixel_aspect_ratio\": {\"num\": " << v.pixel_aspect_ratio.numerator()
               << ", \"den\": " << v.pixel_aspect_ratio.denominator() << "},\n";
            ss << "        \"pixel_format\": \"" << escape_json(v.pixel_format) << "\",\n";
            ss << "        \"color_space\": \"" << escape_json(v.color_space) << "\",\n";
            ss << "        \"color_range\": \"" << escape_json(v.color_range) << "\",\n";
            ss << "        \"bit_depth\": " << v.bit_depth << "\n";
            ss << "      }\n";
        } else if (s.audio_info().has_value()) {
            const auto& a = *s.audio_info();
            ss << ",\n";
            ss << "      \"audio_info\": {\n";
            ss << "        \"sample_rate\": " << a.sample_rate << ",\n";
            ss << "        \"channels\": " << a.channels << ",\n";
            ss << "        \"channel_layout\": \"" << escape_json(a.channel_layout) << "\",\n";
            ss << "        \"sample_format\": \"" << escape_json(a.sample_format) << "\",\n";
            ss << "        \"bit_depth\": " << a.bit_depth << "\n";
            ss << "      }\n";
        } else {
            ss << "\n";
        }

        ss << "    }" << (i + 1 < streams_.size() ? "," : "") << "\n";
    }

    ss << "  ]\n";
    ss << "}";
    return ss.str();
}

namespace {

// Minimal parser helper for extracting JSON fields safely
std::string extract_string_field(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }
    if (pos >= json.length() || json[pos] != '"') return "";
    pos++; // skip open quote
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

MediaAsset MediaAsset::from_json(const std::string& json_text) {
    std::string id = extract_string_field(json_text, "id");
    std::string file_path = extract_string_field(json_text, "file_path");
    std::string display_name = extract_string_field(json_text, "display_name");
    uint64_t file_size = static_cast<uint64_t>(extract_int_field(json_text, "file_size_bytes", 0));
    std::string container_format = extract_string_field(json_text, "container_format");
    Rational duration = extract_rational_field(json_text, "duration_seconds");

    std::vector<SourceStream> streams;
    size_t streams_pos = json_text.find("\"streams\":");
    if (streams_pos != std::string::npos) {
        size_t arr_start = json_text.find('[', streams_pos);
        size_t arr_end = json_text.rfind(']');
        if (arr_start != std::string::npos && arr_end != std::string::npos && arr_end > arr_start) {
            size_t cursor = arr_start;
            while (true) {
                size_t stream_start = json_text.find('{', cursor);
                if (stream_start == std::string::npos || stream_start >= arr_end) break;

                // Find matching closing brace taking nested objects into account
                int brace_depth = 0;
                size_t stream_end = std::string::npos;
                for (size_t i = stream_start; i <= arr_end; ++i) {
                    if (json_text[i] == '{') brace_depth++;
                    else if (json_text[i] == '}') {
                        brace_depth--;
                        if (brace_depth == 0) {
                            stream_end = i;
                            break;
                        }
                    }
                }
                if (stream_end == std::string::npos) break;

                std::string stream_json = json_text.substr(stream_start, stream_end - stream_start + 1);
                int index = static_cast<int>(extract_int_field(stream_json, "index", 0));
                StreamType type = stream_type_from_string(extract_string_field(stream_json, "type"));
                std::string codec_name = extract_string_field(stream_json, "codec_name");
                Rational timebase = extract_rational_field(stream_json, "timebase");
                int64_t duration_ticks = extract_int_field(stream_json, "duration_ticks", 0);

                SourceStream stream(index, type, std::move(codec_name), timebase, duration_ticks);

                if (type == StreamType::Video) {
                    VideoStreamInfo vi;
                    vi.width = static_cast<int>(extract_int_field(stream_json, "width", 0));
                    vi.height = static_cast<int>(extract_int_field(stream_json, "height", 0));
                    vi.frame_rate = extract_rational_field(stream_json, "frame_rate");
                    vi.pixel_aspect_ratio = extract_rational_field(stream_json, "pixel_aspect_ratio");
                    vi.pixel_format = extract_string_field(stream_json, "pixel_format");
                    vi.color_space = extract_string_field(stream_json, "color_space");
                    vi.color_range = extract_string_field(stream_json, "color_range");
                    vi.bit_depth = static_cast<int>(extract_int_field(stream_json, "bit_depth", 8));
                    stream.set_video_info(std::move(vi));
                } else if (type == StreamType::Audio) {
                    AudioStreamInfo ai;
                    ai.sample_rate = static_cast<int>(extract_int_field(stream_json, "sample_rate", 48000));
                    ai.channels = static_cast<int>(extract_int_field(stream_json, "channels", 2));
                    ai.channel_layout = extract_string_field(stream_json, "channel_layout");
                    ai.sample_format = extract_string_field(stream_json, "sample_format");
                    ai.bit_depth = static_cast<int>(extract_int_field(stream_json, "bit_depth", 16));
                    stream.set_audio_info(std::move(ai));
                }

                streams.push_back(std::move(stream));
                cursor = stream_end + 1;
            }
        }
    }

    return MediaAsset(std::move(id),
                      std::filesystem::path(file_path),
                      std::move(display_name),
                      file_size,
                      std::move(container_format),
                      duration,
                      std::move(streams));
}

} // namespace ave

