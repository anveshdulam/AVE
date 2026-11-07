#pragma once

#include "ave/audio_buffer.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace ave {

struct TrackMixParams {
    float volume{1.0f};  // Linear gain (1.0 = 0 dB)
    float pan{0.0f};     // Pan: -1.0 (hard left), 0.0 (center), +1.0 (hard right)
    bool muted{false};
    bool soloed{false};
};

/**
 * @brief Multi-track sample-accurate audio mixer with constant-power panning,
 * track solo/mute routing, and master bus soft-limiting.
 */
class AudioMixer {
public:
    AudioMixer() = default;

    void set_track_params(const std::string& track_id, TrackMixParams params);
    [[nodiscard]] TrackMixParams track_params(const std::string& track_id) const;

    void set_master_volume(float volume) noexcept { master_volume_ = volume; }
    [[nodiscard]] float master_volume() const noexcept { return master_volume_; }

    /**
     * @brief Mixes multiple track audio buffers into a target master buffer.
     * @param track_buffers Pairs of {track_id, AudioBuffer pointer}.
     * @param master_out Destination master stereo buffer.
     */
    void mix(const std::vector<std::pair<std::string, const AudioBuffer*>>& track_buffers,
             AudioBuffer& master_out);

private:
    std::unordered_map<std::string, TrackMixParams> track_params_{};
    float master_volume_{1.0f};

    static void calculate_pan_gains(float pan, float volume, float& out_left, float& out_right) noexcept;
};

} // namespace ave

