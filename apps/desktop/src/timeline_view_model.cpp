#include "ave/timeline_view_model.hpp"

#include <algorithm>

namespace ave {

TimelineViewModel::TimelineViewModel(Timeline& timeline)
    : timeline_(timeline),
      project_(nullptr) {}

TimelineViewModel::TimelineViewModel(Project& project)
    : timeline_(project.timeline_mut()),
      project_(&project) {}

size_t TimelineViewModel::track_count() const {
    return timeline_.video_tracks().size() + timeline_.audio_tracks().size();
}

std::vector<TrackDisplayInfo> TimelineViewModel::tracks() const {
    std::vector<TrackDisplayInfo> result;
    result.reserve(track_count());

    for (const auto& track : timeline_.video_tracks()) {
        TrackDisplayInfo info;
        info.id = track.id();
        info.name = track.name();
        info.type = "Video";
        info.muted = track.muted();
        info.solo = track.soloed();
        result.push_back(std::move(info));
    }

    for (const auto& track : timeline_.audio_tracks()) {
        TrackDisplayInfo info;
        info.id = track.id();
        info.name = track.name();
        info.type = "Audio";
        info.muted = track.muted();
        info.solo = track.soloed();
        result.push_back(std::move(info));
    }

    return result;
}

std::vector<ClipDisplayInfo> TimelineViewModel::clips_for_track(const std::string& track_id) const {
    std::vector<ClipDisplayInfo> result;
    const Track* track = timeline_.find_track(track_id);
    if (!track) return result;

    result.reserve(track->items().size());
    for (const auto& item : track->items()) {
        ClipDisplayInfo info;
        info.id = item.id();
        info.asset_id = item.asset_id();
        info.name = item.name();
        info.start_time = item.timeline_in();
        info.duration = item.duration();
        info.source_in = item.source_in();
        info.source_out = item.source_out();
        result.push_back(std::move(info));
    }
    return result;
}

Rational TimelineViewModel::duration() const {
    return timeline_.duration();
}

std::string TimelineViewModel::playhead_timecode() const {
    return Timecode::from_rational(playhead_time_, timeline_.frame_rate()).to_string();
}

void TimelineViewModel::set_playhead_time(Rational time) {
    if (time < Rational{0, 1}) time = Rational{0, 1};
    Rational dur = timeline_.duration();
    if (dur > Rational{0, 1} && time > dur) time = dur;

    if (playhead_time_ != time) {
        playhead_time_ = time;
        notify_changed();
    }
}

void TimelineViewModel::select_clip(const std::string& clip_id) {
    if (selected_clip_id_ != clip_id) {
        selected_clip_id_ = clip_id;
        notify_changed();
    }
}

void TimelineViewModel::clear_selection() {
    if (!selected_clip_id_.empty()) {
        selected_clip_id_.clear();
        notify_changed();
    }
}

void TimelineViewModel::set_tool(TimelineTool tool) {
    if (tool_ != tool) {
        tool_ = tool;
        notify_changed();
    }
}

double TimelineViewModel::zoom_factor() const noexcept {
    return viewport_.pixels_per_second() / 100.0;
}

void TimelineViewModel::set_zoom_factor(double zoom) {
    double clamped = std::clamp(zoom, 0.1, 20.0);
    viewport_.set_pixels_per_second(clamped * 100.0);
    notify_changed();
}

void TimelineViewModel::set_snapping_enabled(bool enabled) noexcept {
    snapping_.set_enabled(enabled);
}

SnapResult TimelineViewModel::snap(Rational candidate_time, const std::string& ignore_clip_id) const {
    return snapping_.find_snap(candidate_time, timeline_, viewport_, playhead_time_, ignore_clip_id);
}

bool TimelineViewModel::split_at_playhead() {
    std::string target_clip_id = selected_clip_id_;
    std::string target_track_id = "";

    if (!target_clip_id.empty()) {
        const Track* trk = timeline_.find_track_for_item(target_clip_id);
        if (trk) target_track_id = trk->id();
    } else {
        // Find first item under playhead
        for (const auto& trk : timeline_.video_tracks()) {
            const auto* it = trk.item_at(playhead_time_);
            if (it) {
                target_clip_id = it->id();
                target_track_id = trk.id();
                break;
            }
        }
        if (target_clip_id.empty()) {
            for (const auto& trk : timeline_.audio_tracks()) {
                const auto* it = trk.item_at(playhead_time_);
                if (it) {
                    target_clip_id = it->id();
                    target_track_id = trk.id();
                    break;
                }
            }
        }
    }

    if (target_clip_id.empty() || target_track_id.empty()) {
        return false;
    }

    if (project_) {
        auto cmd = std::make_unique<SplitClipCommand>(target_track_id, target_clip_id, playhead_time_);
        auto res = project_->execute_command(std::move(cmd));
        if (res.is_success()) {
            selected_clip_id_.clear();
            notify_changed();
            return true;
        }
        return false;
    } else {
        try {
            auto [left, right] = timeline_.split_item(target_track_id, target_clip_id, playhead_time_);
            selected_clip_id_ = left;
            notify_changed();
            return true;
        } catch (...) {
            return false;
        }
    }
}

bool TimelineViewModel::delete_selected() {
    if (selected_clip_id_.empty()) return false;
    const Track* trk = timeline_.find_track_for_item(selected_clip_id_);
    if (!trk) return false;

    std::string trk_id = trk->id();
    std::string clip_id = selected_clip_id_;

    if (project_) {
        auto cmd = std::make_unique<DeleteClipCommand>(trk_id, clip_id);
        auto res = project_->execute_command(std::move(cmd));
        if (res.is_success()) {
            clear_selection();
            return true;
        }
        return false;
    } else {
        Track* trk_mut = timeline_.find_track_mut(trk_id);
        if (trk_mut && trk_mut->remove_item(clip_id)) {
            clear_selection();
            return true;
        }
        return false;
    }
}

bool TimelineViewModel::move_selected(const std::string& dst_track_id, Rational new_in) {
    if (selected_clip_id_.empty()) return false;
    const Track* src_trk = timeline_.find_track_for_item(selected_clip_id_);
    if (!src_trk) return false;

    // Apply magnetic snapping if enabled
    if (snapping_.enabled()) {
        auto snap_res = snap(new_in, selected_clip_id_);
        if (snap_res.snapped) {
            new_in = snap_res.snapped_time;
        }
    }

    if (project_) {
        auto cmd = std::make_unique<MoveClipCommand>(src_trk->id(), selected_clip_id_, dst_track_id, new_in);
        auto res = project_->execute_command(std::move(cmd));
        if (res.is_success()) {
            notify_changed();
            return true;
        }
        return false;
    }
    return false;
}

bool TimelineViewModel::toggle_track_mute(const std::string& track_id) {
    const Track* trk = timeline_.find_track(track_id);
    if (!trk) return false;

    bool new_mute = !trk->muted();
    if (project_) {
        auto cmd = std::make_unique<SetTrackMuteCommand>(track_id, new_mute);
        auto res = project_->execute_command(std::move(cmd));
        if (res.is_success()) {
            notify_changed();
            return true;
        }
        return false;
    } else {
        Track* trk_mut = timeline_.find_track_mut(track_id);
        if (trk_mut) {
            trk_mut->set_muted(new_mute);
            notify_changed();
            return true;
        }
        return false;
    }
}

bool TimelineViewModel::undo() {
    if (project_ && project_->undo().is_success()) {
        notify_changed();
        return true;
    }
    return false;
}

bool TimelineViewModel::redo() {
    if (project_ && project_->redo().is_success()) {
        notify_changed();
        return true;
    }
    return false;
}

bool TimelineViewModel::can_undo() const {
    return project_ ? project_->command_history().can_undo() : false;
}

bool TimelineViewModel::can_redo() const {
    return project_ ? project_->command_history().can_redo() : false;
}

void TimelineViewModel::notify_changed() {
    if (on_changed_) {
        on_changed_();
    }
}

} // namespace ave
