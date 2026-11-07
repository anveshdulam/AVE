#include "ave/timeline_view_model.hpp"

#include <algorithm>

namespace ave {

TimelineViewModel::TimelineViewModel(Timeline& timeline)
    : timeline_(timeline) {}

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

void TimelineViewModel::set_zoom_factor(double zoom) {
    double clamped = std::clamp(zoom, 0.1, 10.0);
    if (std::abs(zoom_factor_ - clamped) > 0.001) {
        zoom_factor_ = clamped;
        notify_changed();
    }
}

bool TimelineViewModel::split_at_playhead() {
    bool split_done = false;

    if (!selected_clip_id_.empty()) {
        for (const auto& track : timeline_.video_tracks()) {
            if (track.find_item(selected_clip_id_)) {
                auto res = timeline_.split_item(track.id(), selected_clip_id_, playhead_time_);
                if (!res.first.empty()) {
                    selected_clip_id_ = res.first;
                    split_done = true;
                    break;
                }
            }
        }
        if (!split_done) {
            for (const auto& track : timeline_.audio_tracks()) {
                if (track.find_item(selected_clip_id_)) {
                    auto res = timeline_.split_item(track.id(), selected_clip_id_, playhead_time_);
                    if (!res.first.empty()) {
                        selected_clip_id_ = res.first;
                        split_done = true;
                        break;
                    }
                }
            }
        }
    } else {
        // Split any item under playhead
        for (const auto& track : timeline_.video_tracks()) {
            const TimelineItem* item = track.item_at(playhead_time_);
            if (item) {
                auto res = timeline_.split_item(track.id(), item->id(), playhead_time_);
                if (!res.first.empty()) split_done = true;
            }
        }
        for (const auto& track : timeline_.audio_tracks()) {
            const TimelineItem* item = track.item_at(playhead_time_);
            if (item) {
                auto res = timeline_.split_item(track.id(), item->id(), playhead_time_);
                if (!res.first.empty()) split_done = true;
            }
        }
    }

    if (split_done) {
        notify_changed();
    }
    return split_done;
}

void TimelineViewModel::notify_changed() {
    if (on_changed_) {
        on_changed_();
    }
}

} // namespace ave

