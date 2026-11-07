#pragma once

#include "ave/time.hpp"
#include "ave/video_frame.hpp"

#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>

namespace ave {

enum class RenderFormat {
    RGBA8_UNORM,
    RGBA16_SFLOAT,
    RGBA32_SFLOAT
};

[[nodiscard]] std::string to_string(RenderFormat format);

enum class ColorSpace {
    Rec709,
    sRGB,
    LinearRec709,
    ACEScg
};

[[nodiscard]] std::string to_string(ColorSpace space);

enum class BlendMode {
    Over,      // Standard Porter-Duff Alpha Over
    Multiply,  // Multiplicative blending
    Screen,    // Inverse multiply (lighten)
    Add        // Additive linear blending
};

[[nodiscard]] std::string to_string(BlendMode mode);

/**
 * @brief Memory-backed 2D image buffer used for render graph inputs, intermediates, and outputs.
 */
class RenderImage {
public:
    RenderImage() = default;

    RenderImage(int width, int height,
                RenderFormat format = RenderFormat::RGBA8_UNORM,
                ColorSpace color_space = ColorSpace::Rec709);

    RenderImage(int width, int height,
                uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                ColorSpace color_space = ColorSpace::Rec709);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] int channels() const noexcept { return channels_; }
    [[nodiscard]] RenderFormat format() const noexcept { return format_; }
    [[nodiscard]] ColorSpace color_space() const noexcept { return color_space_; }
    [[nodiscard]] size_t size_bytes() const noexcept { return data_.size(); }

    [[nodiscard]] uint8_t* data() noexcept { return data_.data(); }
    [[nodiscard]] const uint8_t* data() const noexcept { return data_.data(); }

    void resize(int width, int height,
                RenderFormat format = RenderFormat::RGBA8_UNORM,
                ColorSpace color_space = ColorSpace::Rec709);

    void clear(uint8_t r = 0, uint8_t g = 0, uint8_t b = 0, uint8_t a = 0) noexcept;

    void get_pixel(int x, int y, uint8_t& out_r, uint8_t& out_g, uint8_t& out_b, uint8_t& out_a) const noexcept;
    void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept;

    /**
     * @brief Samples the image at normalized coordinates (u, v) in [0, 1] with bilinear filtering.
     * Normalized output components in [0.0f, 1.0f].
     */
    void sample_bilinear(float u, float v, float& out_r, float& out_g, float& out_b, float& out_a) const noexcept;

    /**
     * @brief Creates a RenderImage from a decoded VideoFrame (RGBA8).
     */
    static RenderImage from_video_frame(const VideoFrame& frame);

    /**
     * @brief Converts this RenderImage to a VideoFrame.
     */
    [[nodiscard]] VideoFrame to_video_frame(Rational pts, Rational duration, uint64_t seek_gen = 0) const;

private:
    int width_{0};
    int height_{0};
    int channels_{4};
    RenderFormat format_{RenderFormat::RGBA8_UNORM};
    ColorSpace color_space_{ColorSpace::Rec709};
    std::vector<uint8_t> data_{};
};

} // namespace ave
