#include "ave/render_node.hpp"

namespace ave {

std::string to_string(NodeType type) {
    switch (type) {
        case NodeType::Source:    return "Source";
        case NodeType::Transform: return "Transform";
        case NodeType::Opacity:   return "Opacity";
        case NodeType::Blend:     return "Blend";
        case NodeType::Output:    return "Output";
        case NodeType::Custom:    return "Custom";
    }
    return "Unknown";
}

RenderNode::RenderNode(NodeType type, std::string name)
    : type_(type),
      name_(name.empty() ? to_string(type) : std::move(name)) {}

} // namespace ave

