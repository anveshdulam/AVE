#pragma once

#include <ave/edit_proposal.hpp>
#include <ave/reference_fingerprint.hpp>
#include <ave/time.hpp>

#include <string>
#include <vector>

namespace ave {

/**
 * @brief Candidate footage clip available in the media pool for planning.
 */
struct SourceClipCandidate {
    std::string clip_id;
    std::string asset_id;
    std::string name;
    Rational duration{0, 1};
    Rational source_in{0, 1};
    double motion_energy = 0.0;
    std::vector<float> embedding; // 1152-d SigLIP 2 embedding
    bool has_speech = false;
};

/**
 * @brief Tuning configuration for the deterministic cost solver.
 */
struct PlannerConfig {
    double sim_weight = 0.35;
    double motion_weight = 0.25;
    double repeat_penalty_weight = 0.25;
    double speech_boundary_weight = 0.15;
    double min_shot_duration_sec = 0.50;
    double max_shot_duration_sec = 10.00;
};

/**
 * @brief Deterministic beat-grid planner with dynamic programming / cost optimization.
 *
 * Generates structured EditProposalEnvelopes conforming to schemas/edit-proposal-envelope.schema.json.
 * Zero external LLM hallucination: all cuts snap strictly to exact Rational beat times.
 */
class DeterministicPlanner {
public:
    explicit DeterministicPlanner(PlannerConfig config = {});

    [[nodiscard]] const PlannerConfig& config() const noexcept { return m_config; }

    /**
     * @brief Generates an edit proposal fitting candidate footage to the music rhythm
     * and target reference fingerprint style.
     */
    [[nodiscard]] EditProposalEnvelope plan(
        const std::string& project_id,
        const std::string& target_track_id,
        const std::vector<SourceClipCandidate>& candidates,
        const ReferenceFingerprint& reference,
        const AudioRhythmReport& music_rhythm,
        Rational target_duration
    ) const;

private:
    PlannerConfig m_config;

    [[nodiscard]] std::vector<Rational> generate_beat_cut_grid(
        const AudioRhythmReport& rhythm,
        const ReferenceFingerprint& reference,
        Rational target_duration
    ) const;
};

} // namespace ave
