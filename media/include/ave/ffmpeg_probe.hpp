#pragma once

#include "ave/media_asset.hpp"

#include <filesystem>
#include <string>
#include <optional>

namespace ave {

struct ProbeResult {
    bool success{false};
    std::string error_message{};
    std::optional<MediaAsset> asset{std::nullopt};

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

/**
 * @brief Probes a media container file using FFmpeg and constructs an immutable MediaAsset.
 *
 * Extracts container metadata, stream parameters, rational timebases, frame rates,
 * pixel formats, color metadata, and audio channel layouts.
 */
class FFmpegProbe {
public:
    [[nodiscard]] static ProbeResult probe(const std::filesystem::path& file_path);

    [[nodiscard]] static std::string ffmpeg_version_info();
};

} // namespace ave

