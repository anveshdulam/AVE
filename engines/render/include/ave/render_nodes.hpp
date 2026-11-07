#pragma once

#include "ave/render_node.hpp"

namespace ave {

/**
 * @brief Ingests an external image (e.g. decoded video frame) or generates a solid color plate.
 */
class SourceNode : public RenderNode {
public:
    SourceNode(std::string name = "Source");

    [[nodiscard]] size_t input_port_count() const noexcept override { return 0; }

    void set_image(RenderImage img);
    void set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

    void execute(const std::vector<const RenderImage*>& inputs,
                 RenderImage& output,
                 const RenderContext& ctx) override;

private:
    RenderImage image_{};
    bool is_solid_{false};
    uint8_t solid_r_{0};
    uint8_t solid_g_{0};
    uint8_t solid_b_{0};
    uint8_t solid_a_{255};
};

/**
 * @brief Applies 2D spatial transformation (translation, scale, rotation around anchor point).
 */
class TransformNode : public RenderNode {
public:
    TransformNode(std::string name = "Transform");

    [[nodiscard]] size_t input_port_count() const noexcept override { return 1; }

    void set_translation(float tx, float ty) noexcept { tx_ = tx; ty_ = ty; }
    void set_scale(float sx, float sy) noexcept { sx_ = (sx != 0.0f) ? sx : 0.0001f; sy_ = (sy != 0.0f) ? sy : 0.0001f; }
    void set_rotation(float degrees) noexcept { rotation_deg_ = degrees; }
    void set_anchor(float ax, float ay) noexcept { ax_ = ax; ay_ = ay; }

    [[nodiscard]] float translation_x() const noexcept { return tx_; }
    [[nodiscard]] float translation_y() const noexcept { return ty_; }
    [[nodiscard]] float scale_x() const noexcept { return sx_; }
    [[nodiscard]] float scale_y() const noexcept { return sy_; }
    [[nodiscard]] float rotation_deg() const noexcept { return rotation_deg_; }
    [[nodiscard]] float anchor_x() const noexcept { return ax_; }
    [[nodiscard]] float anchor_y() const noexcept { return ay_; }

    void execute(const std::vector<const RenderImage*>& inputs,
                 RenderImage& output,
                 const RenderContext& ctx) override;

private:
    float tx_{0.0f};
    float ty_{0.0f};
    float sx_{1.0f};
    float sy_{1.0f};
    float rotation_deg_{0.0f};
    float ax_{0.5f}; // Normalized center
    float ay_{0.5f};
};

/**
 * @brief Modulates the alpha channel by a scalar opacity factor [0.0, 1.0].
 */
class OpacityNode : public RenderNode {
public:
    explicit OpacityNode(float opacity = 1.0f, std::string name = "Opacity");

    [[nodiscard]] size_t input_port_count() const noexcept override { return 1; }

    void set_opacity(float opacity) noexcept { opacity_ = std::clamp(opacity, 0.0f, 1.0f); }
    [[nodiscard]] float opacity() const noexcept { return opacity_; }

    void execute(const std::vector<const RenderImage*>& inputs,
                 RenderImage& output,
                 const RenderContext& ctx) override;

private:
    float opacity_{1.0f};
};

/**
 * @brief Blends a foreground layer over a background layer using Porter-Duff compositing.
 */
class BlendNode : public RenderNode {
public:
    explicit BlendNode(BlendMode mode = BlendMode::Over, std::string name = "Blend");

    [[nodiscard]] size_t input_port_count() const noexcept override { return 2; } // 0: Bg, 1: Fg

    void set_blend_mode(BlendMode mode) noexcept { mode_ = mode; }
    [[nodiscard]] BlendMode blend_mode() const noexcept { return mode_; }

    void execute(const std::vector<const RenderImage*>& inputs,
                 RenderImage& output,
                 const RenderContext& ctx) override;

private:
    BlendMode mode_{BlendMode::Over};
};

/**
 * @brief Collector node representing the final output surface of the render graph.
 */
class OutputNode : public RenderNode {
public:
    OutputNode(std::string name = "Output");

    [[nodiscard]] size_t input_port_count() const noexcept override { return 1; }

    void execute(const std::vector<const RenderImage*>& inputs,
                 RenderImage& output,
                 const RenderContext& ctx) override;
};

} // namespace ave

