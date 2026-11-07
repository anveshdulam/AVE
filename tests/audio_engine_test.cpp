#include "ave/audio_buffer.hpp"
#include "ave/audio_ring_buffer.hpp"
#include "ave/audio_mixer.hpp"
#include "ave/audio_device.hpp"

#include <iostream>
#include <cmath>
#include <cassert>

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

#define ASSERT_NEAR(a, b, eps) \
    do { \
        if (std::abs((a) - (b)) > (eps)) { \
            std::cerr << "Assertion failed: " #a " (" << (a) << ") ~= " #b " (" << (b) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

int main() {
    std::cout << "[AudioEngineTest] Starting audio engine verification..." << std::endl;

    // 1. AudioBuffer Basic Operations & Metrics
    {
        ave::AudioBuffer buffer(4, 2, 48000); // 4 stereo frames
        ASSERT_TRUE(buffer.frame_count() == 4);
        ASSERT_TRUE(buffer.sample_count() == 8);
        ASSERT_TRUE(buffer.channels() == 2);

        float* data = buffer.data();
        for (size_t i = 0; i < 8; ++i) {
            data[i] = 0.5f;
        }

        buffer.apply_gain(2.0f);
        ASSERT_NEAR(buffer.data()[0], 1.0f, 1e-4f);

        float peak = 0.0f, rms = 0.0f;
        buffer.compute_peak_and_rms(peak, rms);
        ASSERT_NEAR(peak, 1.0f, 1e-4f);
        ASSERT_NEAR(rms, 1.0f, 1e-4f);

        // Soft limiter test
        buffer.data()[0] = 2.0f; // Excessive amplitude
        buffer.apply_soft_limiter(0.95f);
        ASSERT_TRUE(buffer.data()[0] <= 1.0f); // Clamped smoothly to <= 1.0
        ASSERT_TRUE(buffer.data()[0] > 0.95f);
    }

    // 2. Lock-free AudioRingBuffer (SPSC)
    {
        ave::AudioRingBuffer ring(16, 2); // 16 frames capacity
        ASSERT_TRUE(ring.frame_capacity() == 16);
        ASSERT_TRUE(ring.available_read_frames() == 0);
        ASSERT_TRUE(ring.available_write_frames() == 15); // N-1 in circular buffer

        std::vector<float> write_data(8 * 2, 0.75f); // 8 stereo frames
        size_t written = ring.write(write_data.data(), 8);
        ASSERT_TRUE(written == 8);
        ASSERT_TRUE(ring.available_read_frames() == 8);

        std::vector<float> read_data(10 * 2, 0.0f); // Attempt to read 10 frames (2 frames underrun)
        size_t read = ring.read(read_data.data(), 10);
        ASSERT_TRUE(read == 8); // Only 8 available

        // First 8 frames should be 0.75f, last 2 frames must be 0.0f (underrun silence)
        ASSERT_NEAR(read_data[0], 0.75f, 1e-4f);
        ASSERT_NEAR(read_data[15], 0.75f, 1e-4f);
        ASSERT_NEAR(read_data[16], 0.0f, 1e-4f); // Underrun zero-filled
        ASSERT_NEAR(read_data[19], 0.0f, 1e-4f);
    }

    // 3. AudioMixer: Panning, Volume, Solo, and Mute
    {
        ave::AudioMixer mixer;
        ave::AudioBuffer track1(10, 2, 48000);
        ave::AudioBuffer track2(10, 2, 48000);
        ave::AudioBuffer master(10, 2, 48000);

        // Fill track 1 with 1.0, track 2 with 0.5
        std::fill_n(track1.data(), track1.sample_count(), 1.0f);
        std::fill_n(track2.data(), track2.sample_count(), 0.5f);

        // Test Pan: Track 1 hard left (-1.0), Track 2 hard right (+1.0)
        mixer.set_track_params("T1", {1.0f, -1.0f, false, false});
        mixer.set_track_params("T2", {1.0f,  1.0f, false, false});

        std::vector<std::pair<std::string, const ave::AudioBuffer*>> inputs = {
            {"T1", &track1},
            {"T2", &track2}
        };

        mixer.mix(inputs, master);

        // Left channel should receive Track 1 exclusively; Right channel Track 2 exclusively
        ASSERT_TRUE(master.data()[0] > 0.8f);
        ASSERT_NEAR(master.data()[1], 0.5f, 0.05f);

        // Test Mute: Mute Track 1 -> Left channel becomes silent
        mixer.set_track_params("T1", {1.0f, -1.0f, true, false});
        mixer.mix(inputs, master);
        ASSERT_NEAR(master.data()[0], 0.0f, 1e-4f);
        ASSERT_TRUE(master.data()[1] > 0.0f);

        // Test Solo: Solo Track 1 -> Track 2 muted automatically
        mixer.set_track_params("T1", {1.0f, 0.0f, false, true});
        mixer.set_track_params("T2", {1.0f, 0.0f, false, false});
        mixer.mix(inputs, master);
        // Only Track 1 is mixed
        ASSERT_TRUE(master.data()[0] > 0.0f);
        ASSERT_TRUE(master.data()[1] > 0.0f);
    }

    // 4. NullAudioDevice Loopback Callback Execution
    {
        ave::NullAudioDevice device(48000, 2, 256);
        ASSERT_TRUE(!device.is_running());

        bool callback_invoked = false;
        bool callback_valid = true;
        device.start([&](float* out_buf, size_t frames, int channels, int rate) -> void {
            callback_invoked = true;
            if (frames != 256 || channels != 2 || rate != 48000) {
                callback_valid = false;
            }
            std::fill_n(out_buf, frames * static_cast<size_t>(channels), 0.123f);
        });

        ASSERT_TRUE(device.is_running());

        // Synchronous test render
        std::vector<float> test_buf(256 * 2, 0.0f);
        device.render_synchronous(256, test_buf.data());
        ASSERT_TRUE(callback_invoked);
        ASSERT_TRUE(callback_valid);
        ASSERT_NEAR(test_buf[0], 0.123f, 1e-4f);

        device.stop();
        ASSERT_TRUE(!device.is_running());
    }

    std::cout << "[AudioEngineTest] All audio engine assertions passed!" << std::endl;
    return 0;
}
