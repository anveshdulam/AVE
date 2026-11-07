#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>

namespace ave {

/**
 * @brief Pre-allocated multi-channel 32-bit floating point audio buffer.
 * Samples are stored in interleaved format: [L0, R0, L1, R1, ...].
 */
class AudioBuffer {
public:
    AudioBuffer() = default;

    AudioBuffer(size_t frame_count, int channels = 2, int sample_rate = 48000)
        : channels_(channels > 0 ? channels : 2),
          sample_rate_(sample_rate > 0 ? sample_rate : 48000),
          data_(frame_count * static_cast<size_t>(channels_), 0.0f) {}

    [[nodiscard]] size_t frame_count() const noexcept {
        return channels_ > 0 ? data_.size() / static_cast<size_t>(channels_) : 0;
    }

    [[nodiscard]] size_t sample_count() const noexcept {
        return data_.size();
    }

    [[nodiscard]] int channels() const noexcept { return channels_; }
    [[nodiscard]] int sample_rate() const noexcept { return sample_rate_; }

    [[nodiscard]] float* data() noexcept { return data_.data(); }
    [[nodiscard]] const float* data() const noexcept { return data_.data(); }

    void resize(size_t frame_count, int channels = 2, int sample_rate = 48000) {
        channels_ = (channels > 0 ? channels : 2);
        sample_rate_ = (sample_rate > 0 ? sample_rate : 48000);
        data_.assign(frame_count * static_cast<size_t>(channels_), 0.0f);
    }

    void clear() noexcept {
        std::fill(data_.begin(), data_.end(), 0.0f);
    }

    void apply_gain(float gain) noexcept {
        for (float& s : data_) {
            s *= gain;
        }
    }

    /**
     * @brief Mixes (sums) another audio buffer into this one, with gain scaling.
     */
    void mix_from(const AudioBuffer& src, float gain = 1.0f) noexcept {
        size_t count = std::min(data_.size(), src.data_.size());
        for (size_t i = 0; i < count; ++i) {
            data_[i] += src.data_[i] * gain;
        }
    }

    /**
     * @brief Computes peak absolute amplitude and RMS power across all channels.
     */
    void compute_peak_and_rms(float& out_peak, float& out_rms) const noexcept {
        if (data_.empty()) {
            out_peak = 0.0f;
            out_rms = 0.0f;
            return;
        }

        float max_val = 0.0f;
        double sum_squares = 0.0;

        for (float s : data_) {
            float abs_val = std::abs(s);
            if (abs_val > max_val) max_val = abs_val;
            sum_squares += static_cast<double>(s) * static_cast<double>(s);
        }

        out_peak = max_val;
        out_rms = static_cast<float>(std::sqrt(sum_squares / static_cast<double>(data_.size())));
    }

    /**
     * @brief Soft clipping limiter to prevent harsh digital wrap-around.
     */
    void apply_soft_limiter(float threshold = 0.95f) noexcept {
        for (float& s : data_) {
            if (s > threshold) {
                float excess = s - threshold;
                s = threshold + (1.0f - threshold) * std::tanh(excess / (1.0f - threshold));
            } else if (s < -threshold) {
                float excess = -s - threshold;
                s = -(threshold + (1.0f - threshold) * std::tanh(excess / (1.0f - threshold)));
            }
        }
    }

private:
    int channels_{2};
    int sample_rate_{48000};
    std::vector<float> data_{};
};

} // namespace ave
