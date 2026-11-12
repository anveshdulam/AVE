#pragma once

#include "ave/audio_ring_buffer.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ave {

using AudioRenderCallback =
    std::function<void(float* output_buffer, size_t frame_count, int channels, int sample_rate)>;

enum class AudioSampleFormat {
    Unknown = 0,
    Float32,
    Int16,
    Int24,
    Int32
};

enum class AudioDeviceState {
    Uninitialized = 0,
    Stopped,
    Running,
    Paused,
    Error
};

[[nodiscard]] const char* to_string(AudioSampleFormat fmt) noexcept;
[[nodiscard]] const char* to_string(AudioDeviceState state) noexcept;

struct AudioDeviceConfig {
    int sample_rate{48000};
    int channels{2};
    size_t buffer_frames{512};
    AudioSampleFormat sample_format{AudioSampleFormat::Float32};

    [[nodiscard]] bool is_valid() const noexcept {
        return sample_rate >= 8000 && sample_rate <= 384000 &&
               channels >= 1 && channels <= 16 &&
               buffer_frames >= 32 && buffer_frames <= 65536 &&
               sample_format != AudioSampleFormat::Unknown;
    }
};

struct AudioDeviceDiagnostics {
    bool available{false};
    bool is_physical{false};
    std::string backend_name{};
    std::string device_name{};
    int sample_rate{0};
    int channels{0};
    AudioSampleFormat sample_format{AudioSampleFormat::Float32};
    size_t buffer_frames{0};
    int hardware_sample_rate{0};
    int hardware_channels{0};
    AudioSampleFormat hardware_sample_format{AudioSampleFormat::Unknown};
    AudioDeviceState state{AudioDeviceState::Uninitialized};
    uint64_t underrun_count{0};
    uint64_t frames_submitted{0};
    std::string last_error{};

    [[nodiscard]] std::string format_summary() const;
};

/**
 * @brief Pre-allocated, zero-allocation-per-block converter from interleaved Float32
 * engine PCM to hardware endpoint channel count, sample rate, and sample format.
 */
class AudioFormatConverter {
public:
    bool configure(int src_rate,
                   int src_channels,
                   int dst_rate,
                   int dst_channels,
                   AudioSampleFormat dst_format,
                   size_t max_dst_frames,
                   int dst_valid_bits = 0);

    void reset_state() noexcept;

    [[nodiscard]] bool is_passthrough() const noexcept { return passthrough_; }
    [[nodiscard]] size_t max_src_frames() const noexcept { return max_src_frames_; }
    [[nodiscard]] int src_rate() const noexcept { return src_rate_; }
    [[nodiscard]] int src_channels() const noexcept { return src_channels_; }
    [[nodiscard]] int dst_rate() const noexcept { return dst_rate_; }
    [[nodiscard]] int dst_channels() const noexcept { return dst_channels_; }
    [[nodiscard]] AudioSampleFormat dst_format() const noexcept { return dst_format_; }
    [[nodiscard]] size_t bytes_per_dst_frame() const noexcept;

    [[nodiscard]] size_t next_src_frames_needed(size_t dst_frames) const noexcept;

    // Converts `new_src_frames` (from next_src_frames_needed(dst_frames)) into `dst_frames` output frames.
    bool process(const float* src_interleaved,
                 size_t new_src_frames,
                 uint8_t* dst_bytes,
                 size_t dst_frames) noexcept;

    static void sanitize_floats(const float* src, float* dst, size_t sample_count) noexcept;
    static void map_channels(const float* src,
                             int src_channels,
                             float* dst,
                             int dst_channels,
                             size_t frames) noexcept;
    static void encode_samples(const float* src_interleaved,
                               size_t sample_count,
                               AudioSampleFormat dst_format,
                               int dst_valid_bits,
                               uint8_t* dst_bytes) noexcept;

private:
    int src_rate_{48000};
    int src_channels_{2};
    int dst_rate_{48000};
    int dst_channels_{2};
    AudioSampleFormat dst_format_{AudioSampleFormat::Float32};
    int dst_valid_bits_{32};
    size_t max_dst_frames_{512};
    size_t max_src_frames_{512};
    bool passthrough_{true};

    double step_{1.0};
    double phase_{0.0};
    size_t hist_valid_{0};

    std::vector<float> mapped_scratch_{};
    std::vector<float> resampled_scratch_{};
    std::vector<float> history_buf_{};
};

/**
 * @brief Replaceable hardware audio output device interface.
 */
class AudioDevice {
public:
    virtual ~AudioDevice() = default;

    virtual bool initialize(const AudioDeviceConfig& config = {}) = 0;
    virtual bool start(AudioRenderCallback callback) = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;
    virtual void flush() = 0;
    virtual void stop() = 0;
    virtual void shutdown() = 0;

    virtual size_t submit_pcm(const float* interleaved_frames, size_t frame_count) = 0;

    [[nodiscard]] virtual bool is_available() const noexcept = 0;
    [[nodiscard]] virtual bool is_running() const noexcept = 0;
    [[nodiscard]] virtual bool is_paused() const noexcept = 0;
    [[nodiscard]] virtual AudioDeviceState state() const noexcept = 0;
    [[nodiscard]] virtual int sample_rate() const noexcept = 0;
    [[nodiscard]] virtual int channels() const noexcept = 0;
    [[nodiscard]] virtual size_t buffer_frames() const noexcept = 0;
    [[nodiscard]] virtual AudioSampleFormat sample_format() const noexcept = 0;
    [[nodiscard]] virtual uint64_t underrun_count() const noexcept = 0;
    virtual void report_underrun() noexcept = 0;
    [[nodiscard]] virtual std::string backend_name() const = 0;
    [[nodiscard]] virtual std::string device_name() const = 0;
    [[nodiscard]] virtual std::string last_error() const = 0;
    [[nodiscard]] virtual AudioDeviceDiagnostics diagnostics() const = 0;
};

/**
 * @brief Deterministic software audio device used for unit testing, CI, and offscreen verification.
 * Does NOT emit sound to physical speakers.
 */
class NullAudioDevice : public AudioDevice {
public:
    explicit NullAudioDevice(int sample_rate = 48000, int channels = 2, size_t buffer_frames = 512);
    ~NullAudioDevice() override;

    bool initialize(const AudioDeviceConfig& config = {}) override;
    bool start(AudioRenderCallback callback) override;
    bool pause() override;
    bool resume() override;
    void flush() override;
    void stop() override;
    void shutdown() override;

    size_t submit_pcm(const float* interleaved_frames, size_t frame_count) override;

    [[nodiscard]] bool is_available() const noexcept override { return false; }
    [[nodiscard]] bool is_running() const noexcept override;
    [[nodiscard]] bool is_paused() const noexcept override;
    [[nodiscard]] AudioDeviceState state() const noexcept override;
    [[nodiscard]] int sample_rate() const noexcept override { return config_.sample_rate; }
    [[nodiscard]] int channels() const noexcept override { return config_.channels; }
    [[nodiscard]] size_t buffer_frames() const noexcept override { return config_.buffer_frames; }
    [[nodiscard]] AudioSampleFormat sample_format() const noexcept override { return config_.sample_format; }
    [[nodiscard]] uint64_t underrun_count() const noexcept override {
        return underruns_.load(std::memory_order_relaxed);
    }
    void report_underrun() noexcept override {
        underruns_.fetch_add(1, std::memory_order_relaxed);
    }
    [[nodiscard]] std::string backend_name() const override { return "NullAudioDevice"; }
    [[nodiscard]] std::string device_name() const override {
        return "Simulated Null Output (Test / Headless)";
    }
    [[nodiscard]] std::string last_error() const override;
    [[nodiscard]] AudioDeviceDiagnostics diagnostics() const override;

    [[nodiscard]] size_t queued_frames() const noexcept;
    [[nodiscard]] size_t queue_capacity_frames() const noexcept;

    /**
     * @brief Manually renders a specific number of frames synchronously (ideal for unit testing).
     */
    void render_synchronous(size_t frame_count, float* out_buffer = nullptr);

private:
    AudioDeviceConfig config_{};
    std::atomic<AudioDeviceState> state_{AudioDeviceState::Stopped};
    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> frames_submitted_{0};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> flush_requested_{false};
    std::atomic<bool> initialized_{true};

    mutable std::mutex state_mutex_{};
    std::condition_variable control_cv_{};
    std::condition_variable flush_cv_{};
    std::string last_error_{};
    AudioRenderCallback callback_{nullptr};

    std::unique_ptr<AudioRingBuffer> queued_pcm_{};
    std::vector<float> sanitize_scratch_{};
    std::vector<float> internal_buffer_{};
    std::thread thread_{};

    void pull_block(float* dst, size_t frame_count);
    void run_loop();
};

/**
 * @brief Creates the platform-native physical audio output backend (WASAPI on Windows).
 * Returns nullptr on unsupported platforms.
 */
[[nodiscard]] std::unique_ptr<AudioDevice> create_platform_audio_device();

} // namespace ave
