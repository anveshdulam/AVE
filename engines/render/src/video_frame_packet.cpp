#include "ave/video_frame_packet.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace ave {
namespace {

uint8_t linear_unit_to_srgb_byte(float linear) noexcept {
    const float clamped = std::clamp(linear, 0.0f, 1.0f);
    const float srgb = (clamped <= 0.0031308f)
                           ? (clamped * 12.92f)
                           : (1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f);
    const int iv = static_cast<int>(std::lround(srgb * 255.0f));
    return static_cast<uint8_t>(std::clamp(iv, 0, 255));
}

float rec709_oetf_unit_to_linear(float v) noexcept {
    const float clamped = std::clamp(v, 0.0f, 1.0f);
    if (clamped < 0.081f) {
        return clamped / 4.5f;
    }
    return std::pow((clamped + 0.099f) / 1.099f, 1.0f / 0.45f);
}

const std::array<uint8_t, 256>& linear_to_srgb_lut() noexcept {
    static const std::array<uint8_t, 256> kLut = []() {
        std::array<uint8_t, 256> table{};
        for (std::size_t i = 0; i < table.size(); ++i) {
            table[i] = linear_unit_to_srgb_byte(static_cast<float>(i) / 255.0f);
        }
        table[0] = 0;
        table[255] = 255;
        return table;
    }();
    return kLut;
}

const std::array<uint8_t, 256>& rec709_to_srgb_lut() noexcept {
    static const std::array<uint8_t, 256> kLut = []() {
        std::array<uint8_t, 256> table{};
        for (std::size_t i = 0; i < table.size(); ++i) {
            const float lin = rec709_oetf_unit_to_linear(static_cast<float>(i) / 255.0f);
            table[i] = linear_unit_to_srgb_byte(lin);
        }
        table[0] = 0;
        table[255] = 255;
        return table;
    }();
    return kLut;
}

} // namespace

std::string to_string(AlphaRepresentation alpha_mode) {
    switch (alpha_mode) {
        case AlphaRepresentation::Straight:      return "Straight";
        case AlphaRepresentation::Premultiplied: return "Premultiplied";
    }
    return "Unknown";
}

bool checked_mul_size_t(std::size_t a, std::size_t b, std::size_t& out) noexcept {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    if (a > (std::numeric_limits<std::size_t>::max)() / b) {
        return false;
    }
    out = a * b;
    return true;
}

void clamp_preview_dimensions(int target_width,
                              int target_height,
                              int& out_width,
                              int& out_height,
                              int max_width,
                              int max_height) noexcept {
    const int eff_max_w = std::clamp(max_width, 2, kMaxPreviewWidth);
    const int eff_max_h = std::clamp(max_height, 2, kMaxPreviewHeight);

    if (target_width <= 0 || target_height <= 0) {
        out_width = std::min(kDefaultPreviewWidth, eff_max_w);
        out_height = std::min(kDefaultPreviewHeight, eff_max_h);
        return;
    }

    if (target_width <= eff_max_w && target_height <= eff_max_h) {
        out_width = target_width;
        out_height = target_height;
        return;
    }

    const double scale_x = static_cast<double>(eff_max_w) / static_cast<double>(target_width);
    const double scale_y = static_cast<double>(eff_max_h) / static_cast<double>(target_height);
    const double scale = std::min(scale_x, scale_y);

    int scaled_w = static_cast<int>(std::floor(static_cast<double>(target_width) * scale));
    int scaled_h = static_cast<int>(std::floor(static_cast<double>(target_height) * scale));
    if (scaled_w > 2 && (scaled_w & 1) != 0) {
        scaled_w -= 1;
    }
    if (scaled_h > 2 && (scaled_h & 1) != 0) {
        scaled_h -= 1;
    }
    out_width = std::clamp(scaled_w, 2, eff_max_w);
    out_height = std::clamp(scaled_h, 2, eff_max_h);
}

bool validate_video_frame_packet(const VideoFramePacket& packet,
                                 std::string* error_out) noexcept {
    const auto fail = [&](const char* msg) noexcept {
        if (error_out) {
            *error_out = msg;
        }
        return false;
    };

    if (packet.width <= 0 || packet.height <= 0) {
        return fail("VideoFramePacket has non-positive dimensions.");
    }
    if (packet.width > kMaxPreviewWidth || packet.height > kMaxPreviewHeight) {
        return fail("VideoFramePacket dimensions exceed 1280x720 preview cap.");
    }
    if (packet.pixel_format != RenderFormat::RGBA8_UNORM) {
        return fail("VideoFramePacket preview pixel_format must be RGBA8_UNORM.");
    }
    if (packet.color_space != ColorSpace::sRGB) {
        return fail("VideoFramePacket preview color_space must be IEC 61966-2-1 sRGB.");
    }
    if (packet.stride_bytes <= 0) {
        return fail("VideoFramePacket stride_bytes must be positive.");
    }

    std::size_t min_row_bytes = 0;
    if (!checked_mul_size_t(static_cast<std::size_t>(packet.width), 4u, min_row_bytes)) {
        return fail("VideoFramePacket row byte calculation overflowed.");
    }
    if (static_cast<std::size_t>(packet.stride_bytes) < min_row_bytes) {
        return fail("VideoFramePacket stride_bytes is smaller than width * 4.");
    }

    std::size_t required_bytes = 0;
    if (!checked_mul_size_t(static_cast<std::size_t>(packet.stride_bytes),
                            static_cast<std::size_t>(packet.height),
                            required_bytes)) {
        return fail("VideoFramePacket stride_bytes * height overflowed.");
    }
    if (packet.pixels.size() < required_bytes) {
        return fail("VideoFramePacket pixel buffer is smaller than stride_bytes * height.");
    }

    return true;
}

void convert_rgba8_to_srgb_preview(const uint8_t* src_rgba,
                                   uint8_t* dst_rgba,
                                   std::size_t pixel_count,
                                   ColorSpace src_color_space) noexcept {
    if (!src_rgba || !dst_rgba || pixel_count == 0) {
        return;
    }

    if (src_color_space == ColorSpace::sRGB) {
        if (src_rgba != dst_rgba) {
            std::memcpy(dst_rgba, src_rgba, pixel_count * 4u);
        }
        return;
    }

    const auto& lut = (src_color_space == ColorSpace::Rec709)
                          ? rec709_to_srgb_lut()
                          : linear_to_srgb_lut();

    for (std::size_t i = 0; i < pixel_count; ++i) {
        const std::size_t idx = i * 4u;
        dst_rgba[idx + 0] = lut[src_rgba[idx + 0]];
        dst_rgba[idx + 1] = lut[src_rgba[idx + 1]];
        dst_rgba[idx + 2] = lut[src_rgba[idx + 2]];
        dst_rgba[idx + 3] = src_rgba[idx + 3];
    }
}

RenderImage convert_render_image_to_srgb_preview(const RenderImage& src) {
    if (src.width() <= 0 || src.height() <= 0) {
        return RenderImage(0, 0, RenderFormat::RGBA8_UNORM, ColorSpace::sRGB);
    }

    if (src.format() != RenderFormat::RGBA8_UNORM) {
        // Explicit SDR normalization fallback for higher bit-depth linear/scene images.
        RenderImage out(src.width(), src.height(), RenderFormat::RGBA8_UNORM, ColorSpace::sRGB);
        const std::size_t pixel_count = static_cast<std::size_t>(src.width()) * static_cast<std::size_t>(src.height());
        if (src.format() == RenderFormat::RGBA32_SFLOAT &&
            src.size_bytes() >= pixel_count * 16u) {
            const auto* fdata = reinterpret_cast<const float*>(src.data());
            uint8_t* dst = out.data();
            for (std::size_t i = 0; i < pixel_count; ++i) {
                dst[i * 4 + 0] = linear_unit_to_srgb_byte(fdata[i * 4 + 0]);
                dst[i * 4 + 1] = linear_unit_to_srgb_byte(fdata[i * 4 + 1]);
                dst[i * 4 + 2] = linear_unit_to_srgb_byte(fdata[i * 4 + 2]);
                const float a = std::clamp(fdata[i * 4 + 3], 0.0f, 1.0f);
                dst[i * 4 + 3] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(a * 255.0f)), 0, 255));
            }
        }
        return out;
    }

    RenderImage out(src.width(), src.height(), RenderFormat::RGBA8_UNORM, ColorSpace::sRGB);
    const std::size_t pixel_count = static_cast<std::size_t>(src.width()) * static_cast<std::size_t>(src.height());
    if (src.size_bytes() >= pixel_count * 4u) {
        convert_rgba8_to_srgb_preview(src.data(), out.data(), pixel_count, src.color_space());
    }
    return out;
}

VideoFramePacket make_srgb_preview_packet(const RenderImage& image,
                                          Rational pts,
                                          uint64_t seek_generation,
                                          AlphaRepresentation alpha_mode) {
    VideoFramePacket packet;
    if (image.width() <= 0 || image.height() <= 0) {
        return packet;
    }

    int clamped_w = 0;
    int clamped_h = 0;
    clamp_preview_dimensions(image.width(), image.height(), clamped_w, clamped_h);

    const RenderImage srgb_src = convert_render_image_to_srgb_preview(image);
    packet.seek_generation = seek_generation;
    packet.pts = pts;
    packet.width = clamped_w;
    packet.height = clamped_h;
    packet.stride_bytes = clamped_w * 4;
    packet.pixel_format = RenderFormat::RGBA8_UNORM;
    packet.alpha_mode = alpha_mode;
    packet.color_space = ColorSpace::sRGB;

    std::size_t byte_count = 0;
    if (!checked_mul_size_t(static_cast<std::size_t>(packet.stride_bytes),
                            static_cast<std::size_t>(packet.height),
                            byte_count)) {
        packet.width = 0;
        packet.height = 0;
        packet.stride_bytes = 0;
        return packet;
    }

    if (clamped_w == srgb_src.width() && clamped_h == srgb_src.height() &&
        srgb_src.size_bytes() >= byte_count) {
        packet.pixels.assign(srgb_src.data(), srgb_src.data() + byte_count);
    } else {
        // Bilinearly downsample into clamped preview bounds
        packet.pixels.resize(byte_count);
        for (int y = 0; y < clamped_h; ++y) {
            const float v = (clamped_h > 1)
                                ? (static_cast<float>(y) / static_cast<float>(clamped_h - 1))
                                : 0.0f;
            for (int x = 0; x < clamped_w; ++x) {
                const float u = (clamped_w > 1)
                                    ? (static_cast<float>(x) / static_cast<float>(clamped_w - 1))
                                    : 0.0f;
                float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
                srgb_src.sample_bilinear(u, v, r, g, b, a);
                const std::size_t dst_idx =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(clamped_w) +
                     static_cast<std::size_t>(x)) * 4u;
                packet.pixels[dst_idx + 0] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(r * 255.0f)), 0, 255));
                packet.pixels[dst_idx + 1] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(g * 255.0f)), 0, 255));
                packet.pixels[dst_idx + 2] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(b * 255.0f)), 0, 255));
                packet.pixels[dst_idx + 3] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(a * 255.0f)), 0, 255));
            }
        }
    }

    return packet;
}

} // namespace ave

