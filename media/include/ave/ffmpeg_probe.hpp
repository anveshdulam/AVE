#pragma once

#include "ave/media_asset.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace ave {

struct ProbeResult {
    bool success{false};
    bool cancelled{false};
    std::string error_message{};
    std::optional<MediaAsset> asset{std::nullopt};

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

struct CustomProbeIo {
    void* opaque{nullptr};
    int (*read_packet)(void* opaque, uint8_t* buf, int buf_size){nullptr};
    int64_t (*seek)(void* opaque, int64_t offset, int whence){nullptr};
};

/**
 * @brief Probes a media container file using FFmpeg and constructs an immutable MediaAsset.
 *
 * Extracts container metadata, stream parameters, rational timebases, frame rates,
 * pixel formats, color metadata, and audio channel layouts. Supports cooperative
 * cancellation via AVIOInterruptCB.
 */
class FFmpegProbe {
public:
    [[nodiscard]] static ProbeResult probe(
        const std::filesystem::path& file_path,
        std::shared_ptr<std::atomic<bool>> cancel_state = nullptr);

    [[nodiscard]] static ProbeResult probe_custom_io(
        const CustomProbeIo& custom_io,
        const std::filesystem::path& logical_path,
        std::shared_ptr<std::atomic<bool>> cancel_state = nullptr);

    [[nodiscard]] static std::string ffmpeg_version_info();
};

} // namespace ave

