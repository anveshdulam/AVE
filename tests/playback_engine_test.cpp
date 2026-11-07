#include "ave/playback_clock.hpp"
#include "ave/frame_queue.hpp"
#include "ave/playback_controller.hpp"

#include <iostream>
#include <thread>
#include <cassert>

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

int main() {
    std::cout << "[PlaybackEngineTest] Starting playback engine verification..." << std::endl;

    // 1. Playback Clock State Machine & Frame Stepping
    {
        ave::PlaybackClock clock(ave::Rational{24, 1}, ave::Rational{100, 1});
        ASSERT_EQ(clock.state(), ave::PlaybackState::Stopped);
        ASSERT_EQ(clock.current_time(), ave::Rational(0, 1));

        // Step forward 1 frame = 1/24 sec
        clock.step_forward(1);
        ASSERT_EQ(clock.state(), ave::PlaybackState::Paused);
        ASSERT_EQ(clock.current_time(), ave::Rational(1, 24));

        // Step forward 23 frames -> total 24 frames = 1 full second
        clock.step_forward(23);
        ASSERT_EQ(clock.current_time(), ave::Rational(1, 1));

        // Step backward 1 frame -> 23/24 sec
        clock.step_backward(1);
        ASSERT_EQ(clock.current_time(), ave::Rational(23, 24));

        // Clamping to 0
        clock.step_backward(50);
        ASSERT_EQ(clock.current_time(), ave::Rational(0, 1));

        // Seek
        uint64_t gen1 = clock.seek(ave::Rational{10, 1});
        ASSERT_EQ(clock.current_time(), ave::Rational(10, 1));
        ASSERT_TRUE(gen1 > 0);

        uint64_t gen2 = clock.seek(ave::Rational{15, 1});
        ASSERT_EQ(clock.current_time(), ave::Rational(15, 1));
        ASSERT_TRUE(gen2 > gen1);

        // Stop resets to 0
        clock.stop();
        ASSERT_EQ(clock.state(), ave::PlaybackState::Stopped);
        ASSERT_EQ(clock.current_time(), ave::Rational(0, 1));
    }

    // 2. Scrubbing Mode
    {
        ave::PlaybackClock clock(ave::Rational{30, 1}, ave::Rational{60, 1});
        uint64_t scrub_gen = clock.start_scrub(ave::Rational{5, 1});
        ASSERT_EQ(clock.state(), ave::PlaybackState::Scrubbing);
        ASSERT_EQ(clock.current_time(), ave::Rational(5, 1));

        clock.update_scrub(ave::Rational{8, 1});
        ASSERT_EQ(clock.current_time(), ave::Rational(8, 1));
        ASSERT_EQ(clock.current_seek_generation(), scrub_gen);

        clock.end_scrub();
        ASSERT_EQ(clock.state(), ave::PlaybackState::Paused);
        ASSERT_EQ(clock.current_time(), ave::Rational(8, 1));
    }

    // 3. SystemClock Playback Monotonic Time Advance
    {
        ave::PlaybackClock clock(ave::Rational{24, 1}, ave::Rational{100, 1});
        clock.play(1.0);
        ASSERT_EQ(clock.state(), ave::PlaybackState::Playing);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ave::Rational t1 = clock.current_time();
        ASSERT_TRUE(t1 > ave::Rational(0, 1));

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ave::Rational t2 = clock.current_time();
        ASSERT_TRUE(t2 > t1);

        clock.pause();
        ASSERT_EQ(clock.state(), ave::PlaybackState::Paused);
        ave::Rational paused_time = clock.current_time();

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        ASSERT_EQ(clock.current_time(), paused_time);
    }

    // 4. BoundedFrameQueue Backpressure
    {
        ave::BoundedFrameQueue<ave::VideoFrame> queue(3);
        ASSERT_EQ(queue.capacity(), 3);
        ASSERT_EQ(queue.size(), 0);
        ASSERT_TRUE(queue.is_empty());

        ave::VideoFrame f1; f1.pts = ave::Rational{0, 1};
        ave::VideoFrame f2; f2.pts = ave::Rational{1, 24};
        ave::VideoFrame f3; f3.pts = ave::Rational{2, 24};
        ave::VideoFrame f4; f4.pts = ave::Rational{3, 24};

        ASSERT_TRUE(queue.push(f1, std::chrono::milliseconds(10)));
        ASSERT_TRUE(queue.push(f2, std::chrono::milliseconds(10)));
        ASSERT_TRUE(queue.push(f3, std::chrono::milliseconds(10)));
        ASSERT_TRUE(queue.is_full());

        // 4th push must time out (backpressure!)
        ASSERT_TRUE(!queue.push(f4, std::chrono::milliseconds(10)));

        // Pop one
        auto popped = queue.pop(std::chrono::milliseconds(10));
        ASSERT_TRUE(popped.has_value());
        ASSERT_EQ(popped->pts, ave::Rational(0, 1));
        ASSERT_EQ(queue.size(), 2);

        // Now 4th push succeeds
        ASSERT_TRUE(queue.push(f4, std::chrono::milliseconds(10)));
        ASSERT_EQ(queue.size(), 3);

        queue.clear();
        ASSERT_EQ(queue.size(), 0);
        ASSERT_TRUE(queue.is_empty());
    }

    // 5. PlaybackController A/V Synchronization & Stale Frame Dropping
    {
        ave::PlaybackController controller(ave::Rational{24, 1}, ave::Rational{10, 1}, 10);
        uint64_t gen = controller.clock().seek(ave::Rational{2, 1});

        // Test frame with stale seek generation
        ave::VideoFrame stale_frame;
        stale_frame.seek_generation = gen - 1;
        stale_frame.pts = ave::Rational{2, 1};
        stale_frame.duration = ave::Rational{1, 24};
        ASSERT_EQ(controller.evaluate_frame(stale_frame, ave::Rational(2, 1)), ave::FrameSyncAction::Stale);

        // Test frame too far in the past (late -> Drop)
        ave::VideoFrame late_frame;
        late_frame.seek_generation = gen;
        late_frame.pts = ave::Rational{0, 1}; // 2 seconds behind
        late_frame.duration = ave::Rational{1, 24};
        ASSERT_EQ(controller.evaluate_frame(late_frame, ave::Rational(2, 1)), ave::FrameSyncAction::Drop);

        // Test frame in the future (early -> Hold)
        ave::VideoFrame early_frame;
        early_frame.seek_generation = gen;
        early_frame.pts = ave::Rational{5, 1}; // 3 seconds ahead
        early_frame.duration = ave::Rational{1, 24};
        ASSERT_EQ(controller.evaluate_frame(early_frame, ave::Rational(2, 1)), ave::FrameSyncAction::Hold);

        // Test frame in sync window -> Present
        ave::VideoFrame sync_frame;
        sync_frame.seek_generation = gen;
        sync_frame.pts = ave::Rational{2, 1};
        sync_frame.duration = ave::Rational{1, 24};
        ASSERT_EQ(controller.evaluate_frame(sync_frame, ave::Rational(2, 1)), ave::FrameSyncAction::Present);

        // Test fetch_next_display_frame automatic drain
        controller.video_queue().push(stale_frame);
        controller.video_queue().push(late_frame);
        controller.video_queue().push(sync_frame);

        auto displayed = controller.fetch_next_display_frame();
        ASSERT_TRUE(displayed.has_value());
        ASSERT_EQ(displayed->pts, ave::Rational(2, 1));

        auto metrics = controller.metrics();
        ASSERT_EQ(metrics.frames_dropped, 2);  // Stale and late frames dropped
        ASSERT_EQ(metrics.frames_rendered, 1); // Sync frame rendered
    }

    std::cout << "[PlaybackEngineTest] All playback engine assertions passed!" << std::endl;
    return 0;
}
