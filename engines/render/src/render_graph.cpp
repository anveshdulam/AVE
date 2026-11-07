#include "ave/render_graph.hpp"

#include <queue>
#include <algorithm>

namespace ave {

RenderGraph::RenderGraph() = default;

void RenderGraph::add_node_internal(std::unique_ptr<RenderNode> node) {
    if (!node) return;
    std::string nid = node->id();
    RenderNode* ptr = node.get();
    nodes_.push_back(std::move(node));
    node_map_[nid] = ptr;
    is_compiled_ = false;

    if (ptr->type() == NodeType::Output) {
        output_node_ = static_cast<OutputNode*>(ptr);
    }
}

RenderNode* RenderGraph::add_node(std::unique_ptr<RenderNode> node) {
    if (!node) return nullptr;
    RenderNode* ptr = node.get();
    add_node_internal(std::move(node));
    return ptr;
}

RenderNode* RenderGraph::find_node(const std::string& id) noexcept {
    auto it = node_map_.find(id);
    return (it != node_map_.end()) ? it->second : nullptr;
}

const RenderNode* RenderGraph::find_node(const std::string& id) const noexcept {
    auto it = node_map_.find(id);
    return (it != node_map_.end()) ? it->second : nullptr;
}

bool RenderGraph::connect(const std::string& from_node_id, const std::string& to_node_id, size_t to_port_index) {
    RenderNode* from = find_node(from_node_id);
    RenderNode* to = find_node(to_node_id);

    if (!from || !to) return false;
    if (to_port_index >= to->input_port_count()) return false;

    // Check if port already connected, replace if so
    for (auto& conn : connections_) {
        if (conn.to_node_id == to_node_id && conn.to_port_index == to_port_index) {
            conn.from_node_id = from_node_id;
            is_compiled_ = false;
            return true;
        }
    }

    connections_.push_back(RenderConnection{from_node_id, to_node_id, to_port_index});
    is_compiled_ = false;
    return true;
}

bool RenderGraph::compile() {
    execution_order_.clear();

    if (nodes_.empty()) {
        is_compiled_ = true;
        return true;
    }

    // In-degree map for Kahn's algorithm
    std::unordered_map<std::string, int> in_degrees;
    for (const auto& node : nodes_) {
        in_degrees[node->id()] = 0;
    }

    // Adjacency list: from_id -> list of to_id
    std::unordered_map<std::string, std::vector<std::string>> adj;
    for (const auto& conn : connections_) {
        in_degrees[conn.to_node_id]++;
        adj[conn.from_node_id].push_back(conn.to_node_id);
    }

    // Queue of nodes with 0 incoming dependencies
    std::queue<std::string> q;
    for (const auto& node : nodes_) {
        if (in_degrees[node->id()] == 0) {
            q.push(node->id());
        }
    }

    while (!q.empty()) {
        std::string curr = q.front();
        q.pop();

        RenderNode* node_ptr = node_map_[curr];
        execution_order_.push_back(node_ptr);

        auto it = adj.find(curr);
        if (it != adj.end()) {
            for (const auto& neighbor : it->second) {
                in_degrees[neighbor]--;
                if (in_degrees[neighbor] == 0) {
                    q.push(neighbor);
                }
            }
        }
    }

    // If topological sort does not include all nodes, cycle detected
    if (execution_order_.size() != nodes_.size()) {
        execution_order_.clear();
        is_compiled_ = false;
        throw std::runtime_error("RenderGraph validation failed: cyclic dependency detected");
    }

    is_compiled_ = true;
    return true;
}

const RenderImage* RenderGraph::execute(const RenderContext& ctx) {
    if (!is_compiled_) {
        compile();
    }

    for (RenderNode* node : execution_order_) {
        size_t port_count = node->input_port_count();
        std::vector<const RenderImage*> inputs(port_count, nullptr);

        for (const auto& conn : connections_) {
            if (conn.to_node_id == node->id()) {
                if (conn.to_port_index < port_count) {
                    RenderNode* src_node = node_map_[conn.from_node_id];
                    if (src_node) {
                        inputs[conn.to_port_index] = &src_node->output_image();
                    }
                }
            }
        }

        node->execute(inputs, node->output_image(), ctx);
    }

    if (output_node_) {
        return &output_node_->output_image();
    }

    return execution_order_.empty() ? nullptr : &execution_order_.back()->output_image();
}

void RenderGraph::clear() {
    nodes_.clear();
    node_map_.clear();
    connections_.clear();
    execution_order_.clear();
    output_node_ = nullptr;
    is_compiled_ = false;
}

std::unique_ptr<RenderGraph> RenderGraph::build_for_timeline(const Timeline& timeline,
                                                             Rational,
                                                             const std::vector<VideoFrame>& track_frames,
                                                             const RenderContext&) {
    auto graph = std::make_unique<RenderGraph>();
    const auto& v_tracks = timeline.video_tracks();

    if (v_tracks.empty()) {
        auto* out = graph->emplace_node<OutputNode>("Output");
        auto* black = graph->emplace_node<SourceNode>("Black_Solid");
        black->set_solid_color(0, 0, 0, 255);
        graph->connect(black->id(), out->id(), 0);
        graph->compile();
        return graph;
    }

    RenderNode* current_bottom = nullptr;

    for (size_t i = 0; i < v_tracks.size(); ++i) {
        std::string src_name = "V" + std::to_string(i + 1) + "_Source";
        auto* src_node = graph->emplace_node<SourceNode>(src_name);

        if (i < track_frames.size() && !track_frames[i].rgba_data.empty()) {
            src_node->set_image(RenderImage::from_video_frame(track_frames[i]));
        } else {
            src_node->set_solid_color(0, 0, 0, (i == 0) ? 255 : 0);
        }

        if (current_bottom == nullptr) {
            current_bottom = src_node;
        } else {
            // Blend higher track over lower track
            std::string blend_name = "Blend_V" + std::to_string(i + 1);
            auto* blend = graph->emplace_node<BlendNode>(BlendMode::Over, blend_name);
            graph->connect(current_bottom->id(), blend->id(), 0); // Port 0: Background
            graph->connect(src_node->id(), blend->id(), 1);       // Port 1: Foreground
            current_bottom = blend;
        }
    }

    auto* out = graph->emplace_node<OutputNode>("Output");
    if (current_bottom) {
        graph->connect(current_bottom->id(), out->id(), 0);
    }

    graph->compile();
    return graph;
}

} // namespace ave
