#include "ave/inspector_service.hpp"

#include "ave/project.hpp"
#include "ave/timecode.hpp"
#include "ave/timeline.hpp"
#include "ave/timeline_model.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include <QPointer>

namespace ave::desktop {
namespace {

constexpr double kNoopDeltaThreshold = 1e-4;
constexpr double kPropertyCacheEpsilon = 1e-6;

ave::Rational safe_inspector_fps(const ave::Timeline& tl) noexcept {
    const ave::Rational fps = tl.frame_rate();
    return (fps.numerator() > 0 && fps.denominator() > 0) ? fps : ave::Rational{24, 1};
}

bool is_supported_blend_mode(const QString& mode) noexcept {
    return mode == QStringLiteral("Over") ||
           mode == QStringLiteral("Multiply") ||
           mode == QStringLiteral("Screen") ||
           mode == QStringLiteral("Add");
}

QString format_clip_duration(const ave::TimelineItem& item, ave::Rational fps) {
    try {
        return QString::fromStdString(ave::Timecode::from_rational(item.duration(), fps).to_string());
    } catch (const std::exception&) {
        return QStringLiteral("00:00:00:00");
    }
}

} // namespace

InspectorService::InspectorService(ave::Project& project,
                                   TimelineModel& timeline_model,
                                   QObject* parent)
    : QObject(parent),
      project_(project),
      timeline_model_(timeline_model) {
    connect(&timeline_model_, &TimelineModel::selectedClipIdChanged,
            this, &InspectorService::refreshFromProject);
    connect(&timeline_model_, &TimelineModel::projectReloaded,
            this, &InspectorService::handle_project_reloaded);

    bind_history_listener();
    refreshFromProject();
}

InspectorService::~InspectorService() {
    unbind_history_listener();
}

bool InspectorService::hasSelection() const noexcept {
    return has_selection_;
}

QString InspectorService::clipName() const {
    return clip_name_;
}

QString InspectorService::trackName() const {
    return track_name_;
}

QString InspectorService::durationTimecode() const {
    return duration_timecode_;
}

double InspectorService::posX() const noexcept {
    return pos_x_;
}

double InspectorService::posY() const noexcept {
    return pos_y_;
}

double InspectorService::scaleX() const noexcept {
    return scale_x_;
}

double InspectorService::scaleY() const noexcept {
    return scale_y_;
}

double InspectorService::opacity() const noexcept {
    return opacity_;
}

QString InspectorService::blendMode() const {
    return blend_mode_;
}

void InspectorService::setPosX(double x) {
    if (!std::isfinite(x)) {
        return;
    }
    const std::string item_id = timeline_model_.selectedClipId().toStdString();
    const ave::TimelineItem* item = item_id.empty() ? nullptr : project_.timeline().find_item(item_id);
    if (!item || std::abs(x - static_cast<double>(item->transform_x())) < kNoopDeltaThreshold) {
        return;
    }
    ave::TransformParams params;
    params.transform_x = static_cast<float>(x);
    (void)apply_transform(std::move(params));
}

void InspectorService::setPosY(double y) {
    if (!std::isfinite(y)) {
        return;
    }
    const std::string item_id = timeline_model_.selectedClipId().toStdString();
    const ave::TimelineItem* item = item_id.empty() ? nullptr : project_.timeline().find_item(item_id);
    if (!item || std::abs(y - static_cast<double>(item->transform_y())) < kNoopDeltaThreshold) {
        return;
    }
    ave::TransformParams params;
    params.transform_y = static_cast<float>(y);
    (void)apply_transform(std::move(params));
}

void InspectorService::setScaleX(double sx) {
    if (!std::isfinite(sx) || sx <= 0.0) {
        return;
    }
    const std::string item_id = timeline_model_.selectedClipId().toStdString();
    const ave::TimelineItem* item = item_id.empty() ? nullptr : project_.timeline().find_item(item_id);
    if (!item || std::abs(sx - static_cast<double>(item->scale_x())) < kNoopDeltaThreshold) {
        return;
    }
    ave::TransformParams params;
    params.scale_x = static_cast<float>(sx);
    (void)apply_transform(std::move(params));
}

void InspectorService::setScaleY(double sy) {
    if (!std::isfinite(sy) || sy <= 0.0) {
        return;
    }
    const std::string item_id = timeline_model_.selectedClipId().toStdString();
    const ave::TimelineItem* item = item_id.empty() ? nullptr : project_.timeline().find_item(item_id);
    if (!item || std::abs(sy - static_cast<double>(item->scale_y())) < kNoopDeltaThreshold) {
        return;
    }
    ave::TransformParams params;
    params.scale_y = static_cast<float>(sy);
    (void)apply_transform(std::move(params));
}

void InspectorService::setOpacity(double opacity) {
    if (!std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0) {
        return;
    }
    const std::string item_id = timeline_model_.selectedClipId().toStdString();
    const ave::TimelineItem* item = item_id.empty() ? nullptr : project_.timeline().find_item(item_id);
    if (!item || std::abs(opacity - static_cast<double>(item->opacity())) < kNoopDeltaThreshold) {
        return;
    }
    ave::TransformParams params;
    params.opacity = static_cast<float>(opacity);
    (void)apply_transform(std::move(params));
}

void InspectorService::setBlendMode(const QString& mode) {
    const QString trimmed = mode.trimmed();
    if (!is_supported_blend_mode(trimmed)) {
        return;
    }
    const std::string item_id = timeline_model_.selectedClipId().toStdString();
    const ave::TimelineItem* item = item_id.empty() ? nullptr : project_.timeline().find_item(item_id);
    if (!item) {
        return;
    }
    const std::string target_mode = trimmed.toStdString();
    if (target_mode == item->blend_mode()) {
        return;
    }
    ave::TransformParams params;
    params.blend_mode = target_mode;
    (void)apply_transform(std::move(params));
}

void InspectorService::resetTransforms() {
    const std::string item_id = timeline_model_.selectedClipId().toStdString();
    const ave::TimelineItem* item = item_id.empty() ? nullptr : project_.timeline().find_item(item_id);
    if (!item) {
        return;
    }

    const bool already_default =
        std::abs(item->transform_x()) < kNoopDeltaThreshold &&
        std::abs(item->transform_y()) < kNoopDeltaThreshold &&
        std::abs(item->scale_x() - 1.0f) < kNoopDeltaThreshold &&
        std::abs(item->scale_y() - 1.0f) < kNoopDeltaThreshold &&
        std::abs(item->rotation()) < kNoopDeltaThreshold &&
        std::abs(item->anchor_x() - 0.5f) < kNoopDeltaThreshold &&
        std::abs(item->anchor_y() - 0.5f) < kNoopDeltaThreshold &&
        std::abs(item->opacity() - 1.0f) < kNoopDeltaThreshold &&
        item->blend_mode() == "Over";
    if (already_default) {
        return;
    }

    ave::TransformParams reset;
    reset.transform_x = 0.0f;
    reset.transform_y = 0.0f;
    reset.scale_x = 1.0f;
    reset.scale_y = 1.0f;
    reset.rotation = 0.0f;
    reset.anchor_x = 0.5f;
    reset.anchor_y = 0.5f;
    reset.opacity = 1.0f;
    reset.blend_mode = std::string("Over");
    (void)apply_transform(std::move(reset));
}

void InspectorService::refreshFromProject() {
    const std::string item_id = timeline_model_.selectedClipId().toStdString();
    const ave::Track* trk = item_id.empty() ? nullptr : project_.timeline().find_track_for_item(item_id);
    const ave::TimelineItem* item = trk ? trk->find_item(item_id) : nullptr;

    const bool next_has_sel = (trk != nullptr && item != nullptr);
    const QString next_clip = next_has_sel ? QString::fromStdString(item->name()) : QString{};
    const QString next_track = next_has_sel ? QString::fromStdString(trk->name()) : QString{};
    const QString next_dur = next_has_sel
        ? format_clip_duration(*item, safe_inspector_fps(project_.timeline()))
        : QStringLiteral("00:00:00:00");

    const double next_px = next_has_sel ? static_cast<double>(item->transform_x()) : 0.0;
    const double next_py = next_has_sel ? static_cast<double>(item->transform_y()) : 0.0;
    const double next_sx = next_has_sel ? static_cast<double>(item->scale_x()) : 1.0;
    const double next_sy = next_has_sel ? static_cast<double>(item->scale_y()) : 1.0;
    const double next_op = next_has_sel ? static_cast<double>(item->opacity()) : 1.0;
    const QString next_bm = next_has_sel && !item->blend_mode().empty()
        ? QString::fromStdString(item->blend_mode())
        : QStringLiteral("Over");

    const bool sel_changed =
        (next_has_sel != has_selection_) ||
        (next_clip != clip_name_) ||
        (next_track != track_name_) ||
        (next_dur != duration_timecode_);

    const bool tf_changed =
        (std::abs(next_px - pos_x_) >= kPropertyCacheEpsilon) ||
        (std::abs(next_py - pos_y_) >= kPropertyCacheEpsilon) ||
        (std::abs(next_sx - scale_x_) >= kPropertyCacheEpsilon) ||
        (std::abs(next_sy - scale_y_) >= kPropertyCacheEpsilon) ||
        (std::abs(next_op - opacity_) >= kPropertyCacheEpsilon) ||
        (next_bm != blend_mode_);

    has_selection_ = next_has_sel;
    clip_name_ = next_clip;
    track_name_ = next_track;
    duration_timecode_ = next_dur;
    pos_x_ = next_px;
    pos_y_ = next_py;
    scale_x_ = next_sx;
    scale_y_ = next_sy;
    opacity_ = next_op;
    blend_mode_ = next_bm;

    if (sel_changed) {
        emit selectionChanged();
    }
    if (tf_changed) {
        emit transformChanged();
    }
}

void InspectorService::bind_history_listener() {
    unbind_history_listener();
    QPointer<InspectorService> self(this);
    history_listener_id_ = project_.command_history_mut().add_change_listener_with_id([self]() {
        if (self.isNull()) {
            return;
        }
        self->refreshFromProject();
    });
}

void InspectorService::unbind_history_listener() {
    if (history_listener_id_ != 0) {
        project_.command_history_mut().remove_change_listener(history_listener_id_);
        history_listener_id_ = 0;
    }
}

void InspectorService::handle_project_reloaded() {
    bind_history_listener();
    refreshFromProject();
}

bool InspectorService::apply_transform(ave::TransformParams params) {
    const std::string item_id = timeline_model_.selectedClipId().toStdString();
    if (item_id.empty()) {
        return false;
    }
    const ave::Track* trk = project_.timeline().find_track_for_item(item_id);
    if (!trk) {
        return false;
    }
    const ave::CommandResult res = project_.execute_command(
        std::make_unique<ave::SetTransformCommand>(trk->id(), item_id, std::move(params)));
    return res.is_success();
}

} // namespace ave::desktop

