#pragma once

#include "ave/time.hpp"
#include "ave/uuid.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdint>
#include <memory>

namespace ave {

enum class StreamType {
    Unknown,
    Video,
    Audio,
    Subtitle,
    Data
};

[[nodiscard]] std::string to_string(StreamType type);
[[nodiscard]] StreamType stream_type_from_string(std::string_view str);

struct VideoStreamInfo {
    int width{0};
    int height{0};
    Rational frame_rate{24, 1};
    Rational pixel_aspect_ratio{1, 1};
    std::string pixel_format{"yuv420p"};
    std::string color_space{"bt709"};
    std::string color_range{"limited"};
    int bit_depth{8};

    [[nodiscard]] bool operator==(const VideoStreamInfo& other) const = default;
};

struct AudioStreamInfo {
    int sample_rate{48000};
    int channels{2};
    std::string channel_layout{"stereo"};
    std::string sample_format{"fltp"};
    int bit_depth{16};

    [[nodiscard]] bool operator==(const AudioStreamInfo& other) const = default;
};

class SourceStream {
public:
    SourceStream() = default;
    SourceStream(int index, StreamType type, std::string codec_name,
                 Rational timebase, int64_t duration_ticks);

    [[nodiscard]] int index() const noexcept { return index_; }
    [[nodiscard]] StreamType type() const noexcept { return type_; }
    [[nodiscard]] const std::string& codec_name() const noexcept { return codec_name_; }
    [[nodiscard]] Rational timebase() const noexcept { return timebase_; }
    [[nodiscard]] int64_t duration_ticks() const noexcept { return duration_ticks_; }
    [[nodiscard]] Rational duration_seconds() const noexcept {
        return timebase_ * duration_ticks_;
    }

    [[nodiscard]] const std::optional<VideoStreamInfo>& video_info() const noexcept { return video_info_; }
    [[nodiscard]] const std::optional<AudioStreamInfo>& audio_info() const noexcept { return audio_info_; }

    void set_video_info(VideoStreamInfo info) {
        type_ = StreamType::Video;
        video_info_ = std::move(info);
    }

    void set_audio_info(AudioStreamInfo info) {
        type_ = StreamType::Audio;
        audio_info_ = std::move(info);
    }

    [[nodiscard]] bool operator==(const SourceStream& other) const = default;

private:
    int index_{0};
    StreamType type_{StreamType::Unknown};
    std::string codec_name_{"unknown"};
    Rational timebase_{1, 1000};
    int64_t duration_ticks_{0};
    std::optional<VideoStreamInfo> video_info_{std::nullopt};
    std::optional<AudioStreamInfo> audio_info_{std::nullopt};
};

class MediaAsset {
public:
    MediaAsset() = default;

    MediaAsset(std::string id,
               std::filesystem::path file_path,
               std::string display_name,
               uint64_t file_size_bytes,
               std::string container_format,
               Rational duration_seconds,
               std::vector<SourceStream> streams);

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::filesystem::path& file_path() const noexcept { return file_path_; }
    [[nodiscard]] const std::string& display_name() const noexcept { return display_name_; }
    [[nodiscard]] uint64_t file_size_bytes() const noexcept { return file_size_bytes_; }
    [[nodiscard]] const std::string& container_format() const noexcept { return container_format_; }
    [[nodiscard]] Rational duration() const noexcept { return duration_seconds_; }
    [[nodiscard]] const std::vector<SourceStream>& streams() const noexcept { return streams_; }

    void set_file_path(std::filesystem::path path) { file_path_ = std::move(path); }
    void set_file_size_bytes(uint64_t size) noexcept { file_size_bytes_ = size; }

    [[nodiscard]] const SourceStream* primary_video_stream() const noexcept;
    [[nodiscard]] const SourceStream* primary_audio_stream() const noexcept;

    [[nodiscard]] std::vector<const SourceStream*> video_streams() const;
    [[nodiscard]] std::vector<const SourceStream*> audio_streams() const;

    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] static MediaAsset from_json(const std::string& json_text);

    [[nodiscard]] bool operator==(const MediaAsset& other) const = default;

private:
    std::string id_{UUID::generate()};
    std::filesystem::path file_path_{};
    std::string display_name_{};
    uint64_t file_size_bytes_{0};
    std::string container_format_{"unknown"};
    Rational duration_seconds_{0, 1};
    std::vector<SourceStream> streams_{};
};

} // namespace ave

