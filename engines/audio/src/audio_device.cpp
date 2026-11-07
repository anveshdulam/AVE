#include "ave/audio_device.hpp"

#include <chrono>

namespace ave {

NullAudioDevice::NullAudioDevice(int sample_rate, int channels, size_t buffer_frames)
    : sample_rate_(sample_rate > 0 ? sample_rate : 48000),
      channels_(channels > 0 ? channels : 2),
      buffer_frames_(buffer_frames > 0 ? buffer_frames : 512),
      internal_buffer_(buffer_frames_ * static_cast<size_t>(channels_), 0.0f) {}

NullAudioDevice::~NullAudioDevice() {
    stop();
}

bool NullAudioDevice::start(AudioRenderCallback callback) {
    if (running_.load()) return false;
    callback_ = std::move(callback);
    running_ = true;
    thread_ = std::thread(&NullAudioDevice::run_loop, this);
    return true;
}

void NullAudioDevice::stop() {
    if (!running_.load()) return;
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool NullAudioDevice::is_running() const noexcept {
    return running_.load();
}

void NullAudioDevice::render_synchronous(size_t frame_count, float* out_buffer) {
    if (!callback_ || frame_count == 0) return;

    if (out_buffer) {
        callback_(out_buffer, frame_count, channels_, sample_rate_);
    } else {
        if (internal_buffer_.size() < frame_count * static_cast<size_t>(channels_)) {
            internal_buffer_.resize(frame_count * static_cast<size_t>(channels_));
        }
        callback_(internal_buffer_.data(), frame_count, channels_, sample_rate_);
    }
}

void NullAudioDevice::run_loop() {
    // Calculate sleep duration per buffer
    double buffer_duration_sec = static_cast<double>(buffer_frames_) / static_cast<double>(sample_rate_);
    auto buffer_duration = std::chrono::microseconds(static_cast<int64_t>(buffer_duration_sec * 1000000.0));

    while (running_.load()) {
        auto start_time = std::chrono::steady_clock::now();

        if (callback_) {
            callback_(internal_buffer_.data(), buffer_frames_, channels_, sample_rate_);
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time);
        if (elapsed < buffer_duration) {
            std::this_thread::sleep_for(buffer_duration - elapsed);
        } else {
            underruns_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

} // namespace ave

