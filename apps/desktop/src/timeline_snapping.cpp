#include "ave/timeline_snapping.hpp"

#include <cmath>

namespace ave {

SnapResult TimelineSnapping::find_snap(Rational candidate_time,
                                       const Timeline& timeline,
                                       const TimelineViewport& viewport,
                                       Rational playhead_time,
                                       const std::string& ignore_clip_id) const {
    SnapResult result;
    result.snapped_time = candidate_time;

    if (!enabled_) {
        return result;
    }

    double cand_px = viewport.time_to_pixel(candidate_time);
    double closest_dist = threshold_pixels_ + 1.0;

    auto check_point = [&](Rational target_time, SnapTargetType type, const std::string& id) {
        double pt_px = viewport.time_to_pixel(target_time);
        double dist = std::abs(cand_px - pt_px);
        if (dist <= threshold_pixels_ && dist < closest_dist) {
            closest_dist = dist;
            result.snapped = true;
            result.snapped_time = target_time;
            result.pixel_distance = dist;
            result.target_type = type;
            result.target_id = id;
        }
    };

    // 1. Origin (0:00)
    check_point(Rational{0, 1}, SnapTargetType::Origin, "origin");

    // 2. Playhead
    check_point(playhead_time, SnapTargetType::Playhead, "playhead");

    // 3. Clip In-Points and Out-Points
    auto check_tracks = [&](const std::vector<Track>& tracks) {
        for (const auto& track : tracks) {
            for (const auto& item : track.items()) {
                if (!ignore_clip_id.empty() && item.id() == ignore_clip_id) {
                    continue;
                }
                check_point(item.timeline_in(), SnapTargetType::ClipIn, item.id());
                check_point(item.timeline_out(), SnapTargetType::ClipOut, item.id());
            }
        }
    };

    check_tracks(timeline.video_tracks());
    check_tracks(timeline.audio_tracks());

    return result;
}

} // namespace ave
