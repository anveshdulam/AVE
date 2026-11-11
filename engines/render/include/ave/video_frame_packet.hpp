#pragma once

#include "ave/render_types.hpp"
#include "ave/time.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ave {

enum class AlphaRepresentation {
    Straight,
    Premultiplied
};

[[nodiscard]] std::string to_string(AlphaRepresentation alpha_mode);

struct VideoFramePacket {
    uint64_t sequence_id{0};
    uint64_t seek_generation{0};
    Rational pts{0, 1};
    int width{0};
    int height{0};
    int stride_bytes{0};
    RenderFormat pixel_format{RenderFormat::RGBA8_UNORM};
    AlphaRepresentation alpha_mode{AlphaRepresentation::Straight};
    ColorSpace color_space{ColorSpace::sRGB}; // Preview SDR target encoded as sRGB IEC 61966-2-1
    std::vector<uint8_t> pixels{};
};

using VideoFramePacketPtr = std::shared_ptr<const VideoFramePacket>;

inline constexpr int kMaxPreviewWidth = 1280;
inline constexpr int kMaxPreviewHeight = 720;
inline constexpr int kDefaultPreviewWidth = 960;
inline constexpr int kDefaultPreviewHeight = 540;

[[nodiscard]] bool checked_mul_size_t(std::size_t a, std::size_t b, std::size_t& out) noexcept;

void clamp_preview_dimensions(int target_width,
                              int target_height,
                              int& out_width,
                              int& out_height,
                              int max_width = kMaxPreviewWidth,
                              int max_height = kMaxPreviewHeight) noexcept;

[[nodiscard]] bool validate_video_frame_packet(const VideoFramePacket& packet,
                                               std::string* error_out = nullptr) noexcept;

void convert_rgba8_to_srgb_preview(const uint8_t* src_rgba,
                                   uint8_t* dst_rgba,
                                   std::size_t pixel_count,
                                   ColorSpace src_color_space) noexcept;

[[nodiscard]] RenderImage convert_render_image_to_srgb_preview(const RenderImage& src);

[[nodiscard]] VideoFramePacket make_srgb_preview_packet(const RenderImage& image,
                                                        Rational pts,
                                                        uint64_t seek_generation = 0,
                                                        AlphaRepresentation alpha_mode = AlphaRepresentation::Straight);

} // namespace ave

