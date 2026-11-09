#include "ave/project.hpp"
#include "ave/timeline_view_model.hpp"
#include "ave/timeline_viewport.hpp"
#include "ave/timeline_snapping.hpp"
#include "ave/inspector_view_model.hpp"

#include <iostream>
#include <cmath>
#include <cassert>

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

#define ASSERT_FALSE(cond) \
    do { \
        if ((cond)) { \
            std::cerr << "Assertion failed (expected false): " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            std::cerr << "Assertion failed: " #a " == " #b " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

int main() {
    std::cout << "Running Timeline UI & Presentation Layer Tests..." << std::endl;

    // 1. Test TimelineViewport Coordinate Math
    std::cout << "  Testing TimelineViewport..." << std::endl;
    ave::TimelineViewport viewport(100.0, 1920.0, 400.0);
    ASSERT_EQ(viewport.pixels_per_second(), 100.0);

    ave::Rational fps{24, 1};

    // Time to Pixel
    double px0 = viewport.time_to_pixel(ave::Rational{0, 1});
    double px1 = viewport.time_to_pixel(ave::Rational{1, 1});
    double px2 = viewport.time_to_pixel(ave::Rational{2, 1});
    ASSERT_EQ(px0, 0.0);
    ASSERT_EQ(px1, 100.0);
    ASSERT_EQ(px2, 200.0);

    // Pixel to Time (quantized to 24fps)
    ave::Rational t_back = viewport.pixel_to_time(100.0, fps);
    ASSERT_EQ(t_back, ave::Rational(1, 1));

    // Scroll offset test
    viewport.set_scroll_offset_x(50.0);
    ASSERT_EQ(viewport.time_to_pixel(ave::Rational{1, 1}), 50.0);
    viewport.set_scroll_offset_x(0.0);

    // Zoom at pivot test
    viewport.zoom_at(0.0, 2.0); // 200 px/s
    ASSERT_EQ(viewport.pixels_per_second(), 200.0);
    ASSERT_EQ(viewport.time_to_pixel(ave::Rational{1, 1}), 200.0);

    // Track Y layout test
    double y0 = viewport.track_y(0, 64.0, 4.0);
    double y1 = viewport.track_y(1, 64.0, 4.0);
    ASSERT_EQ(y0, 0.0);
    ASSERT_EQ(y1, 68.0);

    // Reset viewport
    viewport.set_pixels_per_second(100.0);

    // 2. Test TimelineSnapping Magnetic Alignment
    std::cout << "  Testing TimelineSnapping..." << std::endl;
    ave::Timeline timeline("tl-snap", "Snap Timeline", fps);
    std::string v1_id = timeline.video_tracks()[0].id();

    // Insert clip: [2.0s .. 5.0s]
    ave::TimelineItem snap_clip("c-snap", "c1", "a1", "Snap Clip",
                               ave::Rational{2, 1}, ave::Rational{3, 1}, ave::Rational{0, 1});
    timeline.find_track_mut(v1_id)->add_item(snap_clip);

    ave::TimelineSnapping snapping(10.0); // 10 px threshold
    ASSERT_TRUE(snapping.enabled());

    // 2.1 Candidate near clip in-point (2.0s = 200px; candidate at 2.05s = 205px -> delta 5px <= 10px)
    ave::Rational near_in{205, 100};
    ave::Rational playhead{0, 1};
    auto res_snap_in = snapping.find_snap(near_in, timeline, viewport, playhead);
    ASSERT_TRUE(res_snap_in.snapped);
    ASSERT_EQ(res_snap_in.snapped_time, ave::Rational(2, 1));
    ASSERT_EQ(res_snap_in.target_type, ave::SnapTargetType::ClipIn);

    // 2.2 Candidate far from any snap point (2.5s = 250px; nearest is 200px or 500px -> delta 50px > 10px)
    ave::Rational far_cand{5, 2};
    auto res_far = snapping.find_snap(far_cand, timeline, viewport, playhead);
    ASSERT_FALSE(res_far.snapped);
    ASSERT_EQ(res_far.snapped_time, far_cand);

    // 2.3 Candidate near playhead (playhead at 1.0s = 100px; candidate at 1.05s = 105px -> delta 5px)
    playhead = ave::Rational{1, 1};
    ave::Rational near_playhead{105, 100};
    auto res_ph = snapping.find_snap(near_playhead, timeline, viewport, playhead);
    ASSERT_TRUE(res_ph.snapped);
    ASSERT_EQ(res_ph.snapped_time, ave::Rational(1, 1));
    ASSERT_EQ(res_ph.target_type, ave::SnapTargetType::Playhead);

    // 2.4 Ignore self during dragging
    auto res_ignore = snapping.find_snap(near_in, timeline, viewport, playhead, "c-snap");
    ASSERT_FALSE(res_ignore.snapped); // c-snap ignored!

    // 3. Test TimelineViewModel & Command Bus Operations
    std::cout << "  Testing TimelineViewModel Command Operations..." << std::endl;
    ave::Project project("UI Test Project");
    std::string v1 = project.timeline().video_tracks()[0].id();

    // Place a 4.0s clip on V1: [0.0s .. 4.0s]
    ave::TimelineItem main_clip("main-clip", "c-main", "a1", "Main Video",
                               ave::Rational{0, 1}, ave::Rational{4, 1}, ave::Rational{0, 1});
    project.timeline_mut().find_track_mut(v1)->add_item(main_clip);

    ave::TimelineViewModel vm(project);
    int change_events = 0;
    vm.set_on_changed([&]() { change_events++; });

    // Select clip
    vm.select_clip("main-clip");
    ASSERT_EQ(vm.selected_clip_id(), "main-clip");

    // Tool switching
    vm.set_tool(ave::TimelineTool::Razor);
    ASSERT_EQ(static_cast<int>(vm.tool()), static_cast<int>(ave::TimelineTool::Razor));
    vm.set_tool(ave::TimelineTool::Select);

    // Blade cut via ViewModel at 2.0s
    vm.set_playhead_time(ave::Rational{2, 1});
    bool split_ok = vm.split_at_playhead();
    ASSERT_TRUE(split_ok);
    ASSERT_EQ(project.timeline().video_tracks()[0].items().size(), 2);
    ASSERT_TRUE(vm.can_undo());

    // Undo Split
    bool undo_ok = vm.undo();
    ASSERT_TRUE(undo_ok);
    ASSERT_EQ(project.timeline().video_tracks()[0].items().size(), 1);
    ASSERT_EQ(project.timeline().video_tracks()[0].items()[0].duration(), ave::Rational(4, 1));

    // Redo Split
    bool redo_ok = vm.redo();
    ASSERT_TRUE(redo_ok);
    ASSERT_EQ(project.timeline().video_tracks()[0].items().size(), 2);

    // Track Mute Toggle
    ASSERT_FALSE(project.timeline().video_tracks()[0].muted());
    vm.toggle_track_mute(v1);
    ASSERT_TRUE(project.timeline().video_tracks()[0].muted());
    vm.undo();
    ASSERT_FALSE(project.timeline().video_tracks()[0].muted());

    // 4. Test InspectorViewModel Property Binding & Command Mutators
    std::cout << "  Testing InspectorViewModel..." << std::endl;
    ave::InspectorViewModel inspector(project);
    ASSERT_FALSE(inspector.has_selection());

    std::string first_slice_id = project.timeline().video_tracks()[0].items()[0].id();
    inspector.set_selected_clip_id(first_slice_id);
    ASSERT_TRUE(inspector.has_selection());
    ASSERT_EQ(inspector.name(), "Main Video");
    ASSERT_EQ(inspector.opacity(), 1.0f);
    ASSERT_EQ(inspector.scale_x(), 1.0f);

    // Mutate transforms via Inspector
    bool op_ok = inspector.set_opacity(0.65f);
    ASSERT_TRUE(op_ok);
    ASSERT_EQ(inspector.opacity(), 0.65f);

    bool sc_ok = inspector.set_scale(0.75f, 0.75f);
    ASSERT_TRUE(sc_ok);
    ASSERT_EQ(inspector.scale_x(), 0.75f);

    bool bm_ok = inspector.set_blend_mode("Screen");
    ASSERT_TRUE(bm_ok);
    ASSERT_EQ(inspector.blend_mode(), "Screen");

    // Undo Inspector transform mutations
    project.undo(); // Undo blend mode
    ASSERT_EQ(inspector.blend_mode(), "Over");
    project.undo(); // Undo scale
    ASSERT_EQ(inspector.scale_x(), 1.0f);
    project.undo(); // Undo opacity
    ASSERT_EQ(inspector.opacity(), 1.0f);

    std::cout << "All Timeline UI & Presentation Layer Tests PASSED!" << std::endl;
    return 0;
}
