#include "ave/stream_decoder.hpp"
#include "ave/timeline_prefetcher.hpp"
#include "ave/timeline.hpp"
#include "ave/media_pool.hpp"
#include "ave/media_asset.hpp"

#include <iostream>
#include <cassert>
#include <filesystem>
#include <thread>

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            std::cerr << "Assertion failed: " #a " == " #b << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

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

    // 4. TimelinePrefetcher Integration
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

        ave::PlaybackController controller(ave::Rational{24, 1}, ave::Rational{2, 1}, 8);
        ave::TimelinePrefetcher prefetcher(timeline, pool, controller, ave::Rational{1, 1});

        // Test single frame prefetch
        uint64_t cur_gen = controller.clock().current_seek_generation();
        ASSERT_TRUE(prefetcher.prefetch_frame_at(ave::Rational{0, 1}, cur_gen));
        ASSERT_EQ(controller.video_queue().size(), 1);

        auto display_frame = controller.fetch_next_display_frame();
        ASSERT_TRUE(display_frame.has_value());
        ASSERT_EQ(display_frame->pts, ave::Rational(0, 1));
        ASSERT_EQ(controller.video_queue().size(), 0);

        // Test asynchronous prefetch worker
        prefetcher.start();
        ASSERT_TRUE(prefetcher.is_running());

        // Wait for worker to prefetch ahead into bounded queue
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ASSERT_TRUE(controller.video_queue().size() > 0);

        prefetcher.stop();
        ASSERT_TRUE(!prefetcher.is_running());
    }

    std::cout << "[StreamDecoderTest] All decoder & prefetcher assertions passed!" << std::endl;
    return 0;
}
