#include "ave/command.hpp"
#include "ave/media_pool.hpp"
#include "ave/playback_session.hpp"
#include "ave/playback_snapshot.hpp"
#include "ave/project.hpp"
#include "ave/timecode.hpp"
#include "ave/timeline.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

template <typename Pred>
bool wait_until(Pred&& pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::yield();
    }
    return pred();
}

struct TempFileGuard {
    std::filesystem::path path;
    ~TempFileGuard() {
        if (!path.empty()) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }
};

} // namespace

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
        fixture_path = std::filesystem::current_path() / "tests/fixtures/sample_test.mp4";
    }

    ASSERT_TRUE(std::filesystem::exists(fixture_path));

    // 1. Setup Project MediaPool with Video and Audio streams
    ave::Project project("Playback Integration Project");
    {
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
        project.media_pool_mut().register_asset(std::move(asset));
    }

    // 2. Setup Timeline with 1 Video Track and 1 Audio Track
    {
        ave::Timeline timeline("tl-main", "Main Sequence", ave::Rational{24, 1}, 320, 240);
        ASSERT_EQ(timeline.video_tracks().size(), static_cast<size_t>(1));
        ASSERT_EQ(timeline.audio_tracks().size(), static_cast<size_t>(1));

        const std::string v_track_id_init = timeline.video_tracks()[0].id();
        const std::string a_track_id_init = timeline.audio_tracks()[0].id();

        ave::TimelineItem v_item("item-v1", "clip-v1", "asset-sample", "Video Clip 1",
                                 ave::Rational{0, 1}, ave::Rational{1, 1}, ave::Rational{0, 1});
        timeline.find_track_mut(v_track_id_init)->add_item(std::move(v_item));

        ave::TimelineItem a_item("item-a1", "clip-a1", "asset-sample", "Audio Clip 1",
                                 ave::Rational{0, 1}, ave::Rational{1, 1}, ave::Rational{0, 1});
        timeline.find_track_mut(a_track_id_init)->add_item(std::move(a_item));

        ASSERT_EQ(timeline.duration(), ave::Rational(1, 1));
        project.timeline_mut() = std::move(timeline);
    }

    const std::string v_track_id = project.timeline().video_tracks()[0].id();

    // 3. Setup SnapshotStore, PlaybackSession, and ProjectSnapshotCoordinator
    ave::PlaybackSessionConfig cfg;
    cfg.prefetch_window = ave::Rational{1, 1};
    cfg.video_queue_capacity = 8;
    cfg.audio_sample_rate = 48000;
    cfg.audio_channels = 2;

    auto snapshot_store = std::make_shared<ave::PlaybackSnapshotStore>(
        project.timeline(), project.media_pool());
    ASSERT_EQ(snapshot_store->current_version(), uint64_t{1});

    ave::PlaybackSession session(snapshot_store, cfg);
    ave::ProjectSnapshotCoordinator coordinator(project, snapshot_store, &session);

    ASSERT_EQ(session.state(), ave::PlaybackState::Stopped);
    ASSERT_EQ(session.current_time(), ave::Rational(0, 1));

    // 4. Test Playback Transport & Frame Prefetching
    session.play();
    ASSERT_EQ(session.state(), ave::PlaybackState::Playing);

    ASSERT_TRUE(wait_until([&]() {
        return session.controller().video_queue().size() > 0;
    }, std::chrono::milliseconds(1500)));

    int frames_presented = 0;
    for (int i = 0; i < 6; ++i) {
        auto result = session.update();
        if (result.frame.has_value()) {
            frames_presented++;
            ASSERT_EQ(result.snapshot_version, uint64_t{1});
            ASSERT_EQ(result.frame->width, 320);
            ASSERT_EQ(result.frame->height, 240);
            ASSERT_EQ(result.frame->rgba_data.size(), static_cast<size_t>(320 * 240 * 4));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    std::cout << "  Frames presented in real-time playback: " << frames_presented << std::endl;
    ASSERT_TRUE(frames_presented > 0);

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
    ASSERT_EQ(seek_res.snapshot_version, uint64_t{1});

    // 8. Test SMPTE Timecode formatting
    ave::Timecode tc = ave::Timecode::from_rational(session.current_time(), project.timeline().frame_rate());
    ASSERT_EQ(tc.hours, 0);
    ASSERT_EQ(tc.minutes, 0);
    ASSERT_EQ(tc.seconds, 0);
    ASSERT_EQ(tc.frames, 12); // 0.5s * 24fps = 12 frames
    ASSERT_EQ(tc.to_string(), "00:00:00:12");

    // Test reverse parse
    ave::Timecode tc_parsed = ave::Timecode::from_string("00:00:00:12");
    ASSERT_EQ(tc_parsed.to_rational(project.timeline().frame_rate()), ave::Rational(1, 2));

    // 9. CommandHistory Undo/Redo Republish & Clean Save Non-Republish
    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
    const std::filesystem::path save_path = temp_dir / "ave_phase10_g08_save_test.aveproj";
    const std::filesystem::path relink_path = temp_dir / "ave_phase10_g08_relink_test.mp4";
    TempFileGuard save_guard{save_path};
    TempFileGuard relink_guard{relink_path};

    {
        const uint64_t v_before = snapshot_store->current_version();
        ASSERT_TRUE(project.execute_command(
            std::make_unique<ave::SetTrackMuteCommand>(v_track_id, true)).is_success());
        ASSERT_EQ(snapshot_store->current_version(), v_before + 1);

        // Saving calls CommandHistory::mark_clean() and MUST NOT spuriously republish snapshots
        ASSERT_TRUE(project.save(save_path));
        ASSERT_EQ(snapshot_store->current_version(), v_before + 1);

        ASSERT_TRUE(project.undo().is_success());
        ASSERT_EQ(snapshot_store->current_version(), v_before + 2);
        ASSERT_TRUE(project.redo().is_success());
        ASSERT_EQ(snapshot_store->current_version(), v_before + 3);
        ASSERT_TRUE(project.undo().is_success());
        ASSERT_EQ(snapshot_store->current_version(), v_before + 4);
    }

    // 10. Concurrent Multi-Thread Edit + Prefetch + Update/Audio Stress Test
    {
        session.set_loop(true);
        session.seek(ave::Rational{0, 1});
        session.play();

        const ave::PlaybackSnapshotPtr retained_baseline = snapshot_store->load();
        ASSERT_TRUE(!retained_baseline->timeline.video_tracks()[0].muted());
        ASSERT_EQ(retained_baseline->timeline.video_tracks()[0].items().size(), static_cast<size_t>(1));

        std::atomic<bool> writer_done{false};
        std::atomic<bool> reader_failed{false};
        std::atomic<uint64_t> max_reader_snapshot{retained_baseline->version};
        std::atomic<int> reader_frames{0};

        std::thread reader_thread([&]() {
            while (!writer_done.load(std::memory_order_acquire)) {
                const ave::PlaybackSnapshotPtr snap = snapshot_store->load();
                if (!snap || snap->version < retained_baseline->version ||
                    snap->timeline.video_tracks().empty() ||
                    !snap->find_asset("asset-sample")) {
                    reader_failed.store(true, std::memory_order_release);
                    break;
                }
                const uint64_t prev_max = max_reader_snapshot.load(std::memory_order_relaxed);
                if (snap->version > prev_max) {
                    max_reader_snapshot.store(snap->version, std::memory_order_release);
                }

                const auto pres = session.update();
                if (pres.frame.has_value()) {
                    reader_frames.fetch_add(1, std::memory_order_relaxed);
                    if (pres.frame->width != 320 || pres.frame->height != 240 ||
                        pres.frame->rgba_data.size() != static_cast<size_t>(320 * 240 * 4)) {
                        reader_failed.store(true, std::memory_order_release);
                        break;
                    }
                }
                session.pump_audio(1024);
                std::this_thread::yield();
            }
        });

        for (int iter = 0; iter < 15; ++iter) {
            const std::string extra_id = "stress-item-" + std::to_string(iter);
            ave::TimelineItem extra_item(extra_id, "stress-clip", "asset-sample", "Stress Clip",
                                         ave::Rational{1, 1}, ave::Rational{1, 1}, ave::Rational{0, 1});
            ASSERT_TRUE(project.execute_command(
                std::make_unique<ave::InsertClipCommand>(v_track_id, extra_item)).is_success());
            ASSERT_TRUE(project.execute_command(
                std::make_unique<ave::SetTrackMuteCommand>(v_track_id, true)).is_success());
            ASSERT_TRUE(project.undo().is_success());
            ASSERT_TRUE(project.undo().is_success());
            std::this_thread::yield();
        }

        // Allow reader thread to observe the final published snapshot cleanly before stopping
        ASSERT_TRUE(wait_until([&]() {
            return max_reader_snapshot.load(std::memory_order_acquire) >= snapshot_store->current_version();
        }, std::chrono::milliseconds(3000)));

        writer_done.store(true, std::memory_order_release);
        reader_thread.join();

        ASSERT_TRUE(!reader_failed.load(std::memory_order_acquire));
        ASSERT_TRUE(reader_frames.load(std::memory_order_acquire) > 0);
        ASSERT_EQ(max_reader_snapshot.load(std::memory_order_acquire), snapshot_store->current_version());

        // Retained reader baseline MUST have remained completely immutable throughout concurrent writes
        ASSERT_TRUE(!retained_baseline->timeline.video_tracks()[0].muted());
        ASSERT_EQ(retained_baseline->timeline.video_tracks()[0].items().size(), static_cast<size_t>(1));
        ASSERT_EQ(snapshot_store->load()->timeline.video_tracks()[0].items().size(), static_cast<size_t>(1));
        session.pause();
    }

    // 11. Asset Relinking Updates Snapshot & Reopens Decoders Cleanly
    {
        std::error_code ec;
        std::filesystem::copy_file(fixture_path, relink_path, std::filesystem::copy_options::overwrite_existing, ec);
        ASSERT_TRUE(!ec);
        ASSERT_TRUE(std::filesystem::exists(relink_path));

        const uint64_t ver_before_relink = snapshot_store->current_version();
        ASSERT_TRUE(coordinator.relink_asset("asset-sample", relink_path));
        ASSERT_EQ(snapshot_store->current_version(), ver_before_relink + 1);

        const ave::PlaybackSnapshotPtr relinked_snap = snapshot_store->load();
        ASSERT_TRUE(relinked_snap->find_asset("asset-sample") != nullptr);
        ASSERT_EQ(relinked_snap->find_asset("asset-sample")->file_path(), relink_path);

        session.seek(ave::Rational{0, 1});
        session.pump_audio(1024);
        const auto relink_pres = session.update();
        ASSERT_TRUE(relink_pres.frame.has_value());
        ASSERT_EQ(relink_pres.snapshot_version, relinked_snap->version);
    }

    // 12. Structural Project Replacement Resets Session, Clock & Rebinds History
    {
        ave::Project replacement("Replaced Sequence Project");
        ave::Timeline new_tl("tl-new", "Replaced 640x360", ave::Rational{30, 1}, 640, 360);
        const std::string new_v1 = new_tl.video_tracks()[0].id();
        replacement.timeline_mut() = std::move(new_tl);

        const uint64_t ver_before_replace = snapshot_store->current_version();
        coordinator.replace_project(std::move(replacement));

        ASSERT_EQ(session.state(), ave::PlaybackState::Stopped);
        ASSERT_EQ(session.current_time(), ave::Rational(0, 1));
        ASSERT_EQ(session.clock().frame_rate(), ave::Rational(30, 1));
        ASSERT_EQ(snapshot_store->current_version(), ver_before_replace + 1);

        ASSERT_EQ(project.name(), "Replaced Sequence Project");
        ASSERT_EQ(project.timeline().width(), 640);
        ASSERT_EQ(project.timeline().height(), 360);

        const ave::PlaybackSnapshotPtr replaced_snap = snapshot_store->load();
        ASSERT_EQ(replaced_snap->timeline.width(), 640);
        ASSERT_EQ(replaced_snap->timeline.height(), 360);
        ASSERT_TRUE(replaced_snap->assets_by_id.empty());

        const auto post_res = session.update();
        ASSERT_TRUE(post_res.frame.has_value());
        ASSERT_EQ(post_res.snapshot_version, replaced_snap->version);
        ASSERT_EQ(post_res.frame->width, 640);
        ASSERT_EQ(post_res.frame->height, 360);

        // Verify new project's CommandHistory listener was cleanly bound
        ASSERT_TRUE(project.execute_command(
            std::make_unique<ave::SetTrackMuteCommand>(new_v1, true)).is_success());
        ASSERT_EQ(snapshot_store->current_version(), ver_before_replace + 2);
        ASSERT_TRUE(snapshot_store->load()->timeline.video_tracks()[0].muted());
    }

    // 13. Clean Shutdown & File Handle Release Verification
    session.stop();
    ASSERT_EQ(session.state(), ave::PlaybackState::Stopped);
    ASSERT_EQ(session.current_time(), ave::Rational(0, 1));
    session.shutdown();

    std::error_code rm_ec;
    ASSERT_TRUE(std::filesystem::remove(relink_path, rm_ec));
    relink_guard.path.clear();

    std::cout << "Playback Integration Test PASSED!" << std::endl;
    return 0;
}
