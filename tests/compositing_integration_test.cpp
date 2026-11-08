#include "ave/timeline.hpp"
#include "ave/media_pool.hpp"
#include "ave/playback_session.hpp"
#include "ave/vulkan_device.hpp"
#include "ave/timecode.hpp"

#include <iostream>
#include <filesystem>
#include <cmath>
#include <cassert>

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

    // 1. Setup MediaPool
    ave::MediaPool pool;
    std::vector<ave::SourceStream> streams;

    ave::SourceStream vs(0, ave::StreamType::Video, "mpeg4", ave::Rational{1, 24}, 24);
    ave::VideoStreamInfo vinfo;
    vinfo.width = 320;
    vinfo.height = 240;
    vinfo.frame_rate = ave::Rational{24, 1};
    vs.set_video_info(vinfo);
    streams.push_back(std::move(vs));

    ave::MediaAsset asset("asset-sample", fixture_path, "sample_test.mp4", 48796, "mp4", ave::Rational{1, 1}, std::move(streams));
    pool.register_asset(std::move(asset));

    // 2. Setup Multi-Track Timeline (V1: Background, V2: Overlay Picture-in-Picture)
    ave::Timeline timeline("tl-comp", "Multi-Track Composite", ave::Rational{24, 1}, 320, 240);
    timeline.add_video_track("V2");

    ASSERT_EQ(timeline.video_tracks().size(), 2);
    std::string v1_id = timeline.video_tracks()[0].id();
    std::string v2_id = timeline.video_tracks()[1].id();

    // V1: 1.0s background clip at origin (0, 0), scale 1.0, 100% opacity
    ave::TimelineItem bg_item("bg-clip", "c1", "asset-sample", "Background",
                             ave::Rational{0, 1}, ave::Rational{1, 1}, ave::Rational{0, 1});
    timeline.find_track_mut(v1_id)->add_item(std::move(bg_item));

    // V2: 1.0s overlay clip with PiP transform (Scale 50%, translated +40, +30, Opacity 80%, Over blend)
    ave::TimelineItem pip_item("pip-clip", "c2", "asset-sample", "PiP Overlay",
                              ave::Rational{0, 1}, ave::Rational{1, 1}, ave::Rational{0, 1});
    pip_item.set_scale(0.5f, 0.5f);
    pip_item.set_transform(40.0f, 30.0f);
    pip_item.set_opacity(0.80f);
    pip_item.set_blend_mode("Over");
    timeline.find_track_mut(v2_id)->add_item(std::move(pip_item));

    // 3. Setup PlaybackSession with Compositing enabled
    ave::PlaybackSessionConfig cfg;
    cfg.enable_compositing = true;
    cfg.video_queue_capacity = 8;
    ave::PlaybackSession session(timeline, pool, cfg);

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
            ASSERT_EQ(result.frame->width, 320);
            ASSERT_EQ(result.frame->height, 240);
            ASSERT_EQ(result.frame->rgba_data.size(), 320 * 240 * 4);
            ASSERT_EQ(result.active_tracks_count, 2);
        }
    }
    std::cout << "  Composited multi-layer frames evaluated: " << composited_frames << std::endl;
    ASSERT_TRUE(composited_frames > 0);

    // 5. Test Track Muting in Compositing Pipeline
    timeline.find_track_mut(v2_id)->set_muted(true);
    auto muted_result = session.update();
    ASSERT_TRUE(muted_result.frame.has_value());
    ASSERT_EQ(muted_result.active_tracks_count, 1); // Only V1 active when V2 is muted
    timeline.find_track_mut(v2_id)->set_muted(false);
    std::cout << "  Track mute dynamically reflected in compositing: PASSED." << std::endl;

    // 6. Test Seeking & Frame Accuracy
    session.pause();
    session.seek(ave::Rational{1, 4}); // Seek to 0.25s (frame 6 @ 24fps)
    auto seek_result = session.update();
    ASSERT_EQ(seek_result.presentation_time, ave::Rational(1, 4));
    ASSERT_TRUE(seek_result.is_composited);

    ave::Timecode tc = ave::Timecode::from_rational(session.current_time(), timeline.frame_rate());
    ASSERT_EQ(tc.to_string(), "00:00:00:06");
    std::cout << "  Composited seek to frame 00:00:00:06: PASSED." << std::endl;

    session.stop();
    ASSERT_EQ(session.state(), ave::PlaybackState::Stopped);

    std::cout << "Multi-Track Compositing Integration Test PASSED!" << std::endl;
    return 0;
}
