#include "ave/audio_mixer.hpp"

#include <cmath>
#include <algorithm>

namespace ave {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

void AudioMixer::calculate_pan_gains(float pan, float volume, float& out_left, float& out_right) noexcept {
    float clamped_pan = std::clamp(pan, -1.0f, 1.0f);
    // Constant power panning curve: angle from 0 (left) to pi/2 (right)
    float angle = (clamped_pan + 1.0f) * 0.25f * kPi;
    out_left = volume * std::cos(angle);
    out_right = volume * std::sin(angle);
}

void AudioMixer::set_track_params(const std::string& track_id, TrackMixParams params) {
    track_params_[track_id] = params;
}

TrackMixParams AudioMixer::track_params(const std::string& track_id) const {
    auto it = track_params_.find(track_id);
    if (it != track_params_.end()) {
        return it->second;
    }
    return TrackMixParams{};
}

void AudioMixer::mix(const std::vector<std::pair<std::string, const AudioBuffer*>>& track_buffers,
                     AudioBuffer& master_out) {
    master_out.clear();
    if (master_out.channels() != 2) {
        master_out.resize(master_out.frame_count(), 2, master_out.sample_rate());
    }

    // Determine if any track is soloed
    bool any_soloed = false;
    for (const auto& [track_id, _] : track_buffers) {
        auto it = track_params_.find(track_id);
        if (it != track_params_.end() && it->second.soloed) {
            any_soloed = true;
            break;
        }
    }

    float* dst = master_out.data();
    size_t master_frames = master_out.frame_count();

    for (const auto& [track_id, buf] : track_buffers) {
        if (!buf || buf->frame_count() == 0) continue;

        TrackMixParams p{};
        auto it = track_params_.find(track_id);
        if (it != track_params_.end()) {
            p = it->second;
        }

        if (p.muted) continue;
        if (any_soloed && !p.soloed) continue;

        float left_gain = 0.0f;
        float right_gain = 0.0f;
        calculate_pan_gains(p.pan, p.volume * master_volume_, left_gain, right_gain);

        const float* src = buf->data();
        size_t frames = std::min(master_frames, buf->frame_count());

        if (buf->channels() == 2) {
            for (size_t f = 0; f < frames; ++f) {
                dst[f * 2 + 0] += src[f * 2 + 0] * left_gain;
                dst[f * 2 + 1] += src[f * 2 + 1] * right_gain;
            }
        } else if (buf->channels() == 1) {
            for (size_t f = 0; f < frames; ++f) {
                float mono_sample = src[f];
                dst[f * 2 + 0] += mono_sample * left_gain;
                dst[f * 2 + 1] += mono_sample * right_gain;
            }
        }
    }

    // Apply master limiter to prevent digital wrap-around clipping
    master_out.apply_soft_limiter(0.95f);
}

} // namespace ave
