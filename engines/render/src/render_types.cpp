#include "ave/render_types.hpp"

#include <cmath>
#include <algorithm>

namespace ave {

std::string to_string(RenderFormat format) {
    switch (format) {
        case RenderFormat::RGBA8_UNORM:  return "RGBA8_UNORM";
        case RenderFormat::RGBA16_SFLOAT: return "RGBA16_SFLOAT";
        case RenderFormat::RGBA32_SFLOAT: return "RGBA32_SFLOAT";
    }
    return "Unknown";
}

std::string to_string(ColorSpace space) {
    switch (space) {
        case ColorSpace::Rec709:       return "Rec709";
        case ColorSpace::sRGB:         return "sRGB";
        case ColorSpace::LinearRec709: return "LinearRec709";
        case ColorSpace::ACEScg:       return "ACEScg";
    }
    return "Unknown";
}

std::string to_string(BlendMode mode) {
    switch (mode) {
        case BlendMode::Over:     return "Over";
        case BlendMode::Multiply: return "Multiply";
        case BlendMode::Screen:   return "Screen";
        case BlendMode::Add:      return "Add";
    }
    return "Unknown";
}

RenderImage::RenderImage(int width, int height, RenderFormat format, ColorSpace color_space)
    : width_(width > 0 ? width : 0),
      height_(height > 0 ? height : 0),
      channels_(4),
      format_(format),
      color_space_(color_space) {
    size_t bytes_per_pixel = 4; // Default RGBA8
    if (format_ == RenderFormat::RGBA16_SFLOAT) bytes_per_pixel = 8;
    else if (format_ == RenderFormat::RGBA32_SFLOAT) bytes_per_pixel = 16;

    data_.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_) * bytes_per_pixel, 0);
}

RenderImage::RenderImage(int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a, ColorSpace color_space)
    : RenderImage(width, height, RenderFormat::RGBA8_UNORM, color_space) {
    clear(r, g, b, a);
}

void RenderImage::resize(int width, int height, RenderFormat format, ColorSpace color_space) {
    width_ = width > 0 ? width : 0;
    height_ = height > 0 ? height : 0;
    format_ = format;
    color_space_ = color_space;

    size_t bytes_per_pixel = 4;
    if (format_ == RenderFormat::RGBA16_SFLOAT) bytes_per_pixel = 8;
    else if (format_ == RenderFormat::RGBA32_SFLOAT) bytes_per_pixel = 16;

    data_.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_) * bytes_per_pixel, 0);
}

void RenderImage::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept {
    if (format_ != RenderFormat::RGBA8_UNORM) return;

    size_t pixel_count = static_cast<size_t>(width_) * static_cast<size_t>(height_);
    for (size_t i = 0; i < pixel_count; ++i) {
        data_[i * 4 + 0] = r;
        data_[i * 4 + 1] = g;
        data_[i * 4 + 2] = b;
        data_[i * 4 + 3] = a;
    }
}

void RenderImage::get_pixel(int x, int y, uint8_t& out_r, uint8_t& out_g, uint8_t& out_b, uint8_t& out_a) const noexcept {
    if (x < 0 || x >= width_ || y < 0 || y >= height_ || format_ != RenderFormat::RGBA8_UNORM) {
        out_r = out_g = out_b = out_a = 0;
        return;
    }

    size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)) * 4;
    out_r = data_[idx + 0];
    out_g = data_[idx + 1];
    out_b = data_[idx + 2];
    out_a = data_[idx + 3];
}

void RenderImage::set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept {
    if (x < 0 || x >= width_ || y < 0 || y >= height_ || format_ != RenderFormat::RGBA8_UNORM) {
        return;
    }

    size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)) * 4;
    data_[idx + 0] = r;
    data_[idx + 1] = g;
    data_[idx + 2] = b;
    data_[idx + 3] = a;
}

void RenderImage::sample_bilinear(float u, float v, float& out_r, float& out_g, float& out_b, float& out_a) const noexcept {
    if (width_ <= 0 || height_ <= 0 || u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
        out_r = out_g = out_b = out_a = 0.0f;
        return;
    }

    float px = u * static_cast<float>(width_ - 1);
    float py = v * static_cast<float>(height_ - 1);

    int x0 = static_cast<int>(std::floor(px));
    int y0 = static_cast<int>(std::floor(py));
    int x1 = std::min(x0 + 1, width_ - 1);
    int y1 = std::min(y0 + 1, height_ - 1);

    float dx = px - static_cast<float>(x0);
    float dy = py - static_cast<float>(y0);

    uint8_t r00, g00, b00, a00;
    uint8_t r10, g10, b10, a10;
    uint8_t r01, g01, b01, a01;
    uint8_t r11, g11, b11, a11;

    get_pixel(x0, y0, r00, g00, b00, a00);
    get_pixel(x1, y0, r10, g10, b10, a10);
    get_pixel(x0, y1, r01, g01, b01, a01);
    get_pixel(x1, y1, r11, g11, b11, a11);

    float w00 = (1.0f - dx) * (1.0f - dy);
    float w10 = dx * (1.0f - dy);
    float w01 = (1.0f - dx) * dy;
    float w11 = dx * dy;

    out_r = (w00 * r00 + w10 * r10 + w01 * r01 + w11 * r11) / 255.0f;
    out_g = (w00 * g00 + w10 * g10 + w01 * g01 + w11 * g11) / 255.0f;
    out_b = (w00 * b00 + w10 * b10 + w01 * b01 + w11 * b11) / 255.0f;
    out_a = (w00 * a00 + w10 * a10 + w01 * a01 + w11 * a11) / 255.0f;
}

RenderImage RenderImage::from_video_frame(const VideoFrame& frame) {
    RenderImage img(frame.width, frame.height, RenderFormat::RGBA8_UNORM, ColorSpace::Rec709);
    if (!frame.rgba_data.empty() && frame.rgba_data.size() == img.size_bytes()) {
        std::copy(frame.rgba_data.begin(), frame.rgba_data.end(), img.data());
    }
    return img;
}

VideoFrame RenderImage::to_video_frame(Rational pts, Rational duration, uint64_t seek_gen) const {
    VideoFrame vf;
    vf.width = width_;
    vf.height = height_;
    vf.pts = pts;
    vf.duration = duration;
    vf.seek_generation = seek_gen;
    vf.is_keyframe = true;
    vf.rgba_data = data_;
    return vf;
}

} // namespace ave

