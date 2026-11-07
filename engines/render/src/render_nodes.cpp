#include "ave/render_nodes.hpp"

#include <cmath>
#include <algorithm>

namespace ave {

namespace {
constexpr float kPi = 3.14159265358979323846f;

inline uint8_t to_byte(float v) noexcept {
    int iv = static_cast<int>(std::round(v * 255.0f));
    return static_cast<uint8_t>(std::clamp(iv, 0, 255));
}
} // namespace

// --- SourceNode ---

SourceNode::SourceNode(std::string name)
    : RenderNode(NodeType::Source, std::move(name)) {}

void SourceNode::set_image(RenderImage img) {
    image_ = std::move(img);
    is_solid_ = false;
}

void SourceNode::set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    is_solid_ = true;
    solid_r_ = r;
    solid_g_ = g;
    solid_b_ = b;
    solid_a_ = a;
}

void SourceNode::execute(const std::vector<const RenderImage*>&,
                        RenderImage& output,
                        const RenderContext& ctx) {
    if (is_solid_) {
        output.resize(ctx.target_width, ctx.target_height, RenderFormat::RGBA8_UNORM, ctx.color_space);
        output.clear(solid_r_, solid_g_, solid_b_, solid_a_);
    } else {
        output = image_;
    }
}

// --- TransformNode ---

TransformNode::TransformNode(std::string name)
    : RenderNode(NodeType::Transform, std::move(name)) {}

void TransformNode::execute(const std::vector<const RenderImage*>& inputs,
                           RenderImage& output,
                           const RenderContext& ctx) {
    if (inputs.empty() || !inputs[0] || inputs[0]->width() <= 0 || inputs[0]->height() <= 0) {
        output.resize(ctx.target_width, ctx.target_height, RenderFormat::RGBA8_UNORM, ctx.color_space);
        output.clear(0, 0, 0, 0);
        return;
    }

    const RenderImage* src = inputs[0];
    output.resize(ctx.target_width, ctx.target_height, src->format(), src->color_space());

    float rad = rotation_deg_ * (kPi / 180.0f);
    float cos_neg = std::cos(-rad);
    float sin_neg = std::sin(-rad);

    float out_cx = ax_ * static_cast<float>(ctx.target_width) + tx_;
    float out_cy = ay_ * static_cast<float>(ctx.target_height) + ty_;

    float src_cx = ax_ * static_cast<float>(src->width());
    float src_cy = ay_ * static_cast<float>(src->height());

    float inv_w = (src->width() > 1) ? 1.0f / static_cast<float>(src->width() - 1) : 0.0f;
    float inv_h = (src->height() > 1) ? 1.0f / static_cast<float>(src->height() - 1) : 0.0f;

    for (int y = 0; y < ctx.target_height; ++y) {
        float dy = static_cast<float>(y) - out_cy;
        for (int x = 0; x < ctx.target_width; ++x) {
            float dx = static_cast<float>(x) - out_cx;

            // Inverse rotation & inverse scale
            float rx = (dx * cos_neg - dy * sin_neg) / sx_;
            float ry = (dx * sin_neg + dy * cos_neg) / sy_;

            float sx_coord = rx + src_cx;
            float sy_coord = ry + src_cy;

            float u = sx_coord * inv_w;
            float v = sy_coord * inv_h;

            if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
                float r, g, b, a;
                src->sample_bilinear(u, v, r, g, b, a);
                output.set_pixel(x, y, to_byte(r), to_byte(g), to_byte(b), to_byte(a));
            } else {
                output.set_pixel(x, y, 0, 0, 0, 0);
            }
        }
    }
}

// --- OpacityNode ---

OpacityNode::OpacityNode(float opacity, std::string name)
    : RenderNode(NodeType::Opacity, std::move(name)),
      opacity_(std::clamp(opacity, 0.0f, 1.0f)) {}

void OpacityNode::execute(const std::vector<const RenderImage*>& inputs,
                         RenderImage& output,
                         const RenderContext&) {
    if (inputs.empty() || !inputs[0]) {
        return;
    }

    const RenderImage* src = inputs[0];
    output = *src;

    if (opacity_ >= 0.999f) return;

    for (int y = 0; y < output.height(); ++y) {
        for (int x = 0; x < output.width(); ++x) {
            uint8_t r, g, b, a;
            output.get_pixel(x, y, r, g, b, a);
            uint8_t new_a = static_cast<uint8_t>(std::round(static_cast<float>(a) * opacity_));
            output.set_pixel(x, y, r, g, b, new_a);
        }
    }
}

// --- BlendNode ---

BlendNode::BlendNode(BlendMode mode, std::string name)
    : RenderNode(NodeType::Blend, std::move(name)),
      mode_(mode) {}

void BlendNode::execute(const std::vector<const RenderImage*>& inputs,
                       RenderImage& output,
                       const RenderContext& ctx) {
    const RenderImage* bg = (inputs.size() > 0) ? inputs[0] : nullptr;
    const RenderImage* fg = (inputs.size() > 1) ? inputs[1] : nullptr;

    if (!bg && !fg) {
        output.resize(ctx.target_width, ctx.target_height, RenderFormat::RGBA8_UNORM, ctx.color_space);
        output.clear(0, 0, 0, 0);
        return;
    }

    if (!fg && bg) {
        output = *bg;
        return;
    }

    if (!bg && fg) {
        output = *fg;
        return;
    }

    output.resize(ctx.target_width, ctx.target_height, RenderFormat::RGBA8_UNORM, ctx.color_space);

    for (int y = 0; y < ctx.target_height; ++y) {
        for (int x = 0; x < ctx.target_width; ++x) {
            uint8_t b_r = 0, b_g = 0, b_b = 0, b_a = 0;
            uint8_t f_r = 0, f_g = 0, f_b = 0, f_a = 0;

            if (bg) bg->get_pixel(x, y, b_r, b_g, b_b, b_a);
            if (fg) fg->get_pixel(x, y, f_r, f_g, f_b, f_a);

            float br = b_r / 255.0f;
            float bg_c = b_g / 255.0f;
            float bb = b_b / 255.0f;
            float ba = b_a / 255.0f;

            float fr = f_r / 255.0f;
            float fg_c = f_g / 255.0f;
            float fb = f_b / 255.0f;
            float fa = f_a / 255.0f;

            float out_r = 0.0f, out_g = 0.0f, out_b = 0.0f, out_a = 0.0f;

            switch (mode_) {
                case BlendMode::Over: {
                    out_a = fa + ba * (1.0f - fa);
                    if (out_a > 1e-5f) {
                        out_r = (fr * fa + br * ba * (1.0f - fa)) / out_a;
                        out_g = (fg_c * fa + bg_c * ba * (1.0f - fa)) / out_a;
                        out_b = (fb * fa + bb * ba * (1.0f - fa)) / out_a;
                    }
                    break;
                }
                case BlendMode::Multiply: {
                    out_r = fr * br;
                    out_g = fg_c * bg_c;
                    out_b = fb * bb;
                    out_a = fa * ba;
                    break;
                }
                case BlendMode::Screen: {
                    out_r = 1.0f - (1.0f - fr) * (1.0f - br);
                    out_g = 1.0f - (1.0f - fg_c) * (1.0f - bg_c);
                    out_b = 1.0f - (1.0f - fb) * (1.0f - bb);
                    out_a = 1.0f - (1.0f - fa) * (1.0f - ba);
                    break;
                }
                case BlendMode::Add: {
                    out_r = std::min(1.0f, fr + br);
                    out_g = std::min(1.0f, fg_c + bg_c);
                    out_b = std::min(1.0f, fb + bb);
                    out_a = std::min(1.0f, fa + ba);
                    break;
                }
            }

            output.set_pixel(x, y, to_byte(out_r), to_byte(out_g), to_byte(out_b), to_byte(out_a));
        }
    }
}

// --- OutputNode ---

OutputNode::OutputNode(std::string name)
    : RenderNode(NodeType::Output, std::move(name)) {}

void OutputNode::execute(const std::vector<const RenderImage*>& inputs,
                         RenderImage& output,
                         const RenderContext& ctx) {
    if (inputs.empty() || !inputs[0]) {
        output.resize(ctx.target_width, ctx.target_height, RenderFormat::RGBA8_UNORM, ctx.color_space);
        output.clear(0, 0, 0, 0);
        return;
    }

    output = *inputs[0];
}

} // namespace ave
