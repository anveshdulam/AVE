#include "ave/audio_device.hpp"
#include "ave/wasapi_audio_device.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>

namespace ave {

const char* to_string(AudioSampleFormat fmt) noexcept {
    switch (fmt) {
        case AudioSampleFormat::Float32: return "float32";
        case AudioSampleFormat::Int16:   return "int16";
        case AudioSampleFormat::Int24:   return "int24";
        case AudioSampleFormat::Int32:   return "int32";
        case AudioSampleFormat::Unknown:
        default:                         return "unknown";
    }
}

const char* to_string(AudioDeviceState state) noexcept {
    switch (state) {
        case AudioDeviceState::Uninitialized: return "Uninitialized";
        case AudioDeviceState::Stopped:       return "Stopped";
        case AudioDeviceState::Running:       return "Running";
        case AudioDeviceState::Paused:        return "Paused";
        case AudioDeviceState::Error:         return "Error";
        default:                              return "Unknown";
    }
}

std::string AudioDeviceDiagnostics::format_summary() const {
    std::ostringstream oss;
    if (!last_error.empty()) {
        oss << backend_name << " Unavailable (" << last_error << ")";
        return oss.str();
    }
    if (!is_physical) {
        oss << backend_name << " (Simulated / Headless; "
            << sample_rate << " Hz, " << channels << "ch, "
            << to_string(sample_format) << ", " << to_string(state) << ")";
        return oss.str();
    }
    oss << backend_name << " ("
        << (device_name.empty() ? "Default Endpoint" : device_name)
        << " | " << sample_rate << " Hz, " << channels << "ch, "
        << to_string(sample_format);
    if (hardware_sample_rate > 0 &&
        (hardware_sample_rate != sample_rate ||
         hardware_channels != channels ||
         hardware_sample_format != sample_format)) {
        oss << " -> HW " << hardware_sample_rate << " Hz, "
            << hardware_channels << "ch, " << to_string(hardware_sample_format);
    }
    oss << " | " << to_string(state);
    if (underrun_count > 0) {
        oss << " | Underruns: " << underrun_count;
    }
    oss << ")";
    return oss.str();
}

// ============================================================================
// AudioFormatConverter
// ============================================================================

bool AudioFormatConverter::configure(int src_rate,
                                     int src_channels,
                                     int dst_rate,
                                     int dst_channels,
                                     AudioSampleFormat dst_format,
                                     size_t max_dst_frames,
                                     int dst_valid_bits) {
    if (src_rate < 8000 || src_rate > 384000 ||
        dst_rate < 8000 || dst_rate > 384000 ||
        src_channels < 1 || src_channels > 16 ||
        dst_channels < 1 || dst_channels > 16 ||
        dst_format == AudioSampleFormat::Unknown ||
        max_dst_frames == 0) {
        return false;
    }

    src_rate_ = src_rate;
    src_channels_ = src_channels;
    dst_rate_ = dst_rate;
    dst_channels_ = dst_channels;
    dst_format_ = dst_format;
    if (dst_valid_bits > 0) {
        dst_valid_bits_ = dst_valid_bits;
    } else {
        switch (dst_format) {
            case AudioSampleFormat::Int16: dst_valid_bits_ = 16; break;
            case AudioSampleFormat::Int24: dst_valid_bits_ = 24; break;
            case AudioSampleFormat::Int32:
            case AudioSampleFormat::Float32:
            default:                       dst_valid_bits_ = 32; break;
        }
    }
    max_dst_frames_ = max_dst_frames;
    step_ = static_cast<double>(src_rate_) / static_cast<double>(dst_rate_);
    passthrough_ = (src_rate_ == dst_rate_ && src_channels_ == dst_channels_);

    const double worst_case_src = std::ceil(static_cast<double>(max_dst_frames_) * step_) + 16.0;
    max_src_frames_ = static_cast<size_t>(worst_case_src);

    mapped_scratch_.assign(max_src_frames_ * static_cast<size_t>(dst_channels_), 0.0f);
    resampled_scratch_.assign(max_dst_frames_ * static_cast<size_t>(dst_channels_), 0.0f);
    history_buf_.assign((max_src_frames_ + 8u) * static_cast<size_t>(dst_channels_), 0.0f);

    reset_state();
    return true;
}

void AudioFormatConverter::reset_state() noexcept {
    phase_ = 0.0;
    hist_valid_ = 0;
    std::fill(history_buf_.begin(), history_buf_.end(), 0.0f);
    std::fill(mapped_scratch_.begin(), mapped_scratch_.end(), 0.0f);
    std::fill(resampled_scratch_.begin(), resampled_scratch_.end(), 0.0f);
}

size_t AudioFormatConverter::bytes_per_dst_frame() const noexcept {
    size_t bps = 4;
    switch (dst_format_) {
        case AudioSampleFormat::Int16:   bps = 2; break;
        case AudioSampleFormat::Int24:   bps = 3; break;
        case AudioSampleFormat::Int32:   bps = 4; break;
        case AudioSampleFormat::Float32: bps = 4; break;
        case AudioSampleFormat::Unknown:
        default:                         bps = 4; break;
    }
    return bps * static_cast<size_t>(dst_channels_);
}

size_t AudioFormatConverter::next_src_frames_needed(size_t dst_frames) const noexcept {
    if (dst_frames == 0) {
        return 0;
    }
    if (src_rate_ == dst_rate_) {
        return dst_frames;
    }
    const double end_pos = phase_ + static_cast<double>(dst_frames) * step_;
    const size_t consumed = static_cast<size_t>(std::floor(end_pos));
    const size_t required_total = std::max<size_t>(consumed + 2u, 2u);
    return (required_total > hist_valid_) ? (required_total - hist_valid_) : 0u;
}

void AudioFormatConverter::sanitize_floats(const float* src, float* dst, size_t sample_count) noexcept {
    if (!dst || sample_count == 0) {
        return;
    }
    if (!src) {
        std::fill_n(dst, sample_count, 0.0f);
        return;
    }
    for (size_t i = 0; i < sample_count; ++i) {
        const float s = src[i];
        dst[i] = std::isfinite(s) ? std::clamp(s, -1.0f, 1.0f) : 0.0f;
    }
}

void AudioFormatConverter::map_channels(const float* src,
                                        int src_channels,
                                        float* dst,
                                        int dst_channels,
                                        size_t frames) noexcept {
    if (!dst || frames == 0 || src_channels <= 0 || dst_channels <= 0) {
        return;
    }
    if (!src) {
        std::fill_n(dst, frames * static_cast<size_t>(dst_channels), 0.0f);
        return;
    }

    if (src_channels == dst_channels) {
        sanitize_floats(src, dst, frames * static_cast<size_t>(dst_channels));
        return;
    }

    auto clean = [](float s) noexcept {
        return std::isfinite(s) ? std::clamp(s, -1.0f, 1.0f) : 0.0f;
    };

    if (src_channels == 1) {
        for (size_t f = 0; f < frames; ++f) {
            const float m = clean(src[f]);
            float* out = dst + f * static_cast<size_t>(dst_channels);
            out[0] = m;
            if (dst_channels > 1) {
                out[1] = m;
            }
            for (int c = 2; c < dst_channels; ++c) {
                out[c] = 0.0f;
            }
        }
        return;
    }

    if (dst_channels == 1) {
        for (size_t f = 0; f < frames; ++f) {
            const float* in = src + f * static_cast<size_t>(src_channels);
            dst[f] = 0.5f * (clean(in[0]) + clean(in[1]));
        }
        return;
    }

    const int copy_ch = std::min(src_channels, dst_channels);
    for (size_t f = 0; f < frames; ++f) {
        const float* in = src + f * static_cast<size_t>(src_channels);
        float* out = dst + f * static_cast<size_t>(dst_channels);
        for (int c = 0; c < copy_ch; ++c) {
            out[c] = clean(in[c]);
        }
        for (int c = copy_ch; c < dst_channels; ++c) {
            out[c] = 0.0f;
        }
    }
}

void AudioFormatConverter::encode_samples(const float* src_interleaved,
                                          size_t sample_count,
                                          AudioSampleFormat dst_format,
                                          int dst_valid_bits,
                                          uint8_t* dst_bytes) noexcept {
    if (!dst_bytes || sample_count == 0) {
        return;
    }
    if (!src_interleaved) {
        size_t bps = 4;
        if (dst_format == AudioSampleFormat::Int16) bps = 2;
        else if (dst_format == AudioSampleFormat::Int24) bps = 3;
        std::memset(dst_bytes, 0, sample_count * bps);
        return;
    }

    switch (dst_format) {
        case AudioSampleFormat::Float32: {
            float* out = reinterpret_cast<float*>(dst_bytes);
            sanitize_floats(src_interleaved, out, sample_count);
            break;
        }
        case AudioSampleFormat::Int16: {
            auto* out = reinterpret_cast<int16_t*>(dst_bytes);
            for (size_t i = 0; i < sample_count; ++i) {
                const float s = std::isfinite(src_interleaved[i])
                                    ? std::clamp(src_interleaved[i], -1.0f, 1.0f)
                                    : 0.0f;
                const long scaled = std::lrint(static_cast<double>(s) * 32767.0);
                out[i] = static_cast<int16_t>(std::clamp<long>(scaled, -32768L, 32767L));
            }
            break;
        }
        case AudioSampleFormat::Int24: {
            for (size_t i = 0; i < sample_count; ++i) {
                const float s = std::isfinite(src_interleaved[i])
                                    ? std::clamp(src_interleaved[i], -1.0f, 1.0f)
                                    : 0.0f;
                const int64_t scaled = std::llround(static_cast<double>(s) * 8388607.0);
                const int32_t v = static_cast<int32_t>(
                    std::clamp<int64_t>(scaled, -8388608LL, 8388607LL));
                const uint32_t uv = static_cast<uint32_t>(v);
                dst_bytes[i * 3 + 0] = static_cast<uint8_t>(uv & 0xFFu);
                dst_bytes[i * 3 + 1] = static_cast<uint8_t>((uv >> 8) & 0xFFu);
                dst_bytes[i * 3 + 2] = static_cast<uint8_t>((uv >> 16) & 0xFFu);
            }
            break;
        }
        case AudioSampleFormat::Int32: {
            auto* out = reinterpret_cast<int32_t*>(dst_bytes);
            const bool mask_24_in_32 = (dst_valid_bits == 24);
            for (size_t i = 0; i < sample_count; ++i) {
                const float s = std::isfinite(src_interleaved[i])
                                    ? std::clamp(src_interleaved[i], -1.0f, 1.0f)
                                    : 0.0f;
                if (mask_24_in_32) {
                    const int64_t scaled = std::llround(static_cast<double>(s) * 8388607.0);
                    const int32_t v = static_cast<int32_t>(
                        std::clamp<int64_t>(scaled, -8388608LL, 8388607LL));
                    out[i] = static_cast<int32_t>(static_cast<uint32_t>(v) << 8);
                } else {
                    const double scaled = static_cast<double>(s) * 2147483647.0;
                    const double clamped = std::clamp(scaled, -2147483648.0, 2147483647.0);
                    out[i] = static_cast<int32_t>(std::llround(clamped));
                }
            }
            break;
        }
        case AudioSampleFormat::Unknown:
        default:
            break;
    }
}

bool AudioFormatConverter::process(const float* src_interleaved,
                                   size_t new_src_frames,
                                   uint8_t* dst_bytes,
                                   size_t dst_frames) noexcept {
    if (!dst_bytes || dst_frames == 0 || dst_frames > max_dst_frames_) {
        return false;
    }

    const size_t dch = static_cast<size_t>(dst_channels_);
    if (src_rate_ == dst_rate_) {
        if (new_src_frames < dst_frames) {
            return false;
        }
        if (passthrough_ && dst_format_ == AudioSampleFormat::Float32) {
            encode_samples(src_interleaved, dst_frames * dch, AudioSampleFormat::Float32, 32, dst_bytes);
            return true;
        }
        if (dst_frames > mapped_scratch_.size() / dch) {
            return false;
        }
        map_channels(src_interleaved, src_channels_, mapped_scratch_.data(), dst_channels_, dst_frames);
        encode_samples(mapped_scratch_.data(), dst_frames * dch, dst_format_, dst_valid_bits_, dst_bytes);
        return true;
    }

    const double end_pos = phase_ + static_cast<double>(dst_frames) * step_;
    const size_t consumed = static_cast<size_t>(std::floor(end_pos));
    const size_t required_total = std::max<size_t>(consumed + 2u, 2u);
    const size_t expected_new = (required_total > hist_valid_) ? (required_total - hist_valid_) : 0u;
    if (new_src_frames < expected_new) {
        return false;
    }
    if (hist_valid_ + expected_new > history_buf_.size() / dch) {
        return false;
    }

    if (expected_new > 0) {
        map_channels(src_interleaved,
                     src_channels_,
                     history_buf_.data() + hist_valid_ * dch,
                     dst_channels_,
                     expected_new);
    }

    const size_t total_valid = hist_valid_ + expected_new;
    for (size_t i = 0; i < dst_frames; ++i) {
        const double pos = phase_ + static_cast<double>(i) * step_;
        const size_t idx0 = std::min(static_cast<size_t>(pos), total_valid - 2u);
        const size_t idx1 = idx0 + 1u;
        const float frac = static_cast<float>(pos - static_cast<double>(idx0));
        const float inv_frac = 1.0f - frac;
        const float* f0 = history_buf_.data() + idx0 * dch;
        const float* f1 = history_buf_.data() + idx1 * dch;
        float* out = resampled_scratch_.data() + i * dch;
        for (size_t c = 0; c < dch; ++c) {
            out[c] = f0[c] * inv_frac + f1[c] * frac;
        }
    }

    const size_t remaining = (total_valid > consumed) ? (total_valid - consumed) : 0u;
    if (remaining > 0 && consumed > 0) {
        std::memmove(history_buf_.data(),
                     history_buf_.data() + consumed * dch,
                     remaining * dch * sizeof(float));
    }
    hist_valid_ = remaining;
    phase_ = end_pos - static_cast<double>(consumed);

    encode_samples(resampled_scratch_.data(), dst_frames * dch, dst_format_, dst_valid_bits_, dst_bytes);
    return true;
}

// ============================================================================
// NullAudioDevice
// ============================================================================

NullAudioDevice::NullAudioDevice(int sample_rate, int channels, size_t buffer_frames) {
    AudioDeviceConfig cfg;
    cfg.sample_rate = (sample_rate > 0 ? sample_rate : 48000);
    cfg.channels = (channels > 0 ? channels : 2);
    cfg.buffer_frames = (buffer_frames > 0 ? buffer_frames : 512);
    cfg.sample_format = AudioSampleFormat::Float32;
    initialize(cfg);
}

NullAudioDevice::~NullAudioDevice() {
    shutdown();
}

bool NullAudioDevice::initialize(const AudioDeviceConfig& config) {
    stop();

    if (!config.is_valid()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = "Invalid audio configuration for NullAudioDevice";
        initialized_.store(false, std::memory_order_release);
        state_.store(AudioDeviceState::Error, std::memory_order_release);
        return false;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    config_ = config;
    const size_t ring_cap = std::max<size_t>(8192u, config_.buffer_frames * 8u);
    queued_pcm_ = std::make_unique<AudioRingBuffer>(ring_cap, config_.channels);
    sanitize_scratch_.assign(ring_cap * static_cast<size_t>(config_.channels), 0.0f);
    internal_buffer_.assign(config_.buffer_frames * static_cast<size_t>(config_.channels), 0.0f);
    last_error_.clear();
    initialized_.store(true, std::memory_order_release);
    state_.store(AudioDeviceState::Stopped, std::memory_order_release);
    return true;
}

bool NullAudioDevice::start(AudioRenderCallback callback) {
    const AudioDeviceState cur = state_.load(std::memory_order_acquire);
    if (!initialized_.load(std::memory_order_acquire) || cur == AudioDeviceState::Error) {
        return false;
    }
    if (cur == AudioDeviceState::Running) {
        return false;
    }

    if (thread_.joinable()) {
        stop_requested_.store(true, std::memory_order_release);
        control_cv_.notify_all();
        thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (callback) {
            callback_ = std::move(callback);
        }
        stop_requested_.store(false, std::memory_order_release);
        flush_requested_.store(false, std::memory_order_release);
        state_.store(AudioDeviceState::Running, std::memory_order_release);
    }

    thread_ = std::thread(&NullAudioDevice::run_loop, this);
    return true;
}

bool NullAudioDevice::pause() {
    if (state_.load(std::memory_order_acquire) != AudioDeviceState::Running) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.store(AudioDeviceState::Paused, std::memory_order_release);
    }
    control_cv_.notify_all();
    return true;
}

bool NullAudioDevice::resume() {
    if (state_.load(std::memory_order_acquire) != AudioDeviceState::Paused) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.store(AudioDeviceState::Running, std::memory_order_release);
    }
    control_cv_.notify_all();
    return true;
}

void NullAudioDevice::flush() {
    std::unique_lock<std::mutex> lock(state_mutex_);
    if (thread_.joinable() && !stop_requested_.load(std::memory_order_acquire)) {
        flush_requested_.store(true, std::memory_order_release);
        control_cv_.notify_all();
        flush_cv_.wait(lock, [this]() {
            return !flush_requested_.load(std::memory_order_acquire) ||
                   stop_requested_.load(std::memory_order_acquire) ||
                   !thread_.joinable();
        });
    }
    if (queued_pcm_) {
        queued_pcm_->reset();
    }
}

void NullAudioDevice::stop() {
    std::thread to_join;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        stop_requested_.store(true, std::memory_order_release);
        if (state_.load(std::memory_order_acquire) != AudioDeviceState::Error &&
            initialized_.load(std::memory_order_acquire)) {
            state_.store(AudioDeviceState::Stopped, std::memory_order_release);
        }
        to_join = std::move(thread_);
    }
    control_cv_.notify_all();
    flush_cv_.notify_all();
    if (to_join.joinable()) {
        to_join.join();
    }
    if (queued_pcm_) {
        queued_pcm_->reset();
    }
}

void NullAudioDevice::shutdown() {
    stop();
    std::lock_guard<std::mutex> lock(state_mutex_);
    callback_ = nullptr;
    if (queued_pcm_) {
        queued_pcm_->reset();
    }
}

size_t NullAudioDevice::submit_pcm(const float* interleaved_frames, size_t frame_count) {
    if (!interleaved_frames || frame_count == 0 || !initialized_.load(std::memory_order_acquire) ||
        state_.load(std::memory_order_acquire) == AudioDeviceState::Error || !queued_pcm_) {
        return 0;
    }
    const size_t writable = queued_pcm_->available_write_frames();
    const size_t accepted = std::min(frame_count, writable);
    if (accepted == 0) {
        return 0;
    }
    const size_t sample_count = accepted * static_cast<size_t>(config_.channels);
    AudioFormatConverter::sanitize_floats(interleaved_frames, sanitize_scratch_.data(), sample_count);
    const size_t written = queued_pcm_->write(sanitize_scratch_.data(), accepted);
    frames_submitted_.fetch_add(written, std::memory_order_relaxed);
    return written;
}

bool NullAudioDevice::is_running() const noexcept {
    return state_.load(std::memory_order_acquire) == AudioDeviceState::Running;
}

bool NullAudioDevice::is_paused() const noexcept {
    return state_.load(std::memory_order_acquire) == AudioDeviceState::Paused;
}

AudioDeviceState NullAudioDevice::state() const noexcept {
    return state_.load(std::memory_order_acquire);
}

std::string NullAudioDevice::last_error() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_error_;
}

size_t NullAudioDevice::queued_frames() const noexcept {
    return queued_pcm_ ? queued_pcm_->available_read_frames() : 0u;
}

size_t NullAudioDevice::queue_capacity_frames() const noexcept {
    return queued_pcm_ ? (queued_pcm_->frame_capacity() > 0 ? queued_pcm_->frame_capacity() - 1u : 0u) : 0u;
}

AudioDeviceDiagnostics NullAudioDevice::diagnostics() const {
    AudioDeviceDiagnostics d;
    d.available = false;
    d.is_physical = false;
    d.backend_name = backend_name();
    d.device_name = device_name();
    d.sample_rate = config_.sample_rate;
    d.channels = config_.channels;
    d.sample_format = config_.sample_format;
    d.buffer_frames = config_.buffer_frames;
    d.hardware_sample_rate = config_.sample_rate;
    d.hardware_channels = config_.channels;
    d.hardware_sample_format = config_.sample_format;
    d.state = state_.load(std::memory_order_acquire);
    d.underrun_count = underruns_.load(std::memory_order_relaxed);
    d.frames_submitted = frames_submitted_.load(std::memory_order_relaxed);
    d.last_error = last_error();
    return d;
}

void NullAudioDevice::pull_block(float* dst, size_t frame_count) {
    const size_t ch = static_cast<size_t>(config_.channels);
    if (queued_pcm_ && queued_pcm_->available_read_frames() > 0) {
        const size_t read = queued_pcm_->read(dst, frame_count);
        if (read < frame_count) {
            if (callback_) {
                callback_(dst + read * ch, frame_count - read, config_.channels, config_.sample_rate);
            } else {
                underruns_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return;
    }

    if (callback_) {
        callback_(dst, frame_count, config_.channels, config_.sample_rate);
    } else {
        std::fill_n(dst, frame_count * ch, 0.0f);
    }
}

void NullAudioDevice::render_synchronous(size_t frame_count, float* out_buffer) {
    if (frame_count == 0) {
        return;
    }
    if (out_buffer) {
        pull_block(out_buffer, frame_count);
        return;
    }
    const size_t needed = frame_count * static_cast<size_t>(config_.channels);
    if (internal_buffer_.size() < needed) {
        internal_buffer_.resize(needed, 0.0f);
    }
    pull_block(internal_buffer_.data(), frame_count);
}

void NullAudioDevice::run_loop() {
    const double buffer_duration_sec =
        static_cast<double>(config_.buffer_frames) / static_cast<double>(config_.sample_rate);
    const auto buffer_duration =
        std::chrono::microseconds(std::max<int64_t>(1000, static_cast<int64_t>(buffer_duration_sec * 1000000.0)));

    while (!stop_requested_.load(std::memory_order_acquire)) {
        if (flush_requested_.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (queued_pcm_) {
                queued_pcm_->reset();
            }
            flush_requested_.store(false, std::memory_order_release);
            flush_cv_.notify_all();
            continue;
        }

        if (state_.load(std::memory_order_acquire) != AudioDeviceState::Running) {
            std::unique_lock<std::mutex> lock(state_mutex_);
            control_cv_.wait(lock, [this]() {
                return stop_requested_.load(std::memory_order_acquire) ||
                       flush_requested_.load(std::memory_order_acquire) ||
                       state_.load(std::memory_order_acquire) == AudioDeviceState::Running;
            });
            continue;
        }

        const auto start_time = std::chrono::steady_clock::now();
        pull_block(internal_buffer_.data(), config_.buffer_frames);

        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_time);
        if (elapsed < buffer_duration) {
            std::unique_lock<std::mutex> lock(state_mutex_);
            control_cv_.wait_for(lock, buffer_duration - elapsed, [this]() {
                return stop_requested_.load(std::memory_order_acquire) ||
                       flush_requested_.load(std::memory_order_acquire) ||
                       state_.load(std::memory_order_acquire) != AudioDeviceState::Running;
            });
        } else {
            underruns_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

std::unique_ptr<AudioDevice> create_platform_audio_device() {
#ifdef _WIN32
    return std::make_unique<WasapiAudioDevice>();
#else
    return nullptr;
#endif
}

} // namespace ave
