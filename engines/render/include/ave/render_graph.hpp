#pragma once

#include "ave/render_node.hpp"
#include "ave/render_nodes.hpp"
#include "ave/timeline.hpp"

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <stdexcept>

namespace ave {

struct RenderConnection {
    std::string from_node_id;
    std::string to_node_id;
    size_t to_port_index{0};

    [[nodiscard]] bool operator==(const RenderConnection& other) const = default;
};

/**
 * @brief Directed Acyclic Graph (DAG) manager for multi-layer video compositing.
 */
class RenderGraph {
public:
    RenderGraph();
    ~RenderGraph() = default;

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    RenderGraph(RenderGraph&&) noexcept = default;
    RenderGraph& operator=(RenderGraph&&) noexcept = default;

    /**
     * @brief Adds a node to the graph and takes ownership.
     */
    template <typename T, typename... Args>
    T* emplace_node(Args&&... args) {
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = node.get();
        add_node_internal(std::move(node));
        return ptr;
    }

    RenderNode* add_node(std::unique_ptr<RenderNode> node);

    [[nodiscard]] RenderNode* find_node(const std::string& id) noexcept;
    [[nodiscard]] const RenderNode* find_node(const std::string& id) const noexcept;

    [[nodiscard]] size_t node_count() const noexcept { return nodes_.size(); }
    [[nodiscard]] size_t connection_count() const noexcept { return connections_.size(); }

    /**
     * @brief Connects an output of from_node to an input port of to_node.
     */
    bool connect(const std::string& from_node_id, const std::string& to_node_id, size_t to_port_index = 0);

    /**
     * @brief Validates graph topology and builds execution schedule via topological sorting.
     * @throws std::runtime_error if cycle is detected or connection invalid.
     */
    bool compile();

    [[nodiscard]] bool is_compiled() const noexcept { return is_compiled_; }
    [[nodiscard]] const std::vector<RenderNode*>& execution_order() const noexcept { return execution_order_; }

    /**
     * @brief Executes the compiled graph and returns pointer to the final OutputNode image.
     */
    const RenderImage* execute(const RenderContext& ctx);

    void clear();

    /**
     * @brief Factory: constructs a compositing graph for a multi-track timeline snapshot at time.
     */
    static std::unique_ptr<RenderGraph> build_for_timeline(const Timeline& timeline,
                                                           Rational time,
                                                           const std::vector<VideoFrame>& track_frames,
                                                           const RenderContext& ctx);

private:
    std::vector<std::unique_ptr<RenderNode>> nodes_{};
    std::unordered_map<std::string, RenderNode*> node_map_{};
    std::vector<RenderConnection> connections_{};

    std::vector<RenderNode*> execution_order_{};
    OutputNode* output_node_{nullptr};
    bool is_compiled_{false};

    void add_node_internal(std::unique_ptr<RenderNode> node);
};

} // namespace ave

