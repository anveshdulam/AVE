#include <ave/deterministic_planner.hpp>
#include <ave/uuid.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace ave {

DeterministicPlanner::DeterministicPlanner(PlannerConfig config)
    : m_config(config) {}

std::vector<Rational> DeterministicPlanner::generate_beat_cut_grid(
    const AudioRhythmReport& rhythm,
    const ReferenceFingerprint& reference,
    Rational target_duration
) const {
    std::vector<Rational> cuts;
    if (target_duration.to_seconds() <= 0.0) {
        return cuts;
    }

    cuts.push_back(Rational(0, 1));

    double target_total_sec = target_duration.to_seconds();
    double median_shot_sec = reference.pacing.median_sec;
    if (median_shot_sec < m_config.min_shot_duration_sec) {
        median_shot_sec = 2.0; // Sensible default: 2.0s shots
    }
    median_shot_sec = (std::min)(median_shot_sec, m_config.max_shot_duration_sec);

    if (rhythm.beats.empty()) {
        // Fallback: regular geometric cut grid
        double current_sec = median_shot_sec;
        while (current_sec < target_total_sec - 0.25) {
            cuts.push_back(Rational(static_cast<int64_t>(std::round(current_sec * 1000.0)), 1000));
            current_sec += median_shot_sec;
        }
        cuts.push_back(target_duration);
        return cuts;
    }

    // Grid snap over detected beats
    Rational current_pts(0, 1);
    while (current_pts.to_seconds() < target_total_sec - m_config.min_shot_duration_sec) {
        double ideal_next_sec = current_pts.to_seconds() + median_shot_sec;
        if (ideal_next_sec >= target_total_sec - 0.25) {
            break;
        }

        // Find candidate beat closest to ideal_next_sec
        size_t best_idx = 0;
        double best_diff = 1e9;
        for (size_t b = 0; b < rhythm.beats.size(); ++b) {
            const auto& beat = rhythm.beats[b];
            double b_sec = beat.pts.to_seconds();
            if (b_sec <= current_pts.to_seconds() + m_config.min_shot_duration_sec) {
                continue;
            }
            if (b_sec > current_pts.to_seconds() + m_config.max_shot_duration_sec) {
                break;
            }

            double diff = std::abs(b_sec - ideal_next_sec);
            // Downbeats get preference (bonus factor 0.7x diff)
            if (beat.is_downbeat) {
                diff *= 0.7;
            }

            if (diff < best_diff) {
                best_diff = diff;
                best_idx = b;
            }
        }

        if (best_diff < 1e8) {
            current_pts = rhythm.beats[best_idx].pts;
            cuts.push_back(current_pts);
        } else {
            // No beat in range, step by median
            current_pts = current_pts + Rational(static_cast<int64_t>(std::round(median_shot_sec * 1000.0)), 1000);
            cuts.push_back(current_pts);
        }
    }

    cuts.push_back(target_duration);
    return cuts;
}

EditProposalEnvelope DeterministicPlanner::plan(
    const std::string& project_id,
    const std::string& target_track_id,
    const std::vector<SourceClipCandidate>& candidates,
    const ReferenceFingerprint& reference,
    const AudioRhythmReport& music_rhythm,
    Rational target_duration
) const {
    EditProposalEnvelope envelope;
    envelope.schema_version = 1;
    envelope.proposal_id = "prop_" + UUID::generate();
    envelope.project_id = project_id;
    envelope.source = "ai";

    if (candidates.empty()) {
        envelope.warnings.push_back("No candidate clips provided for planning.");
        return envelope;
    }

    auto cut_grid = generate_beat_cut_grid(music_rhythm, reference, target_duration);
    if (cut_grid.size() < 2) {
        envelope.warnings.push_back("Timeline target duration is too short for cut grid.");
        return envelope;
    }

    std::vector<std::string> recently_used_clips;

    for (size_t i = 0; i < cut_grid.size() - 1; ++i) {
        Rational slot_in = cut_grid[i];
        Rational slot_out = cut_grid[i + 1];
        Rational slot_duration = slot_out - slot_in;

        // Find candidate with minimum composite cost
        size_t best_candidate_idx = 0;
        double min_cost = 1e9;

        for (size_t c = 0; c < candidates.size(); ++c) {
            const auto& cand = candidates[c];
            if (cand.duration < slot_duration) {
                continue; // Clip too short
            }

            double cost = 0.0;

            // 1. Motion energy match
            double motion_diff = std::abs(cand.motion_energy - reference.motion.mean_motion_energy);
            cost += m_config.motion_weight * motion_diff;

            // 2. Repeat penalty: avoid repeating clips used in last 2 cuts
            auto it = std::find(recently_used_clips.begin(), recently_used_clips.end(), cand.clip_id);
            if (it != recently_used_clips.end()) {
                cost += m_config.repeat_penalty_weight * 5.0; // High penalty
            }

            // 3. Speech penalty if montage
            if (cand.has_speech && reference.speech_density < 0.1) {
                cost += m_config.speech_boundary_weight * 2.0;
            }

            if (cost < min_cost) {
                min_cost = cost;
                best_candidate_idx = c;
            }
        }

        const auto& chosen = candidates[best_candidate_idx];
        recently_used_clips.push_back(chosen.clip_id);
        if (recently_used_clips.size() > 3) {
            recently_used_clips.erase(recently_used_clips.begin());
        }

        // Create typed EditOperation
        EditOperation op;
        op.operation_type = "insert_clip";
        op.target_ids = {target_track_id};
        op.parameters.clip_id = chosen.clip_id;
        op.parameters.asset_id = chosen.asset_id;
        op.parameters.clip_name = chosen.name;
        op.parameters.timeline_in = slot_in;
        op.parameters.duration = slot_duration;
        op.parameters.source_in = chosen.source_in;
        op.parameters.speed = Rational(1, 1);
        op.requires_approval = true;

        // Evidence
        op.evidence.justified_by = (i % 4 == 0) ? "downbeat" : "beat_grid";
        op.evidence.timestamp = slot_in;
        op.evidence.confidence = (std::max)(0.5, 1.0 - (min_cost / 10.0));
        op.evidence.model_version = "deterministic_v1.0.0";

        envelope.operations.push_back(std::move(op));
    }

    return envelope;
}

std::string EditProposalEnvelope::to_json() const {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"schema_version\": " << schema_version << ",\n";
    oss << "  \"proposal_id\": \"" << proposal_id << "\",\n";
    oss << "  \"project_id\": \"" << project_id << "\",\n";
    oss << "  \"source\": \"" << source << "\",\n";
    oss << "  \"operations\": [";

    for (size_t i = 0; i < operations.size(); ++i) {
        const auto& op = operations[i];
        if (i > 0) oss << ",";
        oss << "\n    {";
        oss << "\"operation_type\":\"" << op.operation_type << "\",";
        oss << "\"target_ids\":[";
        for (size_t t = 0; t < op.target_ids.size(); ++t) {
            if (t > 0) oss << ",";
            oss << "\"" << op.target_ids[t] << "\"";
        }
        oss << "],";
        oss << "\"parameters\":{";
        oss << "\"clip_id\":\"" << op.parameters.clip_id << "\",";
        oss << "\"asset_id\":\"" << op.parameters.asset_id << "\",";
        oss << "\"clip_name\":\"" << op.parameters.clip_name << "\",";
        oss << "\"timeline_in\":{\"num\":" << op.parameters.timeline_in.numerator() << ",\"den\":" << op.parameters.timeline_in.denominator() << "},";
        oss << "\"duration\":{\"num\":" << op.parameters.duration.numerator() << ",\"den\":" << op.parameters.duration.denominator() << "},";
        oss << "\"source_in\":{\"num\":" << op.parameters.source_in.numerator() << ",\"den\":" << op.parameters.source_in.denominator() << "}";
        oss << "},";
        oss << "\"requires_approval\":" << (op.requires_approval ? "true" : "false");
        oss << "}";
    }
    if (!operations.empty()) oss << "\n  ";
    oss << "],\n";

    oss << "  \"warnings\": [";
    for (size_t i = 0; i < warnings.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << warnings[i] << "\"";
    }
    oss << "]\n";
    oss << "}\n";
    return oss.str();
}

EditProposalEnvelope EditProposalEnvelope::from_json(const std::string& json_str) {
    EditProposalEnvelope env;
    // Extract proposal_id
    size_t p_pos = json_str.find("\"proposal_id\":");
    if (p_pos != std::string::npos) {
        size_t q1 = json_str.find('"', p_pos + 14);
        size_t q2 = json_str.find('"', q1 + 1);
        if (q1 != std::string::npos && q2 != std::string::npos) {
            env.proposal_id = json_str.substr(q1 + 1, q2 - q1 - 1);
        }
    }
    // Extract project_id
    size_t pr_pos = json_str.find("\"project_id\":");
    if (pr_pos != std::string::npos) {
        size_t q1 = json_str.find('"', pr_pos + 13);
        size_t q2 = json_str.find('"', q1 + 1);
        if (q1 != std::string::npos && q2 != std::string::npos) {
            env.project_id = json_str.substr(q1 + 1, q2 - q1 - 1);
        }
    }
    return env;
}

} // namespace ave
