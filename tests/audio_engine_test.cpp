#include "ave/audio_buffer.hpp"
#include "ave/audio_device.hpp"
#include "ave/audio_mixer.hpp"
#include "ave/audio_ring_buffer.hpp"
#include "ave/wasapi_audio_device.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

namespace {

constexpr double kTwoPi = 6.28318530717958647692;

template <typename Pred>
bool wait_until(Pred&& pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(1500)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

struct MockEndpointBehavior {
    bool fail_open{false};
    bool fail_get_mix_format{false};
    bool reject_desired_format{false};
    bool provide_closest_format{false};
    bool reject_event_mode{false};
    bool fail_start{false};
    bool return_null_buffer{false};
    bool invalidate_on_padding{false};
    bool invalidate_on_release{false};
    size_t hw_buffer_frames{256};
    std::string device_name{"Mock Studio Monitors (WASAPI)"};
    ave::WasapiEndpointFormat mix_format{48000, 2, ave::AudioSampleFormat::Float32, 32, false, 0};
    ave::WasapiEndpointFormat closest_format{48000, 2, ave::AudioSampleFormat::Float32, 32, false, 0};
};

struct MockEndpointStats {
    std::mutex mutex;
    std::atomic<int> open_calls{0};
    std::atomic<int> start_calls{0};
    std::atomic<int> stop_calls{0};
    std::atomic<int> reset_calls{0};
    std::atomic<int> close_calls{0};
    std::atomic<int> release_calls{0};
    std::atomic<size_t> total_frames_released{0};
    std::atomic<size_t> silent_releases{0};
    std::atomic<size_t> simulated_padding_frames{0};
    std::atomic<bool> force_invalidate_next{false};
    ave::WasapiEndpointFormat initialized_format{};
    std::vector<uint8_t> captured_bytes{};
    std::thread::id worker_thread_id{};
};

class MockWasapiEndpoint final : public ave::IWasapiEndpoint {
public:
    MockWasapiEndpoint(MockEndpointBehavior behavior, std::shared_ptr<MockEndpointStats> stats)
        : behavior_(std::move(behavior)), stats_(std::move(stats)) {
#ifdef _WIN32
        wait_event_ = static_cast<void*>(CreateEventW(nullptr, FALSE, FALSE, nullptr));
#endif
    }

    ~MockWasapiEndpoint() override {
#ifdef _WIN32
        if (wait_event_) {
            CloseHandle(static_cast<HANDLE>(wait_event_));
            wait_event_ = nullptr;
        }
#endif
    }

    ave::WasapiStatus open_default_render_device(std::string& out_device_name) override {
        stats_->worker_thread_id = std::this_thread::get_id();
        stats_->open_calls.fetch_add(1, std::memory_order_relaxed);
        if (behavior_.fail_open) {
            ave::WasapiStatus s;
            s.ok = false;
            s.hresult = static_cast<long>(0x80070490L); // E_NOT_SET
            s.error_message = "No default audio render endpoint found";
            return s;
        }
        out_device_name = behavior_.device_name;
        return {};
    }

    ave::WasapiStatus get_mix_format(ave::WasapiEndpointFormat& out_format) override {
        if (behavior_.fail_get_mix_format) {
            ave::WasapiStatus s;
            s.ok = false;
            s.error_message = "Mock GetMixFormat failed";
            return s;
        }
        out_format = behavior_.mix_format;
        return {};
    }

    bool is_format_supported_shared(const ave::WasapiEndpointFormat& desired,
                                    ave::WasapiEndpointFormat& out_closest,
                                    bool& out_has_closest) override {
        if (behavior_.reject_desired_format) {
            if (behavior_.provide_closest_format) {
                out_has_closest = true;
                out_closest = behavior_.closest_format;
            } else {
                out_has_closest = false;
            }
            return false;
        }
        out_has_closest = false;
        (void)desired;
        return true;
    }

    ave::WasapiStatus initialize_client(const ave::WasapiEndpointFormat& format,
                                           size_t requested_buffer_frames,
                                           bool prefer_event_callback,
                                           size_t& out_buffer_frames,
                                           bool& out_event_mode_active) override {
        (void)requested_buffer_frames;
        stats_->initialized_format = format;
        const size_t bpf = bytes_per_frame(format);
        staging_.assign(behavior_.hw_buffer_frames * bpf, 0);
        out_buffer_frames = behavior_.hw_buffer_frames;
        out_event_mode_active = prefer_event_callback && !behavior_.reject_event_mode;
        return {};
    }

    void* native_wait_handle() const noexcept override {
        return wait_event_;
    }

    ave::WasapiStatus get_current_padding(size_t& out_padding_frames) override {
        if (behavior_.invalidate_on_padding ||
            stats_->force_invalidate_next.load(std::memory_order_acquire)) {
            ave::WasapiStatus s;
            s.ok = false;
            s.device_invalidated = true;
            s.hresult = static_cast<long>(0x88890004L); // AUDCLNT_E_DEVICE_INVALIDATED
            s.error_message = "AUDCLNT_E_DEVICE_INVALIDATED (Mock unplugged)";
            return s;
        }
        out_padding_frames = stats_->simulated_padding_frames.exchange(0, std::memory_order_acq_rel);
        return {};
    }

    ave::WasapiStatus start() override {
        if (behavior_.fail_start) {
            ave::WasapiStatus s;
            s.ok = false;
            s.error_message = "Mock IAudioClient::Start failed";
            return s;
        }
        stats_->start_calls.fetch_add(1, std::memory_order_relaxed);
#ifdef _WIN32
        if (wait_event_ && !behavior_.reject_event_mode) {
            SetEvent(static_cast<HANDLE>(wait_event_));
        }
#endif
        return {};
    }

    ave::WasapiStatus stop() override {
        stats_->stop_calls.fetch_add(1, std::memory_order_relaxed);
        return {};
    }

    ave::WasapiStatus reset() override {
        stats_->reset_calls.fetch_add(1, std::memory_order_relaxed);
        return {};
    }

    ave::WasapiStatus get_buffer(size_t frames_to_write, uint8_t** out_data) override {
        if (behavior_.return_null_buffer) {
            *out_data = nullptr;
            ave::WasapiStatus s;
            s.ok = false;
            s.error_message = "Mock GetBuffer returned null pointer";
            return s;
        }
        const size_t needed = frames_to_write * bytes_per_frame(stats_->initialized_format);
        if (staging_.size() < needed) {
            staging_.resize(needed, 0);
        }
        *out_data = staging_.data();
        return {};
    }

    ave::WasapiStatus release_buffer(size_t frames_written, bool silent) override {
        if (behavior_.invalidate_on_release) {
            ave::WasapiStatus s;
            s.ok = false;
            s.device_invalidated = true;
            s.hresult = static_cast<long>(0x88890004L);
            s.error_message = "AUDCLNT_E_DEVICE_INVALIDATED on ReleaseBuffer";
            return s;
        }
        const size_t bytes = frames_written * bytes_per_frame(stats_->initialized_format);
        {
            std::lock_guard<std::mutex> lock(stats_->mutex);
            if (silent) {
                stats_->captured_bytes.insert(stats_->captured_bytes.end(), bytes, 0);
            } else {
                stats_->captured_bytes.insert(stats_->captured_bytes.end(),
                                              staging_.begin(),
                                              staging_.begin() + static_cast<std::ptrdiff_t>(bytes));
            }
        }
        stats_->release_calls.fetch_add(1, std::memory_order_relaxed);
        stats_->total_frames_released.fetch_add(frames_written, std::memory_order_relaxed);
        if (silent) {
            stats_->silent_releases.fetch_add(1, std::memory_order_relaxed);
        }
        return {};
    }

    void close() override {
        stats_->close_calls.fetch_add(1, std::memory_order_relaxed);
    }

    void pulse_event() noexcept {
#ifdef _WIN32
        if (wait_event_) {
            SetEvent(static_cast<HANDLE>(wait_event_));
        }
#endif
    }

private:
    static size_t bytes_per_frame(const ave::WasapiEndpointFormat& f) noexcept {
        size_t bps = 4;
        switch (f.sample_format) {
            case ave::AudioSampleFormat::Int16:   bps = 2; break;
            case ave::AudioSampleFormat::Int24:   bps = 3; break;
            case ave::AudioSampleFormat::Int32:   bps = 4; break;
            case ave::AudioSampleFormat::Float32:
            default:                              bps = 4; break;
        }
        return bps * static_cast<size_t>(std::max(1, f.channels));
    }

    MockEndpointBehavior behavior_;
    std::shared_ptr<MockEndpointStats> stats_;
    std::vector<uint8_t> staging_{};
    void* wait_event_{nullptr};
};

} // namespace

int main() {
    std::cout << "[AudioEngineTest] Starting audio engine & WASAPI verification..." << std::endl;

    // 1. AudioBuffer Basic Operations & Metrics
    {
        ave::AudioBuffer buffer(4, 2, 48000);
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

        buffer.data()[0] = 2.0f;
        buffer.apply_soft_limiter(0.95f);
        ASSERT_TRUE(buffer.data()[0] <= 1.0f);
        ASSERT_TRUE(buffer.data()[0] > 0.95f);
    }

    // 2. Lock-free AudioRingBuffer (SPSC)
    {
        ave::AudioRingBuffer ring(16, 2);
        ASSERT_TRUE(ring.frame_capacity() == 16);
        ASSERT_TRUE(ring.available_read_frames() == 0);
        ASSERT_TRUE(ring.available_write_frames() == 15);

        std::vector<float> write_data(8 * 2, 0.75f);
        size_t written = ring.write(write_data.data(), 8);
        ASSERT_TRUE(written == 8);
        ASSERT_TRUE(ring.available_read_frames() == 8);

        std::vector<float> read_data(10 * 2, 0.0f);
        size_t read = ring.read(read_data.data(), 10);
        ASSERT_TRUE(read == 8);
        ASSERT_NEAR(read_data[0], 0.75f, 1e-4f);
        ASSERT_NEAR(read_data[15], 0.75f, 1e-4f);
        ASSERT_NEAR(read_data[16], 0.0f, 1e-4f);
        ASSERT_NEAR(read_data[19], 0.0f, 1e-4f);
    }

    // 3. AudioMixer: Panning, Volume, Solo, and Mute
    {
        ave::AudioMixer mixer;
        ave::AudioBuffer track1(10, 2, 48000);
        ave::AudioBuffer track2(10, 2, 48000);
        ave::AudioBuffer master(10, 2, 48000);

        std::fill_n(track1.data(), track1.sample_count(), 1.0f);
        std::fill_n(track2.data(), track2.sample_count(), 0.5f);

        mixer.set_track_params("T1", {1.0f, -1.0f, false, false});
        mixer.set_track_params("T2", {1.0f,  1.0f, false, false});

        std::vector<std::pair<std::string, const ave::AudioBuffer*>> inputs = {
            {"T1", &track1},
            {"T2", &track2}
        };

        mixer.mix(inputs, master);
        ASSERT_TRUE(master.data()[0] > 0.8f);
        ASSERT_NEAR(master.data()[1], 0.5f, 0.05f);

        mixer.set_track_params("T1", {1.0f, -1.0f, true, false});
        mixer.mix(inputs, master);
        ASSERT_NEAR(master.data()[0], 0.0f, 1e-4f);
        ASSERT_TRUE(master.data()[1] > 0.0f);

        mixer.set_track_params("T1", {1.0f, 0.0f, false, true});
        mixer.set_track_params("T2", {1.0f, 0.0f, false, false});
        mixer.mix(inputs, master);
        ASSERT_TRUE(master.data()[0] > 0.0f);
        ASSERT_TRUE(master.data()[1] > 0.0f);
    }

    // 4. NullAudioDevice Loopback Callback, Push Queue & Pause/Flush Lifecycle
    {
        ave::NullAudioDevice device(48000, 2, 256);
        ASSERT_TRUE(!device.is_running());
        ASSERT_TRUE(!device.is_available());
        ASSERT_TRUE(device.state() == ave::AudioDeviceState::Stopped);

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
        std::vector<float> test_buf(256 * 2, 0.0f);
        device.render_synchronous(256, test_buf.data());
        ASSERT_TRUE(callback_invoked);
        ASSERT_TRUE(callback_valid);
        ASSERT_NEAR(test_buf[0], 0.123f, 1e-4f);

        ASSERT_TRUE(device.pause());
        ASSERT_TRUE(device.is_paused());
        ASSERT_TRUE(device.resume());
        ASSERT_TRUE(device.is_running());

        device.stop();
        ASSERT_TRUE(!device.is_running());
        ASSERT_TRUE(device.state() == ave::AudioDeviceState::Stopped);

        // Push queue & sanitization + bounded capacity in NullAudioDevice
        std::vector<float> dirty(64 * 2, 0.25f);
        dirty[0] = std::numeric_limits<float>::quiet_NaN();
        dirty[1] = 4.0f;
        dirty[2] = -std::numeric_limits<float>::infinity();
        const size_t accepted = device.submit_pcm(dirty.data(), 64);
        ASSERT_TRUE(accepted == 64);
        ASSERT_TRUE(device.queued_frames() == 64);

        device.flush();
        ASSERT_TRUE(device.queued_frames() == 0);
        device.shutdown();
    }

    // 5. Scenario 1: Mock WASAPI Initialization Success (Event Mode + Timer Fallback) & COM Thread Isolation
    {
        auto stats = std::make_shared<MockEndpointStats>();
        MockEndpointBehavior behavior;
        behavior.hw_buffer_frames = 256;
        ave::WasapiAudioDevice wasapi([behavior, stats]() {
            return std::make_unique<MockWasapiEndpoint>(behavior, stats);
        });

        ave::AudioDeviceConfig cfg;
        cfg.sample_rate = 48000;
        cfg.channels = 2;
        cfg.buffer_frames = 512;
        cfg.sample_format = ave::AudioSampleFormat::Float32;

        ASSERT_TRUE(wasapi.initialize(cfg));
        ASSERT_TRUE(wasapi.is_available());
        ASSERT_TRUE(wasapi.state() == ave::AudioDeviceState::Stopped);
        ASSERT_TRUE(wasapi.backend_name() == "WASAPI");
        ASSERT_TRUE(wasapi.device_name() == behavior.device_name);
        ASSERT_TRUE(wasapi.sample_rate() == 48000);
        ASSERT_TRUE(wasapi.channels() == 2);
        ASSERT_TRUE(wasapi.buffer_frames() == 256);
        ASSERT_TRUE(wasapi.is_event_driven());
        ASSERT_TRUE(wasapi.worker_thread_id() != std::thread::id{});
        ASSERT_TRUE(wasapi.worker_thread_id() != std::this_thread::get_id());
        ASSERT_TRUE(stats->worker_thread_id == wasapi.worker_thread_id());

        wasapi.shutdown();
        ASSERT_TRUE(!wasapi.is_available());
        ASSERT_TRUE(stats->close_calls.load() >= 1);

        // Event-callback rejected -> timer polling fallback active and still delivers audio
        auto poll_stats = std::make_shared<MockEndpointStats>();
        MockEndpointBehavior poll_behavior;
        poll_behavior.reject_event_mode = true;
        poll_behavior.hw_buffer_frames = 128;
        ave::WasapiAudioDevice poll_wasapi([poll_behavior, poll_stats]() {
            return std::make_unique<MockWasapiEndpoint>(poll_behavior, poll_stats);
        });
        ASSERT_TRUE(poll_wasapi.initialize(cfg));
        ASSERT_TRUE(!poll_wasapi.is_event_driven());
        ASSERT_TRUE(poll_wasapi.start([](float* out, size_t n, int ch, int) {
            std::fill_n(out, n * static_cast<size_t>(ch), 0.25f);
        }));
        ASSERT_TRUE(wait_until([&]() {
            return poll_stats->total_frames_released.load() >= 256;
        }));
        poll_wasapi.shutdown();
    }

    // 6. Scenario 2: Mock WASAPI Initialization Failures & Invalid Config
    {
        {
            auto stats = std::make_shared<MockEndpointStats>();
            MockEndpointBehavior behavior;
            behavior.fail_open = true;
            ave::WasapiAudioDevice wasapi([behavior, stats]() {
                return std::make_unique<MockWasapiEndpoint>(behavior, stats);
            });
            ASSERT_TRUE(!wasapi.initialize(ave::AudioDeviceConfig{}));
            ASSERT_TRUE(!wasapi.is_available());
            ASSERT_TRUE(wasapi.state() == ave::AudioDeviceState::Error);
            ASSERT_TRUE(!wasapi.last_error().empty());
            ASSERT_TRUE(wasapi.diagnostics().format_summary().find("WASAPI Unavailable") != std::string::npos);
        }
        {
            auto stats = std::make_shared<MockEndpointStats>();
            MockEndpointBehavior behavior;
            behavior.fail_get_mix_format = true;
            ave::WasapiAudioDevice wasapi([behavior, stats]() {
                return std::make_unique<MockWasapiEndpoint>(behavior, stats);
            });
            ASSERT_TRUE(!wasapi.initialize(ave::AudioDeviceConfig{}));
            ASSERT_TRUE(!wasapi.is_available());
            ASSERT_TRUE(wasapi.state() == ave::AudioDeviceState::Error);
            ASSERT_TRUE(!wasapi.last_error().empty());
        }
        {
            auto stats = std::make_shared<MockEndpointStats>();
            ave::WasapiAudioDevice wasapi([stats]() {
                return std::make_unique<MockWasapiEndpoint>(MockEndpointBehavior{}, stats);
            });
            ave::AudioDeviceConfig bad_cfg;
            bad_cfg.sample_rate = 0;
            ASSERT_TRUE(!wasapi.initialize(bad_cfg));
            ASSERT_TRUE(wasapi.state() == ave::AudioDeviceState::Error);
            ASSERT_TRUE(!wasapi.last_error().empty());
        }
    }

    // 7. Scenario 3: Format Negotiation & AudioFormatConverter (Float32, Int16, Int24, Int32, Channels, Resampling)
    {
        // Direct converter: 48kHz Stereo Float32 -> 44.1kHz 6ch Int16 multi-block continuity
        ave::AudioFormatConverter conv;
        ASSERT_TRUE(conv.configure(48000, 2, 44100, 6, ave::AudioSampleFormat::Int16, 256, 16));
        ASSERT_TRUE(!conv.is_passthrough());
        ASSERT_TRUE(conv.bytes_per_dst_frame() == 12);

        double phase = 0.0;
        const double phase_inc = kTwoPi * 440.0 / 48000.0;
        std::vector<int16_t> pcm_out_all;
        for (int blk = 0; blk < 4; ++blk) {
            const size_t needed = conv.next_src_frames_needed(256);
            std::vector<float> src(needed * 2, 0.0f);
            for (size_t i = 0; i < needed; ++i) {
                const float s = 0.5f * static_cast<float>(std::sin(phase));
                src[i * 2 + 0] = s;
                src[i * 2 + 1] = -s;
                phase += phase_inc;
            }
            std::vector<uint8_t> dst(256 * conv.bytes_per_dst_frame(), 0);
            ASSERT_TRUE(conv.process(src.data(), needed, dst.data(), 256));
            const auto* s16 = reinterpret_cast<const int16_t*>(dst.data());
            pcm_out_all.insert(pcm_out_all.end(), s16, s16 + 256 * 6);
        }
        // Front L/R should carry signal, center/LFE remain 0, no step discontinuity > 0.25 across block boundary
        bool saw_signal = false;
        for (size_t f = 0; f < pcm_out_all.size() / 6; ++f) {
            const float fl = static_cast<float>(pcm_out_all[f * 6 + 0]) / 32767.0f;
            const float fr = static_cast<float>(pcm_out_all[f * 6 + 1]) / 32767.0f;
            const int16_t fc = pcm_out_all[f * 6 + 2];
            ASSERT_TRUE(fc == 0);
            ASSERT_NEAR(fl, -fr, 0.02f);
            if (std::abs(fl) > 0.2f) {
                saw_signal = true;
            }
            if (f > 0) {
                const float prev_fl = static_cast<float>(pcm_out_all[(f - 1) * 6 + 0]) / 32767.0f;
                ASSERT_TRUE(std::abs(fl - prev_fl) < 0.25f);
            }
        }
        ASSERT_TRUE(saw_signal);

        // Int24 & Int32 (24 valid bits in 32-bit container) & Mono downmix checks
        {
            ave::AudioFormatConverter conv24;
            ASSERT_TRUE(conv24.configure(48000, 2, 48000, 1, ave::AudioSampleFormat::Int24, 4, 24));
            std::vector<float> src = {0.5f, 0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 0.0f, 0.0f};
            std::vector<uint8_t> dst(4 * 3, 0);
            ASSERT_TRUE(conv24.process(src.data(), 4, dst.data(), 4));
            const int32_t sample0 = static_cast<int32_t>(dst[0]) |
                                    (static_cast<int32_t>(dst[1]) << 8) |
                                    (static_cast<int8_t>(dst[2]) << 16);
            ASSERT_TRUE(sample0 > 4000000);
        }
        {
            ave::AudioFormatConverter conv32;
            ASSERT_TRUE(conv32.configure(48000, 1, 48000, 2, ave::AudioSampleFormat::Int32, 2, 24));
            std::vector<float> src = {0.5f, -0.5f};
            std::vector<uint8_t> dst(2 * 8, 0);
            ASSERT_TRUE(conv32.process(src.data(), 2, dst.data(), 2));
            int32_t l0 = 0;
            int32_t r0 = 0;
            std::memcpy(&l0, dst.data(), 4);
            std::memcpy(&r0, dst.data() + 4, 4);
            ASSERT_TRUE(l0 == r0);
            // Low 8 bits padded with zeros when dst_valid_bits == 24
            ASSERT_TRUE((l0 & 0xFF) == 0);
            ASSERT_TRUE(l0 > (1 << 28));
        }

        // Mock WASAPI rejecting Float32 and offering Int16 44100 Hz closest format
        auto neg_stats = std::make_shared<MockEndpointStats>();
        MockEndpointBehavior neg_behavior;
        neg_behavior.reject_desired_format = true;
        neg_behavior.provide_closest_format = true;
        neg_behavior.closest_format = {44100, 2, ave::AudioSampleFormat::Int16, 16, false, 0};
        neg_behavior.hw_buffer_frames = 128;

        MockWasapiEndpoint* raw_ep = nullptr;
        ave::WasapiAudioDevice neg_wasapi([&]() {
            auto ep = std::make_unique<MockWasapiEndpoint>(neg_behavior, neg_stats);
            raw_ep = ep.get();
            return ep;
        });

        ave::AudioDeviceConfig cfg;
        cfg.sample_rate = 48000;
        cfg.channels = 2;
        cfg.buffer_frames = 256;
        ASSERT_TRUE(neg_wasapi.initialize(cfg));
        const auto diag = neg_wasapi.diagnostics();
        ASSERT_TRUE(diag.hardware_sample_rate == 44100);
        ASSERT_TRUE(diag.hardware_channels == 2);
        ASSERT_TRUE(diag.hardware_sample_format == ave::AudioSampleFormat::Int16);

        std::atomic<uint64_t> tone_frame{0};
        ASSERT_TRUE(neg_wasapi.start([&](float* out, size_t n, int ch, int rate) {
            const uint64_t base = tone_frame.fetch_add(n, std::memory_order_relaxed);
            for (size_t i = 0; i < n; ++i) {
                const double t = static_cast<double>(base + i) / static_cast<double>(rate);
                const float v = 0.6f * static_cast<float>(std::sin(kTwoPi * 440.0 * t));
                for (int c = 0; c < ch; ++c) {
                    out[i * static_cast<size_t>(ch) + static_cast<size_t>(c)] = v;
                }
            }
        }));
        if (raw_ep) {
            raw_ep->pulse_event();
        }
        ASSERT_TRUE(wait_until([&]() {
            return neg_stats->total_frames_released.load() >= 128;
        }));
        neg_wasapi.shutdown();
    }

    // 8. Scenarios 4, 5, 6, 7, 8, 9, 10: Mock WASAPI Streaming, Pause/Resume, Flush/Stop, Underrun, Bounds, Sanitize, Shutdown
    {
        auto stats = std::make_shared<MockEndpointStats>();
        MockEndpointBehavior behavior;
        behavior.hw_buffer_frames = 128;
        behavior.reject_event_mode = true; // Deterministic timer polling (~3ms)

        ave::WasapiAudioDevice wasapi([behavior, stats]() {
            return std::make_unique<MockWasapiEndpoint>(behavior, stats);
        });
        ave::AudioDeviceConfig cfg;
        cfg.sample_rate = 48000;
        cfg.channels = 2;
        cfg.buffer_frames = 128;
        ASSERT_TRUE(wasapi.initialize(cfg));

        // Scenario 9a: Invalid buffer handling before/during submit_pcm
        ASSERT_TRUE(wasapi.submit_pcm(nullptr, 64) == 0);
        const float dummy[4] = {0.1f, 0.2f, 0.3f, 0.4f};
        ASSERT_TRUE(wasapi.submit_pcm(dummy, 0) == 0);

        // Scenario 8: Bounded queue saturation check
        const size_t cap = wasapi.queue_capacity_frames();
        ASSERT_TRUE(cap >= 8191);
        const size_t oversize = cap + 16;
        std::vector<float> big_tone(oversize * 2, 0.0f);
        for (size_t i = 0; i < oversize; ++i) {
            const float s = 0.5f * static_cast<float>(std::sin(kTwoPi * 440.0 * static_cast<double>(i) / 48000.0));
            big_tone[i * 2 + 0] = s;
            big_tone[i * 2 + 1] = -s;
        }
        // Inject NaN/+Inf/+3.0 at index 0..2 to test Scenario 9b sanitization
        big_tone[0] = std::numeric_limits<float>::quiet_NaN();
        big_tone[1] = 3.5f;
        big_tone[2] = -std::numeric_limits<float>::infinity();
        const size_t written = wasapi.submit_pcm(big_tone.data(), oversize);
        ASSERT_TRUE(written == cap);
        ASSERT_TRUE(wasapi.submit_pcm(dummy, 2) == 0); // Saturated cleanly without blocking

        // Flush clears queued frames and calls endpoint reset
        wasapi.flush();
        ASSERT_TRUE(wasapi.queued_frames() == 0);
        ASSERT_TRUE(stats->reset_calls.load() >= 1);

        // Scenario 4: Synthetic 440 Hz stereo sine submission & exact output verification
        std::vector<float> exact_sine(256 * 2, 0.0f);
        for (size_t i = 0; i < 256; ++i) {
            const float s = 0.4f * static_cast<float>(std::sin(kTwoPi * 440.0 * static_cast<double>(i) / 48000.0));
            exact_sine[i * 2 + 0] = s;
            exact_sine[i * 2 + 1] = -s;
        }
        // Also prepend 1 invalid/clamped frame followed by exact_sine? Better keep first 128 frames clean so we check exact values!
        ASSERT_TRUE(wasapi.submit_pcm(exact_sine.data(), 256) == 256);

        ASSERT_TRUE(wasapi.start(nullptr));
        ASSERT_TRUE(wasapi.is_running());
        ASSERT_TRUE(stats->start_calls.load() == 1);

        // Wait until at least 256 pushed frames + 1 underrun block have rendered (Scenario 7 underrun)
        ASSERT_TRUE(wait_until([&]() {
            return stats->total_frames_released.load() >= 384 && wasapi.underrun_count() >= 1;
        }));

        {
            std::lock_guard<std::mutex> lock(stats->mutex);
            ASSERT_TRUE(stats->captured_bytes.size() >= 256 * 2 * sizeof(float));
            const auto* out_floats = reinterpret_cast<const float*>(stats->captured_bytes.data());
            for (size_t i = 0; i < 256 * 2; ++i) {
                ASSERT_NEAR(out_floats[i], exact_sine[i], 1e-4f);
            }
        }

        // Scenario 5: Pause & Resume
        ASSERT_TRUE(wasapi.pause());
        ASSERT_TRUE(wasapi.is_paused());
        ASSERT_TRUE(!wasapi.is_running());
        ASSERT_TRUE(stats->stop_calls.load() >= 1);
        const size_t frames_at_pause = stats->total_frames_released.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        ASSERT_TRUE(stats->total_frames_released.load() == frames_at_pause);

        ASSERT_TRUE(wasapi.resume());
        ASSERT_TRUE(wasapi.is_running());
        ASSERT_TRUE(stats->start_calls.load() >= 2);
        ASSERT_TRUE(wait_until([&]() {
            return stats->total_frames_released.load() > frames_at_pause;
        }));

        // Scenario 6 & 10: Submit pending buffers then Stop + Shutdown immediately (zero hang/deadlock)
        ASSERT_TRUE(wasapi.submit_pcm(exact_sine.data(), 256) > 0);
        wasapi.stop();
        ASSERT_TRUE(!wasapi.is_running());
        ASSERT_TRUE(wasapi.state() == ave::AudioDeviceState::Stopped);
        ASSERT_TRUE(wasapi.queued_frames() == 0);

        ASSERT_TRUE(wasapi.submit_pcm(exact_sine.data(), 128) == 128);
        wasapi.shutdown();
        ASSERT_TRUE(!wasapi.is_available());
    }

    // 9. Scenario 9b & 11: Sanitization via Float32 pass + Null GetBuffer + AUDCLNT_E_DEVICE_INVALIDATED
    {
        // Sanitization through WASAPI output
        auto san_stats = std::make_shared<MockEndpointStats>();
        MockEndpointBehavior san_behavior;
        san_behavior.hw_buffer_frames = 64;
        san_behavior.reject_event_mode = true;
        ave::WasapiAudioDevice san_wasapi([san_behavior, san_stats]() {
            return std::make_unique<MockWasapiEndpoint>(san_behavior, san_stats);
        });
        ave::AudioDeviceConfig san_cfg{48000, 2, 64, ave::AudioSampleFormat::Float32};
        ASSERT_TRUE(san_wasapi.initialize(san_cfg));
        std::vector<float> toxic(64 * 2, 0.0f);
        toxic[0] = std::numeric_limits<float>::quiet_NaN();
        toxic[1] = 2.5f;
        toxic[2] = std::numeric_limits<float>::infinity();
        toxic[3] = -2.5f;
        ASSERT_TRUE(san_wasapi.submit_pcm(toxic.data(), 64) == 64);
        ASSERT_TRUE(san_wasapi.start(nullptr));
        ASSERT_TRUE(wait_until([&]() { return san_stats->total_frames_released.load() >= 64; }));
        {
            std::lock_guard<std::mutex> lock(san_stats->mutex);
            const auto* out_f = reinterpret_cast<const float*>(san_stats->captured_bytes.data());
            ASSERT_NEAR(out_f[0], 0.0f, 1e-6f);
            ASSERT_NEAR(out_f[1], 1.0f, 1e-6f);
            ASSERT_NEAR(out_f[2], 0.0f, 1e-6f);
            ASSERT_NEAR(out_f[3], -1.0f, 1e-6f);
        }
        san_wasapi.shutdown();

        // Mid-stream AUDCLNT_E_DEVICE_INVALIDATED
        auto inv_stats = std::make_shared<MockEndpointStats>();
        MockEndpointBehavior inv_behavior;
        inv_behavior.hw_buffer_frames = 64;
        inv_behavior.reject_event_mode = true;
        ave::WasapiAudioDevice inv_wasapi([inv_behavior, inv_stats]() {
            return std::make_unique<MockWasapiEndpoint>(inv_behavior, inv_stats);
        });
        ASSERT_TRUE(inv_wasapi.initialize(san_cfg));
        ASSERT_TRUE(inv_wasapi.start(nullptr));
        ASSERT_TRUE(wait_until([&]() { return inv_stats->total_frames_released.load() >= 64; }));
        inv_stats->force_invalidate_next.store(true, std::memory_order_release);
        ASSERT_TRUE(wait_until([&]() {
            return inv_wasapi.state() == ave::AudioDeviceState::Error;
        }));
        ASSERT_TRUE(!inv_wasapi.is_available());
        ASSERT_TRUE(inv_wasapi.last_error().find("AUDCLNT_E_DEVICE_INVALIDATED") != std::string::npos);
        inv_wasapi.shutdown();
    }

    // 10. Scenario 12: Multi-thread Concurrent Producer / Lifecycle Stress
    {
        auto stress_stats = std::make_shared<MockEndpointStats>();
        MockEndpointBehavior stress_behavior;
        stress_behavior.hw_buffer_frames = 128;
        stress_behavior.reject_event_mode = true;

        ave::WasapiAudioDevice stress_wasapi([stress_behavior, stress_stats]() {
            return std::make_unique<MockWasapiEndpoint>(stress_behavior, stress_stats);
        });
        ave::AudioDeviceConfig cfg{48000, 2, 128, ave::AudioSampleFormat::Float32};
        ASSERT_TRUE(stress_wasapi.initialize(cfg));
        ASSERT_TRUE(stress_wasapi.start(nullptr));

        std::atomic<bool> stop_producer{false};
        std::thread producer([&]() {
            std::vector<float> block(256 * 2, 0.25f);
            while (!stop_producer.load(std::memory_order_acquire)) {
                (void)stress_wasapi.submit_pcm(block.data(), 256);
                std::this_thread::yield();
            }
        });

        for (int iter = 0; iter < 5; ++iter) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            (void)stress_wasapi.pause();
            stress_wasapi.flush();
            (void)stress_wasapi.resume();
        }

        stop_producer.store(true, std::memory_order_release);
        producer.join();
        stress_wasapi.shutdown();
        ASSERT_TRUE(stress_stats->total_frames_released.load() > 0);
    }

    // 11. Platform Factory Probe Sanity Check (Works on both Headless CI and Desktop Audio Hardware)
    {
        std::unique_ptr<ave::AudioDevice> platform_dev = ave::create_platform_audio_device();
        ASSERT_TRUE(platform_dev != nullptr);
        ASSERT_TRUE(platform_dev->backend_name() == "WASAPI");
        const ave::AudioDeviceConfig def_cfg{};
        const bool ok = platform_dev->initialize(def_cfg);
        const ave::AudioDeviceDiagnostics diag = platform_dev->diagnostics();
        std::cout << "[AudioEngineTest] Platform WASAPI Probe: " << diag.format_summary() << std::endl;
        if (ok) {
            ASSERT_TRUE(diag.available);
            ASSERT_TRUE(diag.is_physical);
            ASSERT_TRUE(diag.state == ave::AudioDeviceState::Stopped);
        } else {
            ASSERT_TRUE(!diag.available);
            ASSERT_TRUE(diag.state == ave::AudioDeviceState::Error);
            ASSERT_TRUE(!diag.last_error.empty());
        }
        platform_dev->shutdown();
    }

    std::cout << "[AudioEngineTest] All audio engine & WASAPI assertions passed!" << std::endl;
    return 0;
}
