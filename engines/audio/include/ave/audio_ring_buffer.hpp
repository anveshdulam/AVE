#pragma once

#include <vector>
#include <atomic>
#include <algorithm>
#include <cstddef>

namespace ave {

/**
 * @brief Lock-free, wait-free Single-Producer Single-Consumer (SPSC) circular buffer
 * designed for real-time audio callback consumption without locks or allocations.
 */
class AudioRingBuffer {
public:
    explicit AudioRingBuffer(size_t frame_capacity = 32768, int channels = 2)
        : channels_(channels > 0 ? channels : 2),
          sample_capacity_((frame_capacity > 0 ? frame_capacity : 1024) * static_cast<size_t>(channels_)),
          buffer_(sample_capacity_, 0.0f),
          read_index_(0),
          write_index_(0) {}

    [[nodiscard]] int channels() const noexcept { return channels_; }
    [[nodiscard]] size_t frame_capacity() const noexcept { return sample_capacity_ / static_cast<size_t>(channels_); }

    /**
     * @brief Writes interleaved float samples into the ring buffer (Producer thread).
     * @return Number of frames actually written.
     */
    size_t write(const float* src, size_t frames) noexcept {
        if (!src || frames == 0) return 0;

        size_t samples_to_write = frames * static_cast<size_t>(channels_);
        size_t r = read_index_.load(std::memory_order_acquire);
        size_t w = write_index_.load(std::memory_order_relaxed);

        size_t available_samples = (r <= w) ? (sample_capacity_ - (w - r) - 1) : (r - w - 1);
        size_t count = std::min(samples_to_write, available_samples);
        if (count == 0) return 0;

        size_t first_chunk = std::min(count, sample_capacity_ - w);
        std::copy_n(src, first_chunk, buffer_.data() + w);

        size_t second_chunk = count - first_chunk;
        if (second_chunk > 0) {
            std::copy_n(src + first_chunk, second_chunk, buffer_.data());
        }

        size_t next_w = (w + count) % sample_capacity_;
        write_index_.store(next_w, std::memory_order_release);

        return count / static_cast<size_t>(channels_);
    }

    /**
     * @brief Reads interleaved float samples from the ring buffer (Consumer/Audio Callback thread).
     * If fewer samples are available than requested, remaining output is zero-filled (underrun silence).
     * @return Number of valid frames actually read (excluding zero fill).
     */
    size_t read(float* dst, size_t frames) noexcept {
        if (!dst || frames == 0) return 0;

        size_t samples_to_read = frames * static_cast<size_t>(channels_);
        size_t w = write_index_.load(std::memory_order_acquire);
        size_t r = read_index_.load(std::memory_order_relaxed);

        size_t available_samples = (w >= r) ? (w - r) : (sample_capacity_ - (r - w));
        size_t count = std::min(samples_to_read, available_samples);

        if (count > 0) {
            size_t first_chunk = std::min(count, sample_capacity_ - r);
            std::copy_n(buffer_.data() + r, first_chunk, dst);

            size_t second_chunk = count - first_chunk;
            if (second_chunk > 0) {
                std::copy_n(buffer_.data(), second_chunk, dst + first_chunk);
            }

            size_t next_r = (r + count) % sample_capacity_;
            read_index_.store(next_r, std::memory_order_release);
        }

        // Zero-fill underrun samples to prevent audio buzz/glitches
        if (count < samples_to_read) {
            std::fill_n(dst + count, samples_to_read - count, 0.0f);
        }

        return count / static_cast<size_t>(channels_);
    }

    [[nodiscard]] size_t available_read_frames() const noexcept {
        size_t w = write_index_.load(std::memory_order_acquire);
        size_t r = read_index_.load(std::memory_order_acquire);
        size_t avail = (w >= r) ? (w - r) : (sample_capacity_ - (r - w));
        return avail / static_cast<size_t>(channels_);
    }

    [[nodiscard]] size_t available_write_frames() const noexcept {
        size_t r = read_index_.load(std::memory_order_acquire);
        size_t w = write_index_.load(std::memory_order_acquire);
        size_t avail = (r <= w) ? (sample_capacity_ - (w - r) - 1) : (r - w - 1);
        return avail / static_cast<size_t>(channels_);
    }

    /**
     * @brief Clears all buffered frames. Caller must ensure the consumer/producer are
     * quiesced (e.g., audio device stopped, paused, or flushed) before resetting indices.
     */
    void reset() noexcept {
        read_index_.store(0, std::memory_order_release);
        write_index_.store(0, std::memory_order_release);
    }

private:
    int channels_{2};
    size_t sample_capacity_{65536};
    std::vector<float> buffer_{};
    std::atomic<size_t> read_index_{0};
    std::atomic<size_t> write_index_{0};
};

} // namespace ave

