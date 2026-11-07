#pragma once

#include "ave/timeline.hpp"
#include "ave/timecode.hpp"
#include <string>
#include <vector>
#include <functional>

namespace ave {

struct TrackDisplayInfo {
    std::string id;
    std::string name;
    std::string type; // "Video" or "Audio"
    bool muted{false};
    bool solo{false};
    float volume{1.0f};
    float pan{0.0f};
};

struct ClipDisplayInfo {
    std::string id;
    std::string asset_id;
    std::string name;
    Rational start_time;
    Rational duration;
    Rational source_in;
    Rational source_out;
};

/**
 * @brief ViewModel for the multi-track timeline UI view.
 * Decouples presentation and user interactions from core timeline storage.
 */
class TimelineViewModel {
public:
    explicit TimelineViewModel(Timeline& timeline);

    [[nodiscard]] size_t track_count() const;
    [[nodiscard]] std::vector<TrackDisplayInfo> tracks() const;
    [[nodiscard]] std::vector<ClipDisplayInfo> clips_for_track(const std::string& track_id) const;

    [[nodiscard]] Rational duration() const;
    [[nodiscard]] Rational playhead_time() const noexcept { return playhead_time_; }
    [[nodiscard]] std::string playhead_timecode() const;
    void set_playhead_time(Rational time);

    [[nodiscard]] const std::string& selected_clip_id() const noexcept { return selected_clip_id_; }
    void select_clip(const std::string& clip_id);
    void clear_selection();

    [[nodiscard]] double zoom_factor() const noexcept { return zoom_factor_; }
    void set_zoom_factor(double zoom);

    [[nodiscard]] bool snapping_enabled() const noexcept { return snapping_enabled_; }
    void set_snapping_enabled(bool enabled) noexcept { snapping_enabled_ = enabled; }

    /**
     * @brief Splits the clip at the current playhead position across selected or active tracks.
     */
    bool split_at_playhead();

    void set_on_changed(std::function<void()> callback) { on_changed_ = std::move(callback); }

private:
    Timeline& timeline_;
    Rational playhead_time_{0, 1};
    std::string selected_clip_id_{};
    double zoom_factor_{1.0};
    bool snapping_enabled_{true};
    std::function<void()> on_changed_{};

    void notify_changed();
};

} // namespace ave

