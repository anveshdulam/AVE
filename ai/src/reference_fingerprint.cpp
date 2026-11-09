#include <ave/reference_fingerprint.hpp>
#include <ave/timeline.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace ave {

namespace {

double compute_percentile(const std::vector<double>& sorted_vals, double p) {
    if (sorted_vals.empty()) return 0.0;
    if (sorted_vals.size() == 1) return sorted_vals[0];

    double rank = p * static_cast<double>(sorted_vals.size() - 1);
    size_t low = static_cast<size_t>(std::floor(rank));
    size_t high = static_cast<size_t>(std::ceil(rank));
    double weight = rank - static_cast<double>(low);

    if (high >= sorted_vals.size()) high = sorted_vals.size() - 1;
    return (1.0 - weight) * sorted_vals[low] + weight * sorted_vals[high];
}

ShotLengthDistribution calculate_pacing(const std::vector<double>& durations) {
    ShotLengthDistribution dist;
    if (durations.empty()) return dist;

    std::vector<double> sorted = durations;
    std::sort(sorted.begin(), sorted.end());

    dist.median_sec = compute_percentile(sorted, 0.50);
    dist.p25_sec = compute_percentile(sorted, 0.25);
    dist.p75_sec = compute_percentile(sorted, 0.75);
    dist.iqr_sec = (std::max)(0.0, dist.p75_sec - dist.p25_sec);

    // Mean and Standard Deviation
    double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    double mean = sum / static_cast<double>(sorted.size());

    double sq_diff_sum = 0.0;
    for (double val : sorted) {
        double d = val - mean;
        sq_diff_sum += d * d;
    }
    double stddev = std::sqrt(sq_diff_sum / static_cast<double>(sorted.size()));

    // Burstiness B = (sigma - mu) / (sigma + mu) in [-1.0, 1.0]
    if (stddev + mean > 1e-6) {
        dist.burstiness = (stddev - mean) / (stddev + mean);
        dist.burstiness = (std::max)(-1.0, (std::min)(1.0, dist.burstiness));
    }

    return dist;
}

CutToBeatDistribution calculate_cut_to_beat(
    const std::vector<Rational>& cut_points,
    const AudioRhythmReport& rhythm
) {
    CutToBeatDistribution dist;
    if (cut_points.empty() || rhythm.beats.empty()) {
        return dist;
    }

    size_t beat_sync_count = 0;
    size_t downbeat_sync_count = 0;
    double offset_sum_ms = 0.0;
    size_t synced_count = 0;

    constexpr double TOLERANCE_MS = 70.0; // Standard +/-70ms beat matching window

    for (const auto& cut : cut_points) {
        double cut_sec = cut.to_seconds();
        double min_dist_sec = 1e9;
        double closest_offset_ms = 0.0;
        bool closest_is_downbeat = false;

        for (const auto& beat : rhythm.beats) {
            double beat_sec = beat.pts.to_seconds();
            double offset_ms = (cut_sec - beat_sec) * 1000.0;
            if (std::abs(offset_ms) < min_dist_sec) {
                min_dist_sec = std::abs(offset_ms);
                closest_offset_ms = offset_ms;
                closest_is_downbeat = beat.is_downbeat;
            }
        }

        if (min_dist_sec <= TOLERANCE_MS) {
            beat_sync_count++;
            if (closest_is_downbeat) {
                downbeat_sync_count++;
            }
            offset_sum_ms += closest_offset_ms;
            synced_count++;
        }
    }

    double total_cuts = static_cast<double>(cut_points.size());
    dist.beat_sync_ratio = static_cast<double>(beat_sync_count) / total_cuts;
    dist.downbeat_sync_ratio = static_cast<double>(downbeat_sync_count) / total_cuts;
    dist.offbeat_ratio = (std::max)(0.0, 1.0 - dist.beat_sync_ratio);
    dist.mean_offset_ms = (synced_count > 0) ? (offset_sum_ms / static_cast<double>(synced_count)) : 0.0;

    return dist;
}

} // namespace

ReferenceFingerprint FingerprintEngine::from_perception_report(const PerceptionReport& report) {
    ReferenceFingerprint fp;

    // 1. Pacing & Shot lengths
    std::vector<double> durations;
    std::vector<Rational> cuts;
    size_t hard_cuts = 0;
    size_t dissolves = 0;
    size_t fades = 0;

    for (size_t i = 0; i < report.shots.size(); ++i) {
        const auto& s = report.shots[i];
        durations.push_back(s.duration().to_seconds());
        if (i > 0) {
            cuts.push_back(s.start_pts);
        }

        if (s.type == ShotTransitionType::Dissolve) {
            dissolves++;
        } else if (s.type == ShotTransitionType::FadeIn || s.type == ShotTransitionType::FadeOut) {
            fades++;
        } else {
            hard_cuts++;
        }
    }

    fp.pacing = calculate_pacing(durations);

    // 2. Cut to Beat
    fp.rhythm = calculate_cut_to_beat(cuts, report.rhythm);

    // 3. Transitions
    size_t total_transitions = hard_cuts + dissolves + fades;
    if (total_transitions > 0) {
        fp.transitions.hard_cut_ratio = static_cast<double>(hard_cuts) / static_cast<double>(total_transitions);
        fp.transitions.dissolve_ratio = static_cast<double>(dissolves) / static_cast<double>(total_transitions);
        fp.transitions.fade_ratio = static_cast<double>(fades) / static_cast<double>(total_transitions);
    }

    // 4. Motion
    fp.motion.mean_motion_energy = report.visual.average_motion_energy;
    if (!report.visual.motion_curve.empty()) {
        double sq_sum = 0.0;
        for (const auto& pt : report.visual.motion_curve) {
            double d = pt.second - fp.motion.mean_motion_energy;
            sq_sum += d * d;
        }
        fp.motion.motion_variance = sq_sum / static_cast<double>(report.visual.motion_curve.size());
    }

    // 5. Color, Loudness, Tempo, Speech
    fp.color = report.visual.color_look;
    fp.integrated_loudness_lufs = report.rhythm.integrated_loudness_lufs;
    fp.tempo_bpm = report.rhythm.primary_tempo_bpm;

    // Speech density
    if (report.duration.to_seconds() > 0.0) {
        double speech_sec = 0.0;
        for (const auto& seg : report.speech.segments) {
            speech_sec += (seg.end_pts - seg.start_pts).to_seconds();
        }
        fp.speech_density = (std::min)(1.0, speech_sec / report.duration.to_seconds());
    }

    return fp;
}

ReferenceFingerprint FingerprintEngine::from_timeline(
    const Timeline& timeline,
    const AudioRhythmReport& rhythm,
    double motion_energy,
    ColorLook color_look
) {
    ReferenceFingerprint fp;

    // Extract item boundaries from the primary video track (V1)
    std::vector<double> durations;
    std::vector<Rational> cuts;

    for (const auto& track : timeline.video_tracks()) {
        if (!track.items().empty()) {
            for (size_t i = 0; i < track.items().size(); ++i) {
                const auto& item = track.items()[i];
                durations.push_back(item.duration().to_seconds());
                if (i > 0) {
                    cuts.push_back(item.timeline_in());
                }
            }
            break; // Use top primary video track for pacing
        }
    }

    fp.pacing = calculate_pacing(durations);
    fp.rhythm = calculate_cut_to_beat(cuts, rhythm);
    fp.transitions.hard_cut_ratio = 1.0; // Timeline cuts default to hard cuts
    fp.motion.mean_motion_energy = motion_energy;
    fp.color = color_look;
    fp.integrated_loudness_lufs = rhythm.integrated_loudness_lufs;
    fp.tempo_bpm = rhythm.primary_tempo_bpm;

    return fp;
}

FingerprintDistanceBreakdown FingerprintEngine::compute_distance_detailed(
    const ReferenceFingerprint& a,
    const ReferenceFingerprint& b,
    FingerprintDistanceWeights weights
) {
    FingerprintDistanceBreakdown bd;

    // 1. Pacing distance
    double med_max = (std::max)({a.pacing.median_sec, b.pacing.median_sec, 0.1});
    double iqr_max = (std::max)({a.pacing.iqr_sec, b.pacing.iqr_sec, 0.1});
    double d_med = std::abs(a.pacing.median_sec - b.pacing.median_sec) / med_max;
    double d_iqr = std::abs(a.pacing.iqr_sec - b.pacing.iqr_sec) / iqr_max;
    double d_burst = std::abs(a.pacing.burstiness - b.pacing.burstiness) / 2.0;
    bd.pacing_dist = (std::min)(1.0, 0.5 * d_med + 0.3 * d_iqr + 0.2 * d_burst);

    // 2. Rhythm / Beat-sync distance
    double d_sync = std::abs(a.rhythm.beat_sync_ratio - b.rhythm.beat_sync_ratio);
    double d_down = std::abs(a.rhythm.downbeat_sync_ratio - b.rhythm.downbeat_sync_ratio);
    double d_off = (std::min)(1.0, std::abs(a.rhythm.mean_offset_ms - b.rhythm.mean_offset_ms) / 70.0);
    bd.rhythm_dist = (std::min)(1.0, 0.5 * d_sync + 0.3 * d_down + 0.2 * d_off);

    // 3. Transition mix distance (L1 norm / 2)
    double d_h = std::abs(a.transitions.hard_cut_ratio - b.transitions.hard_cut_ratio);
    double d_d = std::abs(a.transitions.dissolve_ratio - b.transitions.dissolve_ratio);
    double d_f = std::abs(a.transitions.fade_ratio - b.transitions.fade_ratio);
    bd.transition_dist = (std::min)(1.0, 0.5 * (d_h + d_d + d_f));

    // 4. Motion distance
    bd.motion_dist = (std::min)(1.0, std::abs(a.motion.mean_motion_energy - b.motion.mean_motion_energy) * 2.0);

    // 5. Color look distance (Normalized Delta E)
    double d_l = (a.color.lab_l - b.color.lab_l) / 100.0;
    double d_a = (a.color.lab_a - b.color.lab_a) / 100.0;
    double d_b = (a.color.lab_b - b.color.lab_b) / 100.0;
    double delta_e = std::sqrt(d_l * d_l + d_a * d_a + d_b * d_b);
    bd.color_dist = (std::min)(1.0, delta_e);

    // Weighted sum
    double total_weight = weights.pacing_weight + weights.rhythm_weight +
                          weights.transition_weight + weights.motion_weight +
                          weights.color_weight;
    if (total_weight < 1e-6) total_weight = 1.0;

    double weighted_sum = weights.pacing_weight * bd.pacing_dist +
                          weights.rhythm_weight * bd.rhythm_dist +
                          weights.transition_weight * bd.transition_dist +
                          weights.motion_weight * bd.motion_dist +
                          weights.color_weight * bd.color_dist;

    bd.total_distance = (std::min)(1.0, (std::max)(0.0, weighted_sum / total_weight));
    return bd;
}

double FingerprintEngine::compute_distance(
    const ReferenceFingerprint& a,
    const ReferenceFingerprint& b,
    FingerprintDistanceWeights weights
) {
    return compute_distance_detailed(a, b, weights).total_distance;
}

std::string ReferenceFingerprint::to_json() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    oss << "{\n";
    oss << "  \"pacing\": {\n";
    oss << "    \"median_sec\": " << pacing.median_sec << ",\n";
    oss << "    \"p25_sec\": " << pacing.p25_sec << ",\n";
    oss << "    \"p75_sec\": " << pacing.p75_sec << ",\n";
    oss << "    \"iqr_sec\": " << pacing.iqr_sec << ",\n";
    oss << "    \"burstiness\": " << pacing.burstiness << "\n";
    oss << "  },\n";
    oss << "  \"rhythm\": {\n";
    oss << "    \"beat_sync_ratio\": " << rhythm.beat_sync_ratio << ",\n";
    oss << "    \"downbeat_sync_ratio\": " << rhythm.downbeat_sync_ratio << ",\n";
    oss << "    \"offbeat_ratio\": " << rhythm.offbeat_ratio << ",\n";
    oss << "    \"mean_offset_ms\": " << rhythm.mean_offset_ms << "\n";
    oss << "  },\n";
    oss << "  \"transitions\": {\n";
    oss << "    \"hard_cut_ratio\": " << transitions.hard_cut_ratio << ",\n";
    oss << "    \"dissolve_ratio\": " << transitions.dissolve_ratio << ",\n";
    oss << "    \"fade_ratio\": " << transitions.fade_ratio << "\n";
    oss << "  },\n";
    oss << "  \"motion\": {\n";
    oss << "    \"mean_motion_energy\": " << motion.mean_motion_energy << ",\n";
    oss << "    \"motion_variance\": " << motion.motion_variance << "\n";
    oss << "  },\n";
    oss << "  \"color\": {\n";
    oss << "    \"lab_l\": " << color.lab_l << ",\n";
    oss << "    \"lab_a\": " << color.lab_a << ",\n";
    oss << "    \"lab_b\": " << color.lab_b << ",\n";
    oss << "    \"contrast\": " << color.contrast << "\n";
    oss << "  },\n";
    oss << "  \"integrated_loudness_lufs\": " << integrated_loudness_lufs << ",\n";
    oss << "  \"tempo_bpm\": " << tempo_bpm << ",\n";
    oss << "  \"speech_density\": " << speech_density << "\n";
    oss << "}\n";
    return oss.str();
}

ReferenceFingerprint ReferenceFingerprint::from_json(const std::string& json_str) {
    ReferenceFingerprint fp;
    auto extract_val = [&](const std::string& key, double default_val) -> double {
        std::string needle = "\"" + key + "\":";
        size_t pos = json_str.find(needle);
        if (pos == std::string::npos) {
            needle = "\"" + key + "\" :";
            pos = json_str.find(needle);
            if (pos == std::string::npos) return default_val;
        }
        pos += needle.length();
        while (pos < json_str.length() && (json_str[pos] == ' ' || json_str[pos] == '\t' || json_str[pos] == '\n' || json_str[pos] == '\r')) {
            pos++;
        }
        size_t end_pos = pos;
        if (json_str[end_pos] == '-' || json_str[end_pos] == '+') end_pos++;
        while (end_pos < json_str.length() && (std::isdigit(static_cast<unsigned char>(json_str[end_pos])) || json_str[end_pos] == '.' || json_str[end_pos] == 'e' || json_str[end_pos] == 'E' || json_str[end_pos] == '+' || json_str[end_pos] == '-')) {
            end_pos++;
        }
        try {
            return std::stod(json_str.substr(pos, end_pos - pos));
        } catch (...) {
            return default_val;
        }
    };

    fp.pacing.median_sec = extract_val("median_sec", 0.0);
    fp.pacing.p25_sec = extract_val("p25_sec", 0.0);
    fp.pacing.p75_sec = extract_val("p75_sec", 0.0);
    fp.pacing.iqr_sec = extract_val("iqr_sec", 0.0);
    fp.pacing.burstiness = extract_val("burstiness", 0.0);

    fp.rhythm.beat_sync_ratio = extract_val("beat_sync_ratio", 0.0);
    fp.rhythm.downbeat_sync_ratio = extract_val("downbeat_sync_ratio", 0.0);
    fp.rhythm.offbeat_ratio = extract_val("offbeat_ratio", 1.0);
    fp.rhythm.mean_offset_ms = extract_val("mean_offset_ms", 0.0);

    fp.transitions.hard_cut_ratio = extract_val("hard_cut_ratio", 1.0);
    fp.transitions.dissolve_ratio = extract_val("dissolve_ratio", 0.0);
    fp.transitions.fade_ratio = extract_val("fade_ratio", 0.0);

    fp.motion.mean_motion_energy = extract_val("mean_motion_energy", 0.0);
    fp.motion.motion_variance = extract_val("motion_variance", 0.0);

    fp.color.lab_l = extract_val("lab_l", 50.0);
    fp.color.lab_a = extract_val("lab_a", 0.0);
    fp.color.lab_b = extract_val("lab_b", 0.0);
    fp.color.contrast = extract_val("contrast", 1.0);

    fp.integrated_loudness_lufs = extract_val("integrated_loudness_lufs", -14.0);
    fp.tempo_bpm = extract_val("tempo_bpm", 120.0);
    fp.speech_density = extract_val("speech_density", 0.0);

    return fp;
}

} // namespace ave
