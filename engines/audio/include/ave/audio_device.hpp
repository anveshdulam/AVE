#pragma once

#include <functional>
#include <memory>
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <thread>

namespace ave {

using AudioRenderCallback = std::function<void(float* output_buffer, size_t frame_count, int channels, int sample_rate)>;

/**
 * @brief Replaceable hardware audio output device interface.
 */
class AudioDevice {
public:
    virtual ~AudioDevice() = default;

    virtual bool start(AudioRenderCallback callback) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool is_running() const noexcept = 0;
    [[nodiscard]] virtual int sample_rate() const noexcept = 0;
    [[nodiscard]] virtual int channels() const noexcept = 0;
    [[nodiscard]] virtual size_t buffer_frames() const noexcept = 0;
    [[nodiscard]] virtual uint64_t underrun_count() const noexcept = 0;
};

/**
 * @brief High-precision software audio device used for testing, CI, and offline rendering.
 */
class NullAudioDevice : public AudioDevice {
public:
    explicit NullAudioDevice(int sample_rate = 48000, int channels = 2, size_t buffer_frames = 512);
    ~NullAudioDevice() override;

    bool start(AudioRenderCallback callback) override;
    void stop() override;
    [[nodiscard]] bool is_running() const noexcept override;
    [[nodiscard]] int sample_rate() const noexcept override { return sample_rate_; }
    [[nodiscard]] int channels() const noexcept override { return channels_; }
    [[nodiscard]] size_t buffer_frames() const noexcept override { return buffer_frames_; }
    [[nodiscard]] uint64_t underrun_count() const noexcept override { return underruns_.load(); }

    /**
     * @brief Manually renders a specific number of frames synchronously (ideal for unit testing).
     */
    void render_synchronous(size_t frame_count, float* out_buffer = nullptr);

private:
    int sample_rate_{48000};
    int channels_{2};
    size_t buffer_frames_{512};
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> underruns_{0};
    AudioRenderCallback callback_{nullptr};

    std::thread thread_{};
    std::vector<float> internal_buffer_{};

    void run_loop();
};

} // namespace ave

