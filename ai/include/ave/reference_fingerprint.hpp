#pragma once

#include <ave/perception_report.hpp>
#include <ave/time.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ave {

class Timeline;

struct ShotLengthDistribution {
    double median_sec = 0.0;
    double p25_sec = 0.0;
    double p75_sec = 0.0;
    double iqr_sec = 0.0;
    double burstiness = 0.0; // [-1.0, 1.0]: (stddev - mean)/(stddev + mean)
    std::vector<std::pair<Rational, double>> pacing_curve; // sliding window cuts per minute
};

struct CutToBeatDistribution {
    double beat_sync_ratio = 0.0;     // Fraction of cuts within +/-70ms of a beat [0.0, 1.0]
    double downbeat_sync_ratio = 0.0; // Fraction of cuts on downbeats [0.0, 1.0]
    double offbeat_ratio = 1.0;       // 1.0 - beat_sync_ratio
    double mean_offset_ms = 0.0;      // Signed average ms offset between cut and nearest beat
};

struct TransitionMix {
    double hard_cut_ratio = 1.0;
    double dissolve_ratio = 0.0;
    double fade_ratio = 0.0;
};

struct MotionProfile {
    double mean_motion_energy = 0.0;
    double motion_variance = 0.0;
};

/**
 * @brief Multi-dimensional editing style representation (Fingerprint v2).
 *
 * Captures pacing distributions, cut-to-beat alignment, transition ratios,
 * motion energy dynamics, color look, and speech density.
 */
struct ReferenceFingerprint {
    ShotLengthDistribution pacing;
    CutToBeatDistribution rhythm;
    TransitionMix transitions;
    MotionProfile motion;
    ColorLook color;
    double integrated_loudness_lufs = -14.0;
    double tempo_bpm = 120.0;
    double speech_density = 0.0;

    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] static ReferenceFingerprint from_json(const std::string& json_str);
};

struct FingerprintDistanceWeights {
    double pacing_weight = 0.30;
    double rhythm_weight = 0.25;
    double transition_weight = 0.15;
    double motion_weight = 0.15;
    double color_weight = 0.15;
};

struct FingerprintDistanceBreakdown {
    double total_distance = 0.0; // Scaled to [0.0, 1.0]
    double pacing_dist = 0.0;
    double rhythm_dist = 0.0;
    double transition_dist = 0.0;
    double motion_dist = 0.0;
    double color_dist = 0.0;
};

/**
 * @brief Extractor and distance evaluator for Reference Fingerprint v2.
 */
class FingerprintEngine {
public:
    FingerprintEngine() = default;

    /**
     * @brief Computes reference fingerprint from a PerceptionReport.
     */
    [[nodiscard]] static ReferenceFingerprint from_perception_report(const PerceptionReport& report);

    /**
     * @brief Computes fingerprint from a native Timeline and rhythm perception data.
     */
    [[nodiscard]] static ReferenceFingerprint from_timeline(
        const Timeline& timeline,
        const AudioRhythmReport& rhythm,
        double motion_energy = 0.0,
        ColorLook color_look = {}
    );

    /**
     * @brief Computes normalized scalar distance D(F1, F2) in [0.0, 1.0].
     * 0.0 = identical editing style; 1.0 = completely divergent style.
     */
    [[nodiscard]] static double compute_distance(
        const ReferenceFingerprint& a,
        const ReferenceFingerprint& b,
        FingerprintDistanceWeights weights = {}
    );

    /**
     * @brief Computes dimensional distance breakdown across all stylistic axes.
     */
    [[nodiscard]] static FingerprintDistanceBreakdown compute_distance_detailed(
        const ReferenceFingerprint& a,
        const ReferenceFingerprint& b,
        FingerprintDistanceWeights weights = {}
    );
};

} // namespace ave
