#pragma once

#include "ave/timeline.hpp"
#include "ave/timecode.hpp"
#include "ave/project.hpp"
#include "ave/timeline_viewport.hpp"
#include "ave/timeline_snapping.hpp"

#include <string>
#include <vector>
#include <functional>

namespace ave {

enum class TimelineTool {
    Select,
    Razor,
    Trim,
    Slip
};

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
 * Decouples presentation and user interactions from core timeline storage,
 * and routes all user edits through validated Command Bus transactions.
 */
class TimelineViewModel {
public:
    explicit TimelineViewModel(Timeline& timeline);
    explicit TimelineViewModel(Project& project);

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

    // Tool Modes
    [[nodiscard]] TimelineTool tool() const noexcept { return tool_; }
    void set_tool(TimelineTool tool);

    // Viewport & Snapping
    [[nodiscard]] const TimelineViewport& viewport() const noexcept { return viewport_; }
    TimelineViewport& viewport_mut() noexcept { return viewport_; }

    [[nodiscard]] const TimelineSnapping& snapping() const noexcept { return snapping_; }
    TimelineSnapping& snapping_mut() noexcept { return snapping_; }

    [[nodiscard]] double zoom_factor() const noexcept;
    void set_zoom_factor(double zoom);

    [[nodiscard]] bool snapping_enabled() const noexcept { return snapping_.enabled(); }
    void set_snapping_enabled(bool enabled) noexcept;

    [[nodiscard]] SnapResult snap(Rational candidate_time, const std::string& ignore_clip_id = "") const;

    // Command Bus Operations
    bool split_at_playhead();
    bool delete_selected();
    bool move_selected(const std::string& dst_track_id, Rational new_in);
    bool toggle_track_mute(const std::string& track_id);

    bool undo();
    bool redo();
    [[nodiscard]] bool can_undo() const;
    [[nodiscard]] bool can_redo() const;

    void set_on_changed(std::function<void()> callback) { on_changed_ = std::move(callback); }

private:
    Timeline& timeline_;
    Project* project_{nullptr};

    Rational playhead_time_{0, 1};
    std::string selected_clip_id_{};
    TimelineTool tool_{TimelineTool::Select};
    TimelineViewport viewport_{};
    TimelineSnapping snapping_{};

    std::function<void()> on_changed_{};

    void notify_changed();
};

} // namespace ave
