#pragma once

#include "ave/render_types.hpp"
#include "ave/uuid.hpp"

#include <string>
#include <vector>
#include <memory>

namespace ave {

enum class NodeType {
    Source,
    Transform,
    Opacity,
    Blend,
    Output,
    Custom
};

[[nodiscard]] std::string to_string(NodeType type);

struct RenderContext {
    int target_width{1920};
    int target_height{1080};
    Rational timeline_time{0, 1};
    Rational frame_rate{24, 1};
    ColorSpace color_space{ColorSpace::Rec709};
};

/**
 * @brief Base abstract class for nodes in the Directed Acyclic Graph (DAG) compositing pipeline.
 */
class RenderNode {
public:
    explicit RenderNode(NodeType type, std::string name = "");
    virtual ~RenderNode() = default;

    RenderNode(const RenderNode&) = delete;
    RenderNode& operator=(const RenderNode&) = delete;

    [[nodiscard]] NodeType type() const noexcept { return type_; }
    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void set_name(std::string name) { name_ = std::move(name); }

    [[nodiscard]] virtual size_t input_port_count() const noexcept = 0;
    [[nodiscard]] virtual size_t output_port_count() const noexcept { return 1; }

    /**
     * @brief Evaluates node transformation, reading from input images and writing to output.
     */
    virtual void execute(const std::vector<const RenderImage*>& inputs,
                         RenderImage& output,
                         const RenderContext& ctx) = 0;

    [[nodiscard]] RenderImage& output_image() noexcept { return output_image_; }
    [[nodiscard]] const RenderImage& output_image() const noexcept { return output_image_; }

protected:
    NodeType type_;
    std::string id_{UUID::generate()};
    std::string name_{"Node"};
    RenderImage output_image_{};
};

} // namespace ave

