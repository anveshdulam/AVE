#pragma once

#include "ave/project.hpp"
#include <string>
#include <functional>

namespace ave {

/**
 * @brief ViewModel for the inspector panel displaying and mutating
 * selected clip properties through validated Command Bus transactions.
 */
class InspectorViewModel {
public:
    explicit InspectorViewModel(Project& project);

    void set_selected_clip_id(std::string clip_id);
    void clear_selection();

    [[nodiscard]] bool has_selection() const;
    [[nodiscard]] const std::string& clip_id() const noexcept { return selected_clip_id_; }
    [[nodiscard]] std::string name() const;
    [[nodiscard]] std::string track_id() const;
    [[nodiscard]] std::string asset_id() const;

    [[nodiscard]] Rational timeline_in() const;
    [[nodiscard]] Rational duration() const;
    [[nodiscard]] Rational timeline_out() const;

    [[nodiscard]] float transform_x() const;
    [[nodiscard]] float transform_y() const;
    [[nodiscard]] float scale_x() const;
    [[nodiscard]] float scale_y() const;
    [[nodiscard]] float rotation() const;
    [[nodiscard]] float anchor_x() const;
    [[nodiscard]] float anchor_y() const;
    [[nodiscard]] float opacity() const;
    [[nodiscard]] std::string blend_mode() const;

    // Mutators dispatched via SetTransformCommand
    bool set_position(float x, float y);
    bool set_scale(float sx, float sy);
    bool set_rotation(float degrees);
    bool set_anchor(float ax, float ay);
    bool set_opacity(float opacity);
    bool set_blend_mode(std::string mode);

    void set_on_changed(std::function<void()> callback) { on_changed_ = std::move(callback); }

private:
    Project& project_;
    std::string selected_clip_id_{};
    std::function<void()> on_changed_{};

    [[nodiscard]] const TimelineItem* current_item() const;
    [[nodiscard]] std::string current_track_id() const;
    void notify_changed();
};

} // namespace ave
