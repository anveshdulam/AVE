#include "ave/inspector_view_model.hpp"

namespace ave {

InspectorViewModel::InspectorViewModel(Project& project)
    : project_(project) {}

void InspectorViewModel::set_selected_clip_id(std::string clip_id) {
    if (selected_clip_id_ != clip_id) {
        selected_clip_id_ = std::move(clip_id);
        notify_changed();
    }
}

void InspectorViewModel::clear_selection() {
    if (!selected_clip_id_.empty()) {
        selected_clip_id_.clear();
        notify_changed();
    }
}

bool InspectorViewModel::has_selection() const {
    return current_item() != nullptr;
}

const TimelineItem* InspectorViewModel::current_item() const {
    if (selected_clip_id_.empty()) return nullptr;
    return project_.timeline().find_item(selected_clip_id_);
}

std::string InspectorViewModel::current_track_id() const {
    if (selected_clip_id_.empty()) return "";
    const Track* track = project_.timeline().find_track_for_item(selected_clip_id_);
    return track ? track->id() : "";
}

std::string InspectorViewModel::name() const {
    const auto* it = current_item();
    return it ? it->name() : "";
}

std::string InspectorViewModel::track_id() const {
    return current_track_id();
}

std::string InspectorViewModel::asset_id() const {
    const auto* it = current_item();
    return it ? it->asset_id() : "";
}

Rational InspectorViewModel::timeline_in() const {
    const auto* it = current_item();
    return it ? it->timeline_in() : Rational{0, 1};
}

Rational InspectorViewModel::duration() const {
    const auto* it = current_item();
    return it ? it->duration() : Rational{0, 1};
}

Rational InspectorViewModel::timeline_out() const {
    const auto* it = current_item();
    return it ? it->timeline_out() : Rational{0, 1};
}

float InspectorViewModel::transform_x() const {
    const auto* it = current_item();
    return it ? it->transform_x() : 0.0f;
}

float InspectorViewModel::transform_y() const {
    const auto* it = current_item();
    return it ? it->transform_y() : 0.0f;
}

float InspectorViewModel::scale_x() const {
    const auto* it = current_item();
    return it ? it->scale_x() : 1.0f;
}

float InspectorViewModel::scale_y() const {
    const auto* it = current_item();
    return it ? it->scale_y() : 1.0f;
}

float InspectorViewModel::rotation() const {
    const auto* it = current_item();
    return it ? it->rotation() : 0.0f;
}

float InspectorViewModel::anchor_x() const {
    const auto* it = current_item();
    return it ? it->anchor_x() : 0.5f;
}

float InspectorViewModel::anchor_y() const {
    const auto* it = current_item();
    return it ? it->anchor_y() : 0.5f;
}

float InspectorViewModel::opacity() const {
    const auto* it = current_item();
    return it ? it->opacity() : 1.0f;
}

std::string InspectorViewModel::blend_mode() const {
    const auto* it = current_item();
    return it ? it->blend_mode() : "Over";
}

bool InspectorViewModel::set_position(float x, float y) {
    std::string trk = current_track_id();
    if (trk.empty() || selected_clip_id_.empty()) return false;

    TransformParams params;
    params.transform_x = x;
    params.transform_y = y;
    auto cmd = std::make_unique<SetTransformCommand>(trk, selected_clip_id_, params);
    auto res = project_.execute_command(std::move(cmd));
    if (res.is_success()) {
        notify_changed();
        return true;
    }
    return false;
}

bool InspectorViewModel::set_scale(float sx, float sy) {
    std::string trk = current_track_id();
    if (trk.empty() || selected_clip_id_.empty()) return false;

    TransformParams params;
    params.scale_x = sx;
    params.scale_y = sy;
    auto cmd = std::make_unique<SetTransformCommand>(trk, selected_clip_id_, params);
    auto res = project_.execute_command(std::move(cmd));
    if (res.is_success()) {
        notify_changed();
        return true;
    }
    return false;
}

bool InspectorViewModel::set_rotation(float degrees) {
    std::string trk = current_track_id();
    if (trk.empty() || selected_clip_id_.empty()) return false;

    TransformParams params;
    params.rotation = degrees;
    auto cmd = std::make_unique<SetTransformCommand>(trk, selected_clip_id_, params);
    auto res = project_.execute_command(std::move(cmd));
    if (res.is_success()) {
        notify_changed();
        return true;
    }
    return false;
}

bool InspectorViewModel::set_anchor(float ax, float ay) {
    std::string trk = current_track_id();
    if (trk.empty() || selected_clip_id_.empty()) return false;

    TransformParams params;
    params.anchor_x = ax;
    params.anchor_y = ay;
    auto cmd = std::make_unique<SetTransformCommand>(trk, selected_clip_id_, params);
    auto res = project_.execute_command(std::move(cmd));
    if (res.is_success()) {
        notify_changed();
        return true;
    }
    return false;
}

bool InspectorViewModel::set_opacity(float opacity) {
    std::string trk = current_track_id();
    if (trk.empty() || selected_clip_id_.empty()) return false;

    TransformParams params;
    params.opacity = opacity;
    auto cmd = std::make_unique<SetTransformCommand>(trk, selected_clip_id_, params);
    auto res = project_.execute_command(std::move(cmd));
    if (res.is_success()) {
        notify_changed();
        return true;
    }
    return false;
}

bool InspectorViewModel::set_blend_mode(std::string mode) {
    std::string trk = current_track_id();
    if (trk.empty() || selected_clip_id_.empty()) return false;

    TransformParams params;
    params.blend_mode = std::move(mode);
    auto cmd = std::make_unique<SetTransformCommand>(trk, selected_clip_id_, params);
    auto res = project_.execute_command(std::move(cmd));
    if (res.is_success()) {
        notify_changed();
        return true;
    }
    return false;
}

void InspectorViewModel::notify_changed() {
    if (on_changed_) {
        on_changed_();
    }
}

} // namespace ave
