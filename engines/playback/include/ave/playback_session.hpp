#pragma once

#include "ave/timeline.hpp"
#include "ave/media_pool.hpp"
#include "ave/playback_clock.hpp"
#include "ave/playback_controller.hpp"
#include "ave/timeline_prefetcher.hpp"
#include "ave/audio_mixer.hpp"
#include "ave/audio_ring_buffer.hpp"
#include "ave/audio_device.hpp"
#include "ave/stream_audio_decoder.hpp"
#include "ave/render_graph.hpp"

#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>

namespace ave {

class VulkanDevice;
class VulkanCompositor;

struct PlaybackSessionConfig {
    Rational prefetch_window{1, 1};       // 1 second lookahead
    size_t video_queue_capacity{12};      // 12 video frames
    uint32_t audio_sample_rate{48000};    // 48 kHz
    int audio_channels{2};                // Stereo
    size_t audio_buffer_frames{48000};    // 1 second ring buffer
    bool enable_compositing{true};        // Multi-track compositing enabled by default
};

struct PresentationResult {
    std::optional<VideoFrame> frame;
    FrameSyncAction action{FrameSyncAction::Hold};
    Rational presentation_time{0, 1};
    bool end_of_timeline{false};
    bool is_composited{false};
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
 * @brief Coordinates the timeline, playback clock, video prefetch queue,
 * audio mixer, and audio output device into a unified real-time playback session.
 */
class PlaybackSession {
public:
    PlaybackSession(const Timeline& timeline,
                    const MediaPool& media_pool,
                    PlaybackSessionConfig config = {});
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

    [[nodiscard]] PlaybackState state() const noexcept;
    [[nodiscard]] Rational current_time() const noexcept;
    [[nodiscard]] double speed() const noexcept;
    [[nodiscard]] bool is_looping() const noexcept { return loop_; }
    [[nodiscard]] uint64_t current_seek_generation() const noexcept;

    /**
     * @brief Polls presentation state: evaluates video frames and advances playback.
     */
    PresentationResult update();

    /**
     * @brief Pumps audio samples into the ring buffer from active timeline audio tracks.
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

    // Compositing engine control
    void enable_compositing(bool enable) noexcept { config_.enable_compositing = enable; }
    [[nodiscard]] bool is_compositing_enabled() const noexcept { return config_.enable_compositing; }
    void set_vulkan_device(std::shared_ptr<VulkanDevice> device);
    [[nodiscard]] std::shared_ptr<VulkanDevice> vulkan_device() const noexcept { return vulkan_device_; }
    [[nodiscard]] std::shared_ptr<VulkanCompositor> vulkan_compositor() const noexcept { return vulkan_compositor_; }

    [[nodiscard]] SessionMetrics metrics() const;

private:
    const Timeline& timeline_;
    const MediaPool& media_pool_;
    PlaybackSessionConfig config_;

    PlaybackController controller_;
    TimelinePrefetcher prefetcher_;

    AudioMixer audio_mixer_;
    AudioRingBuffer audio_ring_buffer_;
    std::unique_ptr<AudioDevice> audio_device_;

    std::shared_ptr<VulkanDevice> vulkan_device_{nullptr};
    std::shared_ptr<VulkanCompositor> vulkan_compositor_{nullptr};

    bool loop_{false};

    // Cached audio decoders per media asset ID
    std::unordered_map<std::string, std::unique_ptr<StreamAudioDecoder>> audio_decoders_;
    Rational audio_cursor_pts_{0, 1};

    void init_audio_device();
    void sync_audio_decoders_on_seek(Rational pts);
};

} // namespace ave
