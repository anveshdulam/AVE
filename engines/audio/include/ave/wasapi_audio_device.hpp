#pragma once

#include "ave/audio_device.hpp"
#include "ave/audio_ring_buffer.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ave {

struct WasapiEndpointFormat {
    int sample_rate{48000};
    int channels{2};
    AudioSampleFormat sample_format{AudioSampleFormat::Float32};
    int valid_bits_per_sample{32};
    bool is_extensible{false};
    uint32_t channel_mask{0};
};

struct WasapiStatus {
    bool ok{true};
    bool device_invalidated{false};
    bool unsupported_format{false};
    long hresult{0};
    std::string error_message{};
};

/**
 * @brief Internal endpoint abstraction wrapping Win32 MMDevice + IAudioClient + IAudioRenderClient
 * so WasapiAudioDevice lifecycle, format negotiation, error handling, pause/resume, flush, and
 * COM thread isolation can be deterministically verified both with and without audio hardware.
 */
class IWasapiEndpoint {
public:
    virtual ~IWasapiEndpoint() = default;

    virtual WasapiStatus open_default_render_device(std::string& out_device_name) = 0;
    virtual WasapiStatus get_mix_format(WasapiEndpointFormat& out_format) = 0;
    virtual bool is_format_supported_shared(const WasapiEndpointFormat& desired,
                                            WasapiEndpointFormat& out_closest,
                                            bool& out_has_closest) = 0;
    virtual WasapiStatus initialize_client(const WasapiEndpointFormat& format,
                                           size_t requested_buffer_frames,
                                           bool prefer_event_callback,
                                           size_t& out_buffer_frames,
                                           bool& out_event_mode_active) = 0;
    [[nodiscard]] virtual void* native_wait_handle() const noexcept = 0;
    virtual WasapiStatus get_current_padding(size_t& out_padding_frames) = 0;
    virtual WasapiStatus start() = 0;
    virtual WasapiStatus stop() = 0;
    virtual WasapiStatus reset() = 0;
    virtual WasapiStatus get_buffer(size_t frames_to_write, uint8_t** out_data) = 0;
    virtual WasapiStatus release_buffer(size_t frames_written, bool silent) = 0;
    virtual void close() = 0;
};

/**
 * @brief Windows native WASAPI shared-mode audio output device.
 * All COM initialization (COINIT_MULTITHREADED) and endpoint I/O execute strictly on
 * this object's dedicated worker thread, never on the caller or Qt GUI thread.
 */
class WasapiAudioDevice final : public AudioDevice {
public:
    using EndpointFactory = std::function<std::unique_ptr<IWasapiEndpoint>()>;

    WasapiAudioDevice();
    explicit WasapiAudioDevice(EndpointFactory endpoint_factory);
    ~WasapiAudioDevice() override;

    WasapiAudioDevice(const WasapiAudioDevice&) = delete;
    WasapiAudioDevice& operator=(const WasapiAudioDevice&) = delete;

    bool initialize(const AudioDeviceConfig& config = {}) override;
    bool start(AudioRenderCallback callback) override;
    bool pause() override;
    bool resume() override;
    void flush() override;
    void stop() override;
    void shutdown() override;

    size_t submit_pcm(const float* interleaved_frames, size_t frame_count) override;

    [[nodiscard]] bool is_available() const noexcept override;
    [[nodiscard]] bool is_running() const noexcept override;
    [[nodiscard]] bool is_paused() const noexcept override;
    [[nodiscard]] AudioDeviceState state() const noexcept override;
    [[nodiscard]] int sample_rate() const noexcept override;
    [[nodiscard]] int channels() const noexcept override;
    [[nodiscard]] size_t buffer_frames() const noexcept override;
    [[nodiscard]] AudioSampleFormat sample_format() const noexcept override;
    [[nodiscard]] uint64_t underrun_count() const noexcept override {
        return underruns_.load(std::memory_order_relaxed);
    }
    void report_underrun() noexcept override {
        underruns_.fetch_add(1, std::memory_order_relaxed);
    }
    [[nodiscard]] std::string backend_name() const override { return "WASAPI"; }
    [[nodiscard]] std::string device_name() const override;
    [[nodiscard]] std::string last_error() const override;
    [[nodiscard]] AudioDeviceDiagnostics diagnostics() const override;

    [[nodiscard]] std::thread::id worker_thread_id() const noexcept;
    [[nodiscard]] bool is_event_driven() const noexcept;
    [[nodiscard]] size_t queued_frames() const noexcept;
    [[nodiscard]] size_t queue_capacity_frames() const noexcept;

private:
    enum class WorkerCmdType {
        Initialize,
        Start,
        Pause,
        Resume,
        Flush,
        Stop,
        Shutdown
    };

    struct PendingCmd {
        WorkerCmdType type{WorkerCmdType::Stop};
        AudioDeviceConfig config{};
        AudioRenderCallback callback{};
        bool done{false};
        bool result{false};
    };

    EndpointFactory endpoint_factory_;
    std::unique_ptr<IWasapiEndpoint> endpoint_{};

    std::atomic<bool> available_{false};
    std::atomic<AudioDeviceState> state_{AudioDeviceState::Uninitialized};
    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> frames_submitted_{0};
    std::atomic<bool> event_driven_{false};
    std::atomic<bool> shutting_down_{false};
    std::atomic<bool> push_had_data_{false};

    mutable std::mutex diag_mutex_{};
    AudioDeviceConfig config_{};
    AudioDeviceConfig negotiated_hw_{};
    int hardware_valid_bits_{32};
    std::string device_name_{};
    std::string last_error_{};
    std::thread::id worker_thread_id_{};

    AudioFormatConverter converter_{};
    AudioRenderCallback callback_{};
    std::unique_ptr<AudioRingBuffer> queued_pcm_{};
    std::vector<float> sanitize_scratch_{};
    std::vector<float> engine_scratch_{};
    size_t hw_buffer_frames_{0};
    bool has_fed_initial_block_{false};

    std::mutex cmd_mutex_{};
    std::condition_variable cmd_ack_cv_{};
    std::deque<std::shared_ptr<PendingCmd>> cmd_queue_{};
    void* control_event_{nullptr};
    std::thread worker_thread_{};

    void ensure_worker_started();
    bool send_command_and_wait(WorkerCmdType type,
                               const AudioDeviceConfig* cfg = nullptr,
                               AudioRenderCallback cb = nullptr);

    void worker_loop();
    void handle_command(PendingCmd& cmd);
    bool handle_initialize_on_worker(const AudioDeviceConfig& config);
    bool handle_start_on_worker(AudioRenderCallback callback);
    bool handle_pause_on_worker();
    bool handle_resume_on_worker();
    void handle_flush_on_worker();
    void handle_stop_on_worker();
    void cleanup_endpoint_on_worker();

    void render_available_frames();
    void pull_engine_frames(float* dst, size_t frame_count);
    void fail_device_on_worker(const std::string& reason);
    void signal_control_event() noexcept;
};

} // namespace ave

