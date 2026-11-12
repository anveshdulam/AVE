#include "ave/media_asset.hpp"

#include <nlohmann/json.hpp>

#include <limits>
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

std::string path_to_utf8(const std::filesystem::path& p) {
    const std::u8string u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::filesystem::path utf8_to_path(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const auto* begin = reinterpret_cast<const char8_t*>(utf8.data());
    return std::filesystem::path(std::u8string(begin, begin + utf8.size()));
}

nlohmann::json rational_to_json(Rational r) {
    return nlohmann::json{{"num", r.numerator()}, {"den", r.denominator()}};
}

std::string require_non_empty_string(const nlohmann::json& obj, const char* key, const std::string& ctx) {
    if (!obj.contains(key) || !obj.at(key).is_string()) {
        throw std::runtime_error("MediaAsset validation error: " + ctx + " missing or invalid string '" + key + "'");
    }
    std::string val = obj.at(key).get<std::string>();
    if (val.empty()) {
        throw std::runtime_error("MediaAsset validation error: " + ctx + "." + key + " must not be empty");
    }
    return val;
}

std::string optional_string(const nlohmann::json& obj,
                            const char* key,
                            const std::string& default_val,
                            const std::string& ctx) {
    if (!obj.contains(key)) {
        return default_val;
    }
    if (!obj.at(key).is_string()) {
        throw std::runtime_error("MediaAsset validation error: " + ctx + "." + key + " must be a string");
    }
    return obj.at(key).get<std::string>();
}

int optional_int(const nlohmann::json& obj, const char* key, int default_val, const std::string& ctx) {
    if (!obj.contains(key)) {
        return default_val;
    }
    const auto& v = obj.at(key);
    if (!v.is_number_integer()) {
        throw std::runtime_error("MediaAsset validation error: " + ctx + "." + key + " must be an integer");
    }
    const int64_t val = v.get<int64_t>();
    if (val < std::numeric_limits<int>::min() || val > std::numeric_limits<int>::max()) {
        throw std::runtime_error("MediaAsset validation error: " + ctx + "." + key + " out of int range");
    }
    return static_cast<int>(val);
}

int64_t optional_int64(const nlohmann::json& obj, const char* key, int64_t default_val, const std::string& ctx) {
    if (!obj.contains(key)) {
        return default_val;
    }
    const auto& v = obj.at(key);
    if (!v.is_number_integer()) {
        throw std::runtime_error("MediaAsset validation error: " + ctx + "." + key + " must be an integer");
    }
    if (v.is_number_unsigned()) {
        const uint64_t uval = v.get<uint64_t>();
        if (uval > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            throw std::runtime_error("MediaAsset validation error: " + ctx + "." + key + " out of int64 range");
        }
        return static_cast<int64_t>(uval);
    }
    return v.get<int64_t>();
}

uint64_t optional_uint64(const nlohmann::json& obj, const char* key, uint64_t default_val, const std::string& ctx) {
    if (!obj.contains(key)) {
        return default_val;
    }
    const auto& v = obj.at(key);
    if (!v.is_number_integer()) {
        throw std::runtime_error("MediaAsset validation error: " + ctx + "." + key + " must be a non-negative integer");
    }
    if (v.is_number_unsigned()) {
        return v.get<uint64_t>();
    }
    const int64_t signed_val = v.get<int64_t>();
    if (signed_val < 0) {
        throw std::runtime_error("MediaAsset validation error: " + ctx + "." + key + " must be non-negative");
    }
    return static_cast<uint64_t>(signed_val);
}

Rational optional_rational(const nlohmann::json& obj,
                           const char* key,
                           Rational default_val,
                           const std::string& ctx) {
    if (!obj.contains(key)) {
        return default_val;
    }
    const auto& r = obj.at(key);
    if (!r.is_object() || !r.contains("num") || !r.contains("den")) {
        throw std::runtime_error("MediaAsset validation error: " + ctx + "." + key + " must be an object with 'num' and 'den'");
    }
    if (!r.at("num").is_number_integer() || !r.at("den").is_number_integer()) {
        throw std::runtime_error("MediaAsset validation error: " + ctx + "." + key + " 'num' and 'den' must be integers");
    }
    const int64_t num = r.at("num").get<int64_t>();
    const int64_t den = r.at("den").get<int64_t>();
    if (den <= 0) {
        throw std::runtime_error("MediaAsset validation error: " + ctx + "." + key + " denominator must be > 0");
    }
    return Rational{num, den};
}

StreamType parse_strict_stream_type(const std::string& s, const std::string& ctx) {
    if (s == "video") return StreamType::Video;
    if (s == "audio") return StreamType::Audio;
    if (s == "subtitle") return StreamType::Subtitle;
    if (s == "data") return StreamType::Data;
    if (s == "unknown") return StreamType::Unknown;
    throw std::runtime_error("MediaAsset validation error: " + ctx + ".type invalid ('" + s + "')");
}

} // namespace

std::string MediaAsset::to_json() const {
    nlohmann::json streams_arr = nlohmann::json::array();
    for (const auto& s : streams_) {
        nlohmann::json s_obj{
            {"index", s.index()},
            {"type", to_string(s.type())},
            {"codec_name", s.codec_name()},
            {"timebase", rational_to_json(s.timebase())},
            {"duration_ticks", s.duration_ticks()}
        };
        if (s.video_info().has_value()) {
            const auto& v = *s.video_info();
            s_obj["video_info"] = nlohmann::json{
                {"width", v.width},
                {"height", v.height},
                {"frame_rate", rational_to_json(v.frame_rate)},
                {"pixel_aspect_ratio", rational_to_json(v.pixel_aspect_ratio)},
                {"pixel_format", v.pixel_format},
                {"color_space", v.color_space},
                {"color_range", v.color_range},
                {"bit_depth", v.bit_depth}
            };
        }
        if (s.audio_info().has_value()) {
            const auto& a = *s.audio_info();
            s_obj["audio_info"] = nlohmann::json{
                {"sample_rate", a.sample_rate},
                {"channels", a.channels},
                {"channel_layout", a.channel_layout},
                {"sample_format", a.sample_format},
                {"bit_depth", a.bit_depth}
            };
        }
        streams_arr.push_back(std::move(s_obj));
    }

    nlohmann::json root{
        {"id", id_},
        {"file_path", path_to_utf8(file_path_)},
        {"display_name", display_name_},
        {"file_size_bytes", file_size_bytes_},
        {"container_format", container_format_},
        {"duration_seconds", rational_to_json(duration_seconds_)},
        {"streams", std::move(streams_arr)}
    };
    return root.dump(2);
}

MediaAsset MediaAsset::from_json(const std::string& json_text) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(std::string("MediaAsset JSON parse error: ") + e.what());
    }

    if (!root.is_object()) {
        throw std::runtime_error("MediaAsset validation error: root JSON value must be an object");
    }

    std::string id = require_non_empty_string(root, "id", "asset");
    std::string file_path_str = optional_string(root, "file_path", "", "asset");
    std::filesystem::path file_path = utf8_to_path(file_path_str);

    std::string default_display = file_path.has_filename() ? path_to_utf8(file_path.filename()) : "Untitled Media";
    std::string display_name = optional_string(root, "display_name", default_display, "asset");
    if (display_name.empty()) {
        display_name = default_display;
    }

    const uint64_t file_size = optional_uint64(root, "file_size_bytes", 0, "asset");
    std::string container_format = optional_string(root, "container_format", "unknown", "asset");
    if (container_format.empty()) {
        container_format = "unknown";
    }

    const Rational duration = optional_rational(root, "duration_seconds", Rational{0, 1}, "asset");
    if (duration.numerator() < 0) {
        throw std::runtime_error("MediaAsset validation error: asset.duration_seconds cannot be negative");
    }

    std::vector<SourceStream> streams;
    if (root.contains("streams")) {
        const auto& streams_arr = root.at("streams");
        if (!streams_arr.is_array()) {
            throw std::runtime_error("MediaAsset validation error: asset.streams must be an array");
        }
        streams.reserve(streams_arr.size());

        for (size_t i = 0; i < streams_arr.size(); ++i) {
            const std::string s_ctx = "asset.streams[" + std::to_string(i) + "]";
            const auto& s_obj = streams_arr[i];
            if (!s_obj.is_object()) {
                throw std::runtime_error("MediaAsset validation error: " + s_ctx + " must be an object");
            }

            const int index = optional_int(s_obj, "index", static_cast<int>(i), s_ctx);
            if (index < 0) {
                throw std::runtime_error("MediaAsset validation error: " + s_ctx + ".index must be >= 0");
            }

            const std::string type_str = optional_string(s_obj, "type", "unknown", s_ctx);
            const StreamType type = parse_strict_stream_type(type_str, s_ctx);

            std::string codec_name = optional_string(s_obj, "codec_name", "unknown", s_ctx);
            if (codec_name.empty()) {
                codec_name = "unknown";
            }

            const Rational timebase = optional_rational(s_obj, "timebase", Rational{1, 1000}, s_ctx);
            if (timebase.numerator() <= 0) {
                throw std::runtime_error("MediaAsset validation error: " + s_ctx + ".timebase must be positive");
            }

            const int64_t duration_ticks = optional_int64(s_obj, "duration_ticks", 0, s_ctx);
            if (duration_ticks < 0) {
                throw std::runtime_error("MediaAsset validation error: " + s_ctx + ".duration_ticks cannot be negative");
            }

            SourceStream stream(index, type, std::move(codec_name), timebase, duration_ticks);

            if (s_obj.contains("video_info")) {
                if (type != StreamType::Video) {
                    throw std::runtime_error("MediaAsset validation error: " + s_ctx + " has video_info on non-video stream");
                }
                const auto& v_obj = s_obj.at("video_info");
                if (!v_obj.is_object()) {
                    throw std::runtime_error("MediaAsset validation error: " + s_ctx + ".video_info must be an object");
                }
                const std::string v_ctx = s_ctx + ".video_info";

                VideoStreamInfo vi;
                vi.width = optional_int(v_obj, "width", 0, v_ctx);
                vi.height = optional_int(v_obj, "height", 0, v_ctx);
                if (vi.width < 0 || vi.height < 0) {
                    throw std::runtime_error("MediaAsset validation error: " + v_ctx + " dimensions cannot be negative");
                }
                vi.frame_rate = optional_rational(v_obj, "frame_rate", Rational{24, 1}, v_ctx);
                if (vi.frame_rate.numerator() < 0) {
                    throw std::runtime_error("MediaAsset validation error: " + v_ctx + ".frame_rate cannot be negative");
                }
                vi.pixel_aspect_ratio = optional_rational(v_obj, "pixel_aspect_ratio", Rational{1, 1}, v_ctx);
                if (vi.pixel_aspect_ratio.numerator() <= 0) {
                    throw std::runtime_error("MediaAsset validation error: " + v_ctx + ".pixel_aspect_ratio must be positive");
                }
                vi.pixel_format = optional_string(v_obj, "pixel_format", "yuv420p", v_ctx);
                if (vi.pixel_format.empty()) vi.pixel_format = "yuv420p";
                vi.color_space = optional_string(v_obj, "color_space", "bt709", v_ctx);
                if (vi.color_space.empty()) vi.color_space = "bt709";
                vi.color_range = optional_string(v_obj, "color_range", "limited", v_ctx);
                if (vi.color_range.empty()) vi.color_range = "limited";
                vi.bit_depth = optional_int(v_obj, "bit_depth", 8, v_ctx);
                if (vi.bit_depth < 0) {
                    throw std::runtime_error("MediaAsset validation error: " + v_ctx + ".bit_depth cannot be negative");
                }
                stream.set_video_info(std::move(vi));
            }

            if (s_obj.contains("audio_info")) {
                if (type != StreamType::Audio) {
                    throw std::runtime_error("MediaAsset validation error: " + s_ctx + " has audio_info on non-audio stream");
                }
                const auto& a_obj = s_obj.at("audio_info");
                if (!a_obj.is_object()) {
                    throw std::runtime_error("MediaAsset validation error: " + s_ctx + ".audio_info must be an object");
                }
                const std::string a_ctx = s_ctx + ".audio_info";

                AudioStreamInfo ai;
                ai.sample_rate = optional_int(a_obj, "sample_rate", 48000, a_ctx);
                if (ai.sample_rate <= 0) {
                    throw std::runtime_error("MediaAsset validation error: " + a_ctx + ".sample_rate must be > 0");
                }
                ai.channels = optional_int(a_obj, "channels", 2, a_ctx);
                if (ai.channels <= 0) {
                    throw std::runtime_error("MediaAsset validation error: " + a_ctx + ".channels must be > 0");
                }
                ai.channel_layout = optional_string(a_obj, "channel_layout", "stereo", a_ctx);
                if (ai.channel_layout.empty()) ai.channel_layout = "stereo";
                ai.sample_format = optional_string(a_obj, "sample_format", "fltp", a_ctx);
                if (ai.sample_format.empty()) ai.sample_format = "fltp";
                ai.bit_depth = optional_int(a_obj, "bit_depth", 16, a_ctx);
                if (ai.bit_depth <= 0) {
                    throw std::runtime_error("MediaAsset validation error: " + a_ctx + ".bit_depth must be > 0");
                }
                stream.set_audio_info(std::move(ai));
            }

            streams.push_back(std::move(stream));
        }
    }

    return MediaAsset(std::move(id),
                      std::move(file_path),
                      std::move(display_name),
                      file_size,
                      std::move(container_format),
                      duration,
                      std::move(streams));
}

} // namespace ave

