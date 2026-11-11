#include "ave/media_asset.hpp"
#include "ave/media_pool.hpp"
#include "ave/playback_snapshot.hpp"
#include "ave/stream_decoder.hpp"
#include "ave/timeline.hpp"
#include "ave/timeline_prefetcher.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

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

} // namespace

int main(int argc, char* argv[]) {
    std::filesystem::path fixture_path = (argc > 1) ? argv[1] : "tests/fixtures/sample_test.mp4";
    std::cout << "[StreamDecoderTest] Testing with fixture: " << fixture_path << std::endl;

    if (!std::filesystem::exists(fixture_path)) {
        std::cerr << "Fixture file does not exist: " << fixture_path << std::endl;
        return 1;
    }

    // 1. StreamDecoder Open & Metadata Inspection
    ave::StreamDecoder decoder;
    ASSERT_TRUE(!decoder.is_open());
    ASSERT_TRUE(decoder.open(fixture_path));
    ASSERT_TRUE(decoder.is_open());
    ASSERT_EQ(decoder.width(), 320);
    ASSERT_EQ(decoder.height(), 240);
    ASSERT_TRUE(decoder.frame_rate().numerator() > 0);
    ASSERT_TRUE(decoder.timebase().numerator() > 0);

    // 2. Decode Sequential Frames
    auto frame1 = decoder.decode_next_frame(1, 320, 240);
    ASSERT_TRUE(frame1.has_value());
    ASSERT_EQ(frame1->width, 320);
    ASSERT_EQ(frame1->height, 240);
    ASSERT_EQ(frame1->seek_generation, 1);
    ASSERT_EQ(frame1->rgba_data.size(), static_cast<size_t>(320 * 240 * 4));

    auto frame2 = decoder.decode_next_frame(1, 320, 240);
    ASSERT_TRUE(frame2.has_value());
    ASSERT_TRUE(frame2->pts >= frame1->pts);

    // 3. Seek PTS
    ASSERT_TRUE(decoder.seek_pts(ave::Rational{1, 1}));
    auto frame_seek = decoder.decode_next_frame(2, 320, 240);
    ASSERT_TRUE(frame_seek.has_value());
    ASSERT_EQ(frame_seek->seek_generation, 2);

    decoder.close();
    ASSERT_TRUE(!decoder.is_open());

    // 4. Snapshot-Backed TimelinePrefetcher Integration & Stale-Work Handling
    {
        ave::Timeline timeline("tl-prefetch", "Prefetch Test", ave::Rational{24, 1}, 320, 240);
        ave::MediaPool pool;

        std::vector<ave::SourceStream> streams;
        ave::SourceStream vs(0, ave::StreamType::Video, "mpeg4", ave::Rational{1, 24}, 48);
        ave::VideoStreamInfo vinfo;
        vinfo.width = 320;
        vinfo.height = 240;
        vinfo.frame_rate = ave::Rational{24, 1};
        vs.set_video_info(vinfo);
        streams.push_back(vs);

        ave::MediaAsset asset("asset-1", fixture_path, "sample_test.mp4", 48796, "mp4", ave::Rational{2, 1}, std::move(streams));
        pool.register_asset(std::move(asset));

        auto* v1 = timeline.find_track_mut(timeline.video_tracks()[0].id());
        ASSERT_TRUE(v1 != nullptr);
        v1->add_item(ave::TimelineItem("item-1", "clip-1", "asset-1", "Sample Clip",
                                       ave::Rational{0, 1}, ave::Rational{2, 1},
                                       ave::Rational{0, 1}));

        auto snapshot_store = std::make_shared<ave::PlaybackSnapshotStore>(timeline, pool);
        const ave::PlaybackSnapshotPtr initial_snap = snapshot_store->load();
        ASSERT_EQ(initial_snap->version, 1u);

        ave::PlaybackController controller(ave::Rational{24, 1}, ave::Rational{2, 1}, 8);
        controller.set_snapshot_version(initial_snap->version);
        ave::TimelinePrefetcher prefetcher(snapshot_store, controller, ave::Rational{1, 1});

        // Test single frame prefetch under snapshot v1
        const uint64_t cur_gen = controller.clock().current_seek_generation();
        ASSERT_TRUE(prefetcher.prefetch_frame_at(ave::Rational{0, 1}, cur_gen));
        ASSERT_EQ(controller.video_queue().size(), 1u);

        auto display_frame = controller.fetch_next_display_frame();
        ASSERT_TRUE(display_frame.has_value());
        ASSERT_EQ(display_frame->pts, ave::Rational(0, 1));
        ASSERT_EQ(display_frame->snapshot_version, 1u);
        ASSERT_EQ(controller.video_queue().size(), 0u);

        // Test asynchronous prefetch worker & snapshot isolation across live mutation
        prefetcher.start();
        ASSERT_TRUE(prefetcher.is_running());

        ASSERT_TRUE(wait_until([&]() {
            return controller.video_queue().size() > 0;
        }));

        // Retain v1 reader snapshot, then mutate timeline & republish v2 while worker runs
        v1->set_muted(true);
        const ave::PlaybackSnapshotPtr snap2 = snapshot_store->publish(timeline, pool);
        ASSERT_EQ(snap2->version, 2u);
        ASSERT_TRUE(!initial_snap->timeline.video_tracks()[0].muted()); // v1 stays immutable

        // Update controller active snapshot version and drain old queued frames cleanly
        controller.set_snapshot_version(snap2->version);
        controller.video_queue().clear();
        prefetcher.trigger();

        ASSERT_TRUE(wait_until([&]() {
            auto peeked = controller.video_queue().peek();
            return peeked.has_value() && peeked->snapshot_version == snap2->version;
        }));

        auto v2_frame = controller.fetch_next_display_frame();
        ASSERT_TRUE(v2_frame.has_value());
        ASSERT_EQ(v2_frame->snapshot_version, snap2->version);

        prefetcher.stop();
        ASSERT_TRUE(!prefetcher.is_running());
    }

    std::cout << "[StreamDecoderTest] All decoder & prefetcher assertions passed!" << std::endl;
    return 0;
}
