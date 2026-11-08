#include "ave/playback_session.hpp"
#include "ave/vulkan_device.hpp"
#include "ave/vulkan_compositor.hpp"

#include <algorithm>
#include <iostream>

namespace ave {

PlaybackSession::PlaybackSession(const Timeline& timeline,
                                 const MediaPool& media_pool,
                                 PlaybackSessionConfig config)
    : timeline_(timeline),
      media_pool_(media_pool),
      config_(config),
      controller_(timeline.frame_rate().numerator() > 0 ? timeline.frame_rate() : Rational{24, 1},
                  timeline.duration(),
                  config.video_queue_capacity),
      prefetcher_(timeline, media_pool, controller_, config.prefetch_window),
      audio_mixer_(),
      audio_ring_buffer_(config.audio_buffer_frames, config.audio_channels) {
    init_audio_device();
}

PlaybackSession::~PlaybackSession() {
    if (audio_device_) {
        audio_device_->stop();
    }
    prefetcher_.stop();
}

void PlaybackSession::init_audio_device() {
    audio_device_ = std::make_unique<NullAudioDevice>(
        static_cast<int>(config_.audio_sample_rate),
        config_.audio_channels,
        512
    );

    audio_device_->start([this](float* output, size_t frames, int, int) {
        audio_ring_buffer_.read(output, frames);
    });
}

void PlaybackSession::set_audio_device(std::unique_ptr<AudioDevice> device) {
    if (audio_device_) {
        audio_device_->stop();
    }
    audio_device_ = std::move(device);
    if (audio_device_) {
        audio_device_->start([this](float* output, size_t frames, int, int) {
            audio_ring_buffer_.read(output, frames);
        });
    }
}

void PlaybackSession::play() {
    controller_.clock().play();
    prefetcher_.start();
    pump_audio(4096);
}

void PlaybackSession::pause() {
    controller_.clock().pause();
    prefetcher_.stop();
}

void PlaybackSession::toggle_play() {
    if (controller_.clock().state() == PlaybackState::Playing) {
        pause();
    } else {
        play();
    }
}

void PlaybackSession::stop() {
    controller_.clock().pause();
    prefetcher_.stop();
    controller_.clock().stop();
    controller_.video_queue().clear();
    audio_ring_buffer_.reset();
    audio_cursor_pts_ = Rational{0, 1};
    sync_audio_decoders_on_seek(Rational{0, 1});
}

void PlaybackSession::seek(Rational target_pts) {
    if (target_pts < Rational{0, 1}) {
        target_pts = Rational{0, 1};
    }
    Rational dur = timeline_.duration();
    if (dur > Rational{0, 1} && target_pts > dur) {
        target_pts = dur;
    }

    controller_.seek(target_pts);
    uint64_t gen = controller_.clock().current_seek_generation();
    audio_ring_buffer_.reset();
    audio_cursor_pts_ = target_pts;

    sync_audio_decoders_on_seek(target_pts);

    prefetcher_.prefetch_frame_at(target_pts, gen);
    prefetcher_.trigger();

    pump_audio(2048);
}

void PlaybackSession::step(int64_t frame_count) {
    if (controller_.clock().state() == PlaybackState::Playing) {
        pause();
    }

    if (frame_count > 0) {
        controller_.clock().step_forward(static_cast<int>(frame_count));
    } else if (frame_count < 0) {
        controller_.clock().step_backward(static_cast<int>(-frame_count));
    }

    uint64_t gen = controller_.clock().current_seek_generation();
    prefetcher_.prefetch_frame_at(controller_.clock().current_time(), gen);
}

void PlaybackSession::set_speed(double speed) {
    controller_.clock().play(speed);
}

PlaybackState PlaybackSession::state() const noexcept {
    return controller_.clock().state();
}

Rational PlaybackSession::current_time() const noexcept {
    return controller_.clock().current_time();
}

double PlaybackSession::speed() const noexcept {
    return controller_.clock().playback_speed();
}

uint64_t PlaybackSession::current_seek_generation() const noexcept {
    return controller_.clock().current_seek_generation();
}

void PlaybackSession::set_vulkan_device(std::shared_ptr<VulkanDevice> device) {
    vulkan_device_ = std::move(device);
    if (vulkan_device_) {
        vulkan_compositor_ = VulkanCompositor::create(*vulkan_device_);
    } else {
        vulkan_compositor_.reset();
    }
}

PresentationResult PlaybackSession::update() {
    PresentationResult result;
    Rational cur_time = controller_.clock().current_time();
    Rational dur = timeline_.duration();

    if (dur > Rational{0, 1} && cur_time >= dur) {
        if (loop_) {
            seek(Rational{0, 1});
            cur_time = Rational{0, 1};
        } else if (controller_.clock().state() == PlaybackState::Playing) {
            pause();
            result.end_of_timeline = true;
            result.presentation_time = cur_time;
            result.action = FrameSyncAction::Hold;
            return result;
        }
    }

    result.presentation_time = cur_time;
    auto raw_frame = controller_.fetch_next_display_frame();

    const auto& v_tracks = timeline_.video_tracks();
    result.active_tracks_count = 0;
    for (const auto& trk : v_tracks) {
        if (!trk.muted() && trk.item_at(cur_time) != nullptr) {
            result.active_tracks_count++;
        }
    }

    if (config_.enable_compositing) {
        std::vector<VideoFrame> track_frames;
        if (raw_frame.has_value()) {
            track_frames.push_back(*raw_frame);
        }

        RenderContext ctx;
        ctx.target_width = timeline_.width();
        ctx.target_height = timeline_.height();
        ctx.timeline_time = cur_time;
        ctx.frame_rate = timeline_.frame_rate();

        auto graph = RenderGraph::build_for_timeline(timeline_, cur_time, track_frames, ctx);
        const RenderImage* comp_img = graph->execute(ctx);
        if (comp_img) {
            result.frame = comp_img->to_video_frame(cur_time,
                                                    Rational{1, timeline_.frame_rate().numerator() > 0 ? timeline_.frame_rate().numerator() : 24},
                                                    controller_.clock().current_seek_generation());
            result.is_composited = true;
            result.action = FrameSyncAction::Present;
        } else {
            result.frame = std::move(raw_frame);
            result.action = result.frame.has_value() ? FrameSyncAction::Present : FrameSyncAction::Hold;
        }
    } else {
        result.frame = std::move(raw_frame);
        result.action = result.frame.has_value() ? FrameSyncAction::Present : FrameSyncAction::Hold;
    }

    if (controller_.clock().state() == PlaybackState::Playing &&
        audio_ring_buffer_.available_write_frames() >= 2048) {
        pump_audio(2048);
    }

    return result;
}

void PlaybackSession::sync_audio_decoders_on_seek(Rational pts) {
    for (auto& [asset_id, decoder] : audio_decoders_) {
        if (decoder && decoder->is_open()) {
            decoder->seek_pts(pts);
        }
    }
}

void PlaybackSession::pump_audio(size_t sample_frames_needed) {
    if (sample_frames_needed == 0) return;

    size_t writable = audio_ring_buffer_.available_write_frames();
    if (writable == 0) return;

    size_t frames_to_generate = std::min(sample_frames_needed, writable);
    Rational window_duration{static_cast<int64_t>(frames_to_generate), static_cast<int64_t>(config_.audio_sample_rate)};
    Rational window_end = audio_cursor_pts_ + window_duration;

    std::vector<std::pair<std::string, const AudioBuffer*>> track_buffers;
    std::vector<std::unique_ptr<AudioBuffer>> allocated_buffers;

    for (const auto& track : timeline_.audio_tracks()) {
        if (track.muted()) continue;

        for (const auto& item : track.items()) {
            Rational clip_start = item.timeline_in();
            Rational clip_end = clip_start + item.duration();

            if (clip_end <= audio_cursor_pts_ || clip_start >= window_end) {
                continue;
            }

            const MediaAsset* asset = media_pool_.find_asset(item.asset_id());
            if (!asset) continue;

            auto it = audio_decoders_.find(item.asset_id());
            if (it == audio_decoders_.end()) {
                auto dec = std::make_unique<StreamAudioDecoder>();
                if (dec->open(asset->file_path(), config_.audio_sample_rate, config_.audio_channels)) {
                    auto res = audio_decoders_.insert_or_assign(item.asset_id(), std::move(dec));
                    it = res.first;
                } else {
                    continue;
                }
            }

            auto opt_buf = it->second->decode_next_audio();
            if (opt_buf.has_value()) {
                auto buf_ptr = std::make_unique<AudioBuffer>(std::move(*opt_buf));
                track_buffers.emplace_back(track.id(), buf_ptr.get());
                allocated_buffers.push_back(std::move(buf_ptr));
            }
        }
    }

    if (!track_buffers.empty()) {
        AudioBuffer mix_out(frames_to_generate, config_.audio_channels, static_cast<int>(config_.audio_sample_rate));
        audio_mixer_.mix(track_buffers, mix_out);
        audio_ring_buffer_.write(mix_out.data(), mix_out.frame_count());
    } else {
        std::vector<float> silence(frames_to_generate * static_cast<size_t>(config_.audio_channels), 0.0f);
        audio_ring_buffer_.write(silence.data(), frames_to_generate);
    }

    audio_cursor_pts_ = window_end;
}

SessionMetrics PlaybackSession::metrics() const {
    SessionMetrics m;
    m.video_metrics = controller_.metrics();
    m.audio_underruns = (audio_device_ != nullptr) ? audio_device_->underrun_count() : 0;
    m.current_pts = controller_.clock().current_time();
    m.state = controller_.clock().state();
    m.speed = controller_.clock().playback_speed();
    return m;
}

} // namespace ave
