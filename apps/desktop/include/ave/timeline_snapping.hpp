#pragma once

#include "ave/timeline.hpp"
#include "ave/timeline_viewport.hpp"

#include <string>
#include <vector>
#include <optional>

namespace ave {

enum class SnapTargetType {
    None,
    Origin,
    Playhead,
    ClipIn,
    ClipOut,
    Marker
};

struct SnapResult {
    bool snapped{false};
    Rational snapped_time{0, 1};
    double pixel_distance{0.0};
    SnapTargetType target_type{SnapTargetType::None};
    std::string target_id{};
};

/**
 * @brief Snapping engine providing magnetic alignment to clip edges,
 * playhead position, and timeline boundaries within pixel thresholds.
 */
class TimelineSnapping {
public:
    explicit TimelineSnapping(double threshold_pixels = 10.0)
        : threshold_pixels_(threshold_pixels) {}

    [[nodiscard]] double threshold_pixels() const noexcept { return threshold_pixels_; }
    void set_threshold_pixels(double px) noexcept { threshold_pixels_ = std::max(1.0, px); }

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void set_enabled(bool en) noexcept { enabled_ = en; }

    /**
     * @brief Computes nearest magnetic snap target for a given candidate timestamp.
     */
    [[nodiscard]] SnapResult find_snap(Rational candidate_time,
                                       const Timeline& timeline,
                                       const TimelineViewport& viewport,
                                       Rational playhead_time,
                                       const std::string& ignore_clip_id = "") const;

private:
    double threshold_pixels_{10.0};
    bool enabled_{true};
};

} // namespace ave
