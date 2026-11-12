#pragma once

#include "ave/audio_device.hpp"
#include "ave/audio_mixer.hpp"
#include "ave/audio_ring_buffer.hpp"
#include "ave/frame_presentation_hub.hpp"
#include "ave/playback_clock.hpp"
#include "ave/playback_controller.hpp"
#include "ave/playback_snapshot.hpp"
#include "ave/render_graph.hpp"
#include "ave/stream_audio_decoder.hpp"
#include "ave/timeline_prefetcher.hpp"
#include "ave/video_frame_packet.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ave {

class VulkanDevice;
class VulkanCompositor;
class VulkanReadbackRing;

enum class AudioBackendMode {
    AutoPlatformDefault = 0, // Physical WASAPI output on Windows; explicitly reports unavailability on failure
    NullForTesting           // Deterministic simulated NullAudioDevice for unit tests & headless CI
};

struct PlaybackSessionConfig {
    Rational prefetch_window{1, 1};             // 1 second lookahead
    size_t video_queue_capacity{12};            // 12 video frames
    uint32_t audio_sample_rate{48000};          // 48 kHz
    int audio_channels{2};                      // Stereo
    size_t audio_buffer_frames{48000};          // 1 second ring buffer
    AudioBackendMode audio_backend_mode{AudioBackendMode::AutoPlatformDefault};
    bool enable_compositing{true};              // Multi-track compositing enabled by default
    int max_preview_width{kMaxPreviewWidth};    // <= 1280
    int max_preview_height{kMaxPreviewHeight};  // <= 720
};

struct PresentationResult {
    std::optional<VideoFrame> frame;
    FrameSyncAction action{FrameSyncAction::Hold};
    Rational presentation_time{0, 1};
    uint64_t snapshot_version{0};
    uint64_t published_sequence_id{0};
    bool end_of_timeline{false};
    bool is_composited{false};
    bool used_gpu_readback{false};
    bool gpu_fallback_triggered{false};
    size_t active_tracks_count{0};
};

struct SessionMetrics {
    PlaybackMetrics video_metrics;
    uint64_t audio_underruns{0};
    Rational current_pts{0, 1};
    PlaybackState state{PlaybackState::Stopped};
    double speed{1.0};
};

/**
 * @brief Coordinates the immutable playback snapshot store, playback clock,
 * video prefetch queue, Vulkan compositor + readback ring, frame presentation hub,
 * audio mixer, and audio output device into a thread-safe real-time playback session.
 */
class PlaybackSession {
public:
    explicit PlaybackSession(std::shared_ptr<PlaybackSnapshotStore> snapshot_store,
                             PlaybackSessionConfig config = {},
                             std::shared_ptr<FramePresentationHub> frame_hub = nullptr);
    ~PlaybackSession();

    PlaybackSession(const PlaybackSession&) = delete;
    PlaybackSession& operator=(const PlaybackSession&) = delete;

    // Transport controls
    void play();
    void pause();
    void toggle_play();
    void stop();
    void seek(Rational target_pts);
    void step(int64_t frame_count);
    void set_speed(double speed);
    void set_loop(bool loop) noexcept { loop_ = loop; }

    /**
     * @brief Prepares the session for structural Project replacement (newProject/openProject):
     * stops prefetch workers, clears queued video frames, closes audio decoders, and resets clock.
     */
    void prepare_for_project_replace();

    /**
     * @brief Convenience structural reset + atomic snapshot publication for project replacement.
     */
    void reset_for_project_load(const Timeline& timeline, const MediaPool& pool);

    /**
     * @brief Terminates playback workers, drains Vulkan readback fences, closes audio callbacks,
     * and detaches snapshot listeners cleanly.
     */
    void shutdown();

    [[nodiscard]] PlaybackState state() const noexcept;
    [[nodiscard]] Rational current_time() const noexcept;
    [[nodiscard]] double speed() const noexcept;
    [[nodiscard]] bool is_looping() const noexcept { return loop_; }
    [[nodiscard]] uint64_t current_seek_generation() const noexcept;
    [[nodiscard]] const std::shared_ptr<PlaybackSnapshotStore>& snapshot_store() const noexcept { return snapshot_store_; }

    /**
     * @brief Polls presentation state: evaluates video frames, renders GPU/CPU preview frame,
     * publishes to FramePresentationHub, and advances playback strictly against PlaybackSnapshot.
     * @param pipelined_overlap When true during steady PlaybackState::Playing, overlaps current
     * Vulkan compute/copy submission with fence harvesting from the opposite ring slot.
     */
    PresentationResult update(bool pipelined_overlap = false);

    /**
     * @brief Flushes any pending GPU readback slot to FramePresentationHub cleanly (no vkDeviceWaitIdle).
     */
    void flush_gpu_readback();

    /**
     * @brief Pumps audio samples into the ring buffer from active timeline audio tracks
     * strictly against an immutable PlaybackSnapshot.
     */
    void pump_audio(size_t sample_frames_needed = 2048);

    // Direct subsystem access for inspection and diagnostics
    [[nodiscard]] PlaybackController& controller() noexcept { return controller_; }
    [[nodiscard]] const PlaybackController& controller() const noexcept { return controller_; }
    [[nodiscard]] PlaybackClock& clock() noexcept { return controller_.clock(); }
    [[nodiscard]] const PlaybackClock& clock() const noexcept { return controller_.clock(); }
    [[nodiscard]] const AudioMixer& audio_mixer() const noexcept { return audio_mixer_; }
    [[nodiscard]] const AudioRingBuffer& audio_ring_buffer() const noexcept { return audio_ring_buffer_; }
    [[nodiscard]] AudioDevice* audio_device() noexcept { return audio_device_.get(); }
    void set_audio_device(std::unique_ptr<AudioDevice> device);

    // Frame presentation hub & live audio metering
    void set_frame_hub(std::shared_ptr<FramePresentationHub> frame_hub);
    [[nodiscard]] std::shared_ptr<FramePresentationHub> frame_hub() const noexcept;
    [[nodiscard]] double left_level_db() const noexcept;
    [[nodiscard]] double right_level_db() const noexcept;
    [[nodiscard]] AudioDeviceDiagnostics audio_diagnostics() const;
    [[nodiscard]] std::string audio_output_status() const;

    // Compositing & Vulkan readback engine control
    void enable_compositing(bool enable) noexcept { compositing_enabled_.store(enable, std::memory_order_release); }
    [[nodiscard]] bool is_compositing_enabled() const noexcept { return compositing_enabled_.load(std::memory_order_acquire); }
    void set_vulkan_device(std::shared_ptr<VulkanDevice> device);
    [[nodiscard]] bool has_vulkan_device() const noexcept;
    [[nodiscard]] std::string gpu_device_name() const;
    [[nodiscard]] std::shared_ptr<VulkanDevice> vulkan_device() const noexcept;
    [[nodiscard]] std::shared_ptr<VulkanCompositor> vulkan_compositor() const noexcept;
    [[nodiscard]] VulkanReadbackRing* vulkan_readback_ring() noexcept;
    [[nodiscard]] bool is_gpu_fallback_active() const noexcept;

    [[nodiscard]] SessionMetrics metrics() const;

private:
    struct GpuPreviewPipeline;

    std::shared_ptr<PlaybackSnapshotStore> snapshot_store_;
    PlaybackSessionConfig config_;

    PlaybackController controller_;
    TimelinePrefetcher prefetcher_;

    AudioMixer audio_mixer_;
    AudioRingBuffer audio_ring_buffer_;
    mutable std::mutex audio_device_mutex_{};
    std::unique_ptr<AudioDevice> audio_device_;
    std::atomic<bool> audio_playing_flag_{false};
    std::atomic<double> audio_left_db_{-60.0};
    std::atomic<double> audio_right_db_{-60.0};

    mutable std::mutex frame_hub_mutex_{};
    std::shared_ptr<FramePresentationHub> frame_hub_;

    // Declared strictly before GPU dependents so reverse C++ member destruction
    // tears down VulkanReadbackRing / VulkanCompositor before VulkanDevice.
    mutable std::mutex render_mutex_{};
    std::shared_ptr<VulkanDevice> vulkan_device_{nullptr};
    std::shared_ptr<VulkanCompositor> vulkan_compositor_{nullptr};
    std::unique_ptr<GpuPreviewPipeline> gpu_pipeline_;
    std::atomic<bool> has_vulkan_device_{false};
    mutable std::mutex device_info_mutex_{};
    std::string cached_gpu_device_name_{};
    std::atomic<bool> compositing_enabled_{true};
    std::atomic<bool> gpu_fallback_active_{false};

    bool loop_{false};
    std::atomic<bool> shut_down_{false};
    PlaybackSnapshotStore::ListenerId snapshot_listener_id_{0};

    // Cached audio decoders per media asset ID (guarded by audio_mutex_)
    struct CachedAudioDecoder {
        std::unique_ptr<StreamAudioDecoder> decoder;
        std::filesystem::path file_path;
        uint64_t file_size_bytes{0};
    };
    mutable std::mutex audio_mutex_{};
    std::unordered_map<std::string, CachedAudioDecoder> audio_decoders_;
    Rational audio_cursor_pts_{0, 1};
    uint64_t last_audio_snapshot_version_{0};

    void init_audio_device();
    AudioRenderCallback make_audio_render_callback(AudioDevice* dev) noexcept;
    void start_or_resume_audio_device_locked();
    void update_audio_levels_lockfree(const float* samples, size_t frames) noexcept;
    void reset_audio_levels() noexcept;
    void sync_audio_decoders_on_seek_locked(Rational pts);
    void on_snapshot_updated(const PlaybackSnapshotPtr& snap);
    uint64_t publish_packet_to_hub(VideoFramePacket packet);
};

} // namespace ave
