#include "ave/command.hpp"
#include "ave/media_pool.hpp"
#include "ave/playback_session.hpp"
#include "ave/playback_snapshot.hpp"
#include "ave/project.hpp"
#include "ave/timecode.hpp"
#include "ave/timeline.hpp"
#include "ave/vulkan_device.hpp"

#include <iostream>
#include <filesystem>
#include <memory>
#include <utility>

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
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

int main(int argc, char* argv[]) {
    std::cout << "Running Multi-Track Compositing Integration Test..." << std::endl;

    std::filesystem::path fixture_path = (argc > 1) ? argv[1] : "tests/fixtures/sample_test.mp4";
    if (!std::filesystem::exists(fixture_path)) {
        fixture_path = std::filesystem::current_path() / "tests/fixtures/sample_test.mp4";
    }
    ASSERT_TRUE(std::filesystem::exists(fixture_path));

    // 1. Setup Project (MediaPool + Multi-Track Timeline)
    ave::Project project("Multi-Track Comp Project");
    {
        std::vector<ave::SourceStream> streams;

        ave::SourceStream vs(0, ave::StreamType::Video, "mpeg4", ave::Rational{1, 24}, 24);
        ave::VideoStreamInfo vinfo;
        vinfo.width = 320;
        vinfo.height = 240;
        vinfo.frame_rate = ave::Rational{24, 1};
        vs.set_video_info(vinfo);
        streams.push_back(std::move(vs));

        ave::MediaAsset asset("asset-sample", fixture_path, "sample_test.mp4", 48796, "mp4", ave::Rational{1, 1}, std::move(streams));
        project.media_pool_mut().register_asset(std::move(asset));
    }

    // 2. Setup Multi-Track Timeline (V1: Background, V2: Overlay Picture-in-Picture)
    {
        ave::Timeline timeline("tl-comp", "Multi-Track Composite", ave::Rational{24, 1}, 320, 240);
        timeline.add_video_track("V2");
        ASSERT_EQ(timeline.video_tracks().size(), static_cast<size_t>(2));

        const std::string v1_id_init = timeline.video_tracks()[0].id();
        const std::string v2_id_init = timeline.video_tracks()[1].id();

        ave::TimelineItem bg_item("bg-clip", "c1", "asset-sample", "Background",
                                 ave::Rational{0, 1}, ave::Rational{1, 1}, ave::Rational{0, 1});
        timeline.find_track_mut(v1_id_init)->add_item(std::move(bg_item));

        ave::TimelineItem pip_item("pip-clip", "c2", "asset-sample", "PiP Overlay",
                                  ave::Rational{0, 1}, ave::Rational{1, 1}, ave::Rational{0, 1});
        pip_item.set_scale(0.5f, 0.5f);
        pip_item.set_transform(40.0f, 30.0f);
        pip_item.set_opacity(0.80f);
        pip_item.set_blend_mode("Over");
        timeline.find_track_mut(v2_id_init)->add_item(std::move(pip_item));

        project.timeline_mut() = std::move(timeline);
    }

    const std::string v2_id = project.timeline().video_tracks()[1].id();

    // 3. Setup SnapshotStore, PlaybackSession, and Coordinator
    auto snapshot_store = std::make_shared<ave::PlaybackSnapshotStore>(
        project.timeline(), project.media_pool());
    const ave::PlaybackSnapshotPtr initial_snap = snapshot_store->load();
    ASSERT_TRUE(initial_snap != nullptr);
    ASSERT_EQ(initial_snap->version, uint64_t{1});

    ave::PlaybackSessionConfig cfg;
    cfg.enable_compositing = true;
    cfg.video_queue_capacity = 8;
    ave::PlaybackSession session(snapshot_store, cfg);
    ave::ProjectSnapshotCoordinator coordinator(project, snapshot_store, &session);

    // Attach Vulkan hardware device if supported
    auto vulkan_device = ave::VulkanDevice::create(false);
    if (vulkan_device) {
        std::cout << "  Hardware Compositor Active: " << vulkan_device->info().device_name << std::endl;
        session.set_vulkan_device(std::move(vulkan_device));
    } else {
        std::cout << "  Hardware Compositor: CPU Reference Path (Headless/CI mode)" << std::endl;
    }

    ASSERT_TRUE(session.is_compositing_enabled());

    // 4. Test Playback Transport & Multi-Layer Frame Compositing
    session.play();
    ASSERT_EQ(session.state(), ave::PlaybackState::Playing);

    // Advance and evaluate presentation updates
    int composited_frames = 0;
    for (int step = 0; step < 8; ++step) {
        auto result = session.update();
        if (result.frame.has_value()) {
            composited_frames++;
            ASSERT_TRUE(result.is_composited);
            ASSERT_EQ(result.snapshot_version, uint64_t{1});
            ASSERT_EQ(result.frame->snapshot_version, uint64_t{1});
            ASSERT_EQ(result.frame->width, 320);
            ASSERT_EQ(result.frame->height, 240);
            ASSERT_EQ(result.frame->rgba_data.size(), static_cast<size_t>(320 * 240 * 4));
            ASSERT_EQ(result.active_tracks_count, static_cast<size_t>(2));
        }
    }
    std::cout << "  Composited multi-layer frames evaluated: " << composited_frames << std::endl;
    ASSERT_TRUE(composited_frames > 0);

    // 5. Test Track Muting & Undo Reflected via Immutable Snapshot Publication
    session.seek(ave::Rational{0, 1});
    const ave::PlaybackSnapshotPtr before_mute_snap = snapshot_store->load();
    ASSERT_EQ(before_mute_snap->version, uint64_t{1});
    ASSERT_TRUE(!before_mute_snap->timeline.video_tracks()[1].muted());

    const auto mute_res = project.execute_command(
        std::make_unique<ave::SetTrackMuteCommand>(v2_id, true));
    ASSERT_TRUE(mute_res.is_success());

    const ave::PlaybackSnapshotPtr muted_snap = snapshot_store->load();
    ASSERT_EQ(muted_snap->version, uint64_t{2});
    ASSERT_TRUE(muted_snap->timeline.video_tracks()[1].muted());
    // Retained reader snapshot remains untouched/immutable
    ASSERT_TRUE(!before_mute_snap->timeline.video_tracks()[1].muted());

    auto muted_result = session.update();
    ASSERT_TRUE(muted_result.frame.has_value());
    ASSERT_EQ(muted_result.snapshot_version, muted_snap->version);
    ASSERT_EQ(muted_result.frame->snapshot_version, muted_snap->version);
    ASSERT_EQ(muted_result.active_tracks_count, static_cast<size_t>(1)); // Only V1 active when V2 is muted

    const auto undo_res = project.undo();
    ASSERT_TRUE(undo_res.is_success());
    const ave::PlaybackSnapshotPtr unmuted_snap = snapshot_store->load();
    ASSERT_EQ(unmuted_snap->version, uint64_t{3});
    ASSERT_TRUE(!unmuted_snap->timeline.video_tracks()[1].muted());
    ASSERT_TRUE(muted_snap->timeline.video_tracks()[1].muted());

    auto unmuted_result = session.update();
    ASSERT_TRUE(unmuted_result.frame.has_value());
    ASSERT_EQ(unmuted_result.snapshot_version, unmuted_snap->version);
    ASSERT_EQ(unmuted_result.active_tracks_count, static_cast<size_t>(2));
    std::cout << "  Track mute & undo dynamically reflected via PlaybackSnapshot: PASSED." << std::endl;

    // 6. Test Seeking & Frame Accuracy
    session.pause();
    session.seek(ave::Rational{1, 4}); // Seek to 0.25s (frame 6 @ 24fps)
    auto seek_result = session.update();
    ASSERT_EQ(seek_result.presentation_time, ave::Rational(1, 4));
    ASSERT_EQ(seek_result.snapshot_version, unmuted_snap->version);
    ASSERT_TRUE(seek_result.is_composited);

    ave::Timecode tc = ave::Timecode::from_rational(session.current_time(), unmuted_snap->timeline.frame_rate());
    ASSERT_EQ(tc.to_string(), "00:00:00:06");
    std::cout << "  Composited seek to frame 00:00:00:06: PASSED." << std::endl;

    session.stop();
    ASSERT_EQ(session.state(), ave::PlaybackState::Stopped);
    session.shutdown();

    std::cout << "Multi-Track Compositing Integration Test PASSED!" << std::endl;
    return 0;
}
