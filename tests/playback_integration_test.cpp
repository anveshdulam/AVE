#include "ave/timeline.hpp"
#include "ave/media_pool.hpp"
#include "ave/playback_session.hpp"
#include "ave/timecode.hpp"

#include <cassert>
#include <iostream>
#include <filesystem>
#include <thread>
#include <chrono>

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            std::cerr << "Assertion failed: " #a " == " #b " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while(0)

int main(int argc, char* argv[]) {
    std::cout << "Running Playback Integration Test..." << std::endl;

    std::filesystem::path fixture_path = (argc > 1) ? argv[1] : "tests/fixtures/sample_test.mp4";
    if (!std::filesystem::exists(fixture_path)) {
        fixture_path = "D:/anvesh/AVE/tests/fixtures/sample_test.mp4";
    }

    ASSERT_TRUE(std::filesystem::exists(fixture_path));

    // 1. Setup MediaPool with Video and Audio streams
    ave::MediaPool pool;
    std::vector<ave::SourceStream> streams;

    ave::SourceStream vs(0, ave::StreamType::Video, "mpeg4", ave::Rational{1, 24}, 24);
    ave::VideoStreamInfo vinfo;
    vinfo.width = 320;
    vinfo.height = 240;
    vinfo.frame_rate = ave::Rational{24, 1};
    vs.set_video_info(vinfo);
    streams.push_back(std::move(vs));

    ave::SourceStream as(1, ave::StreamType::Audio, "aac", ave::Rational{1, 44100}, 44100);
    ave::AudioStreamInfo ainfo;
    ainfo.sample_rate = 44100;
    ainfo.channels = 1;
    as.set_audio_info(ainfo);
    streams.push_back(std::move(as));

    ave::MediaAsset asset("asset-sample", fixture_path, "sample_test.mp4", 48796, "mp4", ave::Rational{1, 1}, std::move(streams));
    pool.register_asset(std::move(asset));

    // 2. Setup Timeline with 1 Video Track and 1 Audio Track
    ave::Timeline timeline("tl-main", "Main Sequence", ave::Rational{24, 1}, 320, 240);
    ASSERT_EQ(timeline.video_tracks().size(), 1);
    ASSERT_EQ(timeline.audio_tracks().size(), 1);

    std::string v_track_id = timeline.video_tracks()[0].id();
    std::string a_track_id = timeline.audio_tracks()[0].id();

    // Add 1.0s video item and 1.0s audio item
    ave::TimelineItem v_item("item-v1", "clip-v1", "asset-sample", "Video Clip 1",
                            ave::Rational{0, 1}, ave::Rational{1, 1}, ave::Rational{0, 1});
    timeline.find_track_mut(v_track_id)->add_item(std::move(v_item));

    ave::TimelineItem a_item("item-a1", "clip-a1", "asset-sample", "Audio Clip 1",
                            ave::Rational{0, 1}, ave::Rational{1, 1}, ave::Rational{0, 1});
    timeline.find_track_mut(a_track_id)->add_item(std::move(a_item));

    ASSERT_EQ(timeline.duration(), ave::Rational(1, 1));

    // 3. Setup PlaybackSession
    ave::PlaybackSessionConfig cfg;
    cfg.prefetch_window = ave::Rational{1, 1};
    cfg.video_queue_capacity = 8;
    cfg.audio_sample_rate = 48000;
    cfg.audio_channels = 2;

    ave::PlaybackSession session(timeline, pool, cfg);
    ASSERT_EQ(session.state(), ave::PlaybackState::Stopped);
    ASSERT_EQ(session.current_time(), ave::Rational(0, 1));

    // 4. Test Playback Transport & Frame Prefetching
    session.play();
    ASSERT_EQ(session.state(), ave::PlaybackState::Playing);

    // Allow background prefetch worker to fill frames
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Poll presentation updates
    int frames_presented = 0;
    for (int i = 0; i < 10; ++i) {
        auto result = session.update();
        if (result.frame.has_value()) {
            frames_presented++;
            ASSERT_EQ(result.frame->width, 320);
            ASSERT_EQ(result.frame->height, 240);
            ASSERT_EQ(result.frame->rgba_data.size(), 320 * 240 * 4);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    std::cout << "  Frames presented in real-time playback: " << frames_presented << std::endl;

    // 5. Test Audio Device is active and consuming frames
    ASSERT_TRUE(session.audio_device() != nullptr);
    ASSERT_TRUE(session.audio_device()->is_running());

    // 6. Test Pause & Frame-stepping
    session.pause();
    ASSERT_EQ(session.state(), ave::PlaybackState::Paused);
    ave::Rational paused_time = session.current_time();

    // Verify audio pump when output device is stopped
    session.audio_device()->stop();
    session.pump_audio(2048);
    ASSERT_TRUE(session.audio_ring_buffer().available_read_frames() > 0);

    // Step +1 frame (1/24 s)
    session.step(1);
    ASSERT_EQ(session.current_time(), paused_time + ave::Rational(1, 24));

    // Step -1 frame back
    session.step(-1);
    ASSERT_EQ(session.current_time(), paused_time);

    // 7. Test Seeking and Scrub Token Invalidation
    uint64_t old_gen = session.current_seek_generation();
    session.seek(ave::Rational{1, 2}); // Seek to 0.5s
    ASSERT_EQ(session.current_time(), ave::Rational(1, 2));
    ASSERT_TRUE(session.current_seek_generation() > old_gen);

    // Immediate presentation frame available after seek
    auto seek_res = session.update();
    ASSERT_EQ(seek_res.presentation_time, ave::Rational(1, 2));

    // 8. Test SMPTE Timecode formatting
    ave::Timecode tc = ave::Timecode::from_rational(session.current_time(), timeline.frame_rate());
    ASSERT_EQ(tc.hours, 0);
    ASSERT_EQ(tc.minutes, 0);
    ASSERT_EQ(tc.seconds, 0);
    ASSERT_EQ(tc.frames, 12); // 0.5s * 24fps = 12 frames
    ASSERT_EQ(tc.to_string(), "00:00:00:12");

    // Test reverse parse
    ave::Timecode tc_parsed = ave::Timecode::from_string("00:00:00:12");
    ASSERT_EQ(tc_parsed.to_rational(timeline.frame_rate()), ave::Rational(1, 2));

    // 9. Clean shutdown
    session.stop();
    ASSERT_EQ(session.state(), ave::PlaybackState::Stopped);
    ASSERT_EQ(session.current_time(), ave::Rational(0, 1));

    std::cout << "Playback Integration Test PASSED!" << std::endl;
    return 0;
}
