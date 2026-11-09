#include <ave/synthetic_eval_harness.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace ave {

CutEvalResult SyntheticEvalHarness::evaluate_cuts(
    const std::vector<Rational>& detected_cuts,
    const std::vector<Rational>& ground_truth_cuts,
    int tolerance_frames,
    const Rational& frame_rate
) {
    CutEvalResult res;
    if (detected_cuts.empty() && ground_truth_cuts.empty()) {
        res.precision = 1.0;
        res.recall = 1.0;
        res.f1_score = 1.0;
        return res;
    }

    std::vector<int64_t> det_frames;
    det_frames.reserve(detected_cuts.size());
    for (const auto& c : detected_cuts) {
        det_frames.push_back(c.to_frames(frame_rate));
    }
    std::sort(det_frames.begin(), det_frames.end());

    std::vector<int64_t> gt_frames;
    gt_frames.reserve(ground_truth_cuts.size());
    for (const auto& c : ground_truth_cuts) {
        gt_frames.push_back(c.to_frames(frame_rate));
    }
    std::sort(gt_frames.begin(), gt_frames.end());

    std::vector<bool> gt_matched(gt_frames.size(), false);
    double total_offset = 0.0;

    for (int64_t det : det_frames) {
        int best_idx = -1;
        int64_t min_diff = tolerance_frames + 1;

        for (size_t g = 0; g < gt_frames.size(); ++g) {
            if (gt_matched[g]) continue;
            int64_t diff = std::abs(det - gt_frames[g]);
            if (diff <= tolerance_frames && diff < min_diff) {
                min_diff = diff;
                best_idx = static_cast<int>(g);
            }
        }

        if (best_idx >= 0) {
            gt_matched[static_cast<size_t>(best_idx)] = true;
            res.true_positives++;
            total_offset += static_cast<double>(min_diff);
        } else {
            res.false_positives++;
        }
    }

    for (bool matched : gt_matched) {
        if (!matched) {
            res.false_negatives++;
        }
    }

    if (res.true_positives > 0) {
        res.mean_frame_offset = total_offset / static_cast<double>(res.true_positives);
    }

    int total_predicted = res.true_positives + res.false_positives;
    res.precision = (total_predicted > 0) ? (static_cast<double>(res.true_positives) / total_predicted) : 0.0;

    int total_actual = res.true_positives + res.false_negatives;
    res.recall = (total_actual > 0) ? (static_cast<double>(res.true_positives) / total_actual) : 0.0;

    if (res.precision + res.recall > 0.0) {
        res.f1_score = (2.0 * res.precision * res.recall) / (res.precision + res.recall);
    }

    return res;
}

TransitionEvalResult SyntheticEvalHarness::evaluate_transitions(
    const std::vector<std::pair<Rational, Rational>>& detected_ranges,
    const std::vector<std::pair<Rational, Rational>>& ground_truth_ranges
) {
    TransitionEvalResult res;
    if (detected_ranges.empty() && ground_truth_ranges.empty()) {
        res.mean_iou = 1.0;
        res.coverage = 1.0;
        res.f1_score = 1.0;
        return res;
    }
    if (detected_ranges.empty() || ground_truth_ranges.empty()) {
        return res;
    }

    double total_iou = 0.0;
    int matched_count = 0;

    for (const auto& det : detected_ranges) {
        double d_start = det.first.to_seconds();
        double d_end = det.second.to_seconds();

        double max_iou = 0.0;
        for (const auto& gt : ground_truth_ranges) {
            double g_start = gt.first.to_seconds();
            double g_end = gt.second.to_seconds();

            double inter_start = (std::max)(d_start, g_start);
            double inter_end = (std::min)(d_end, g_end);
            double inter = (std::max)(0.0, inter_end - inter_start);

            double union_start = (std::min)(d_start, g_start);
            double union_end = (std::max)(d_end, g_end);
            double u = union_end - union_start;

            if (u > 0.0) {
                double iou = inter / u;
                if (iou > max_iou) max_iou = iou;
            }
        }
        total_iou += max_iou;
        if (max_iou > 0.5) matched_count++;
    }

    res.mean_iou = total_iou / static_cast<double>(detected_ranges.size());
    res.coverage = static_cast<double>(matched_count) / static_cast<double>(ground_truth_ranges.size());
    if (res.mean_iou + res.coverage > 0.0) {
        res.f1_score = (2.0 * res.mean_iou * res.coverage) / (res.mean_iou + res.coverage);
    }
    return res;
}

BeatEvalResult SyntheticEvalHarness::evaluate_beats(
    const std::vector<Rational>& detected_beats,
    const std::vector<Rational>& ground_truth_beats,
    double tolerance_ms,
    double detected_bpm,
    double ground_truth_bpm
) {
    BeatEvalResult res;
    if (detected_beats.empty() && ground_truth_beats.empty()) {
        res.precision = 1.0;
        res.recall = 1.0;
        res.f_measure = 1.0;
        res.tempo_accuracy_pct = 100.0;
        return res;
    }

    std::vector<double> det_ms;
    det_ms.reserve(detected_beats.size());
    for (const auto& b : detected_beats) {
        det_ms.push_back(b.to_seconds() * 1000.0);
    }
    std::sort(det_ms.begin(), det_ms.end());

    std::vector<double> gt_ms;
    gt_ms.reserve(ground_truth_beats.size());
    for (const auto& b : ground_truth_beats) {
        gt_ms.push_back(b.to_seconds() * 1000.0);
    }
    std::sort(gt_ms.begin(), gt_ms.end());

    std::vector<bool> gt_matched(gt_ms.size(), false);
    double total_offset = 0.0;

    for (double det : det_ms) {
        int best_idx = -1;
        double min_diff = tolerance_ms + 1.0;

        for (size_t g = 0; g < gt_ms.size(); ++g) {
            if (gt_matched[g]) continue;
            double diff = std::abs(det - gt_ms[g]);
            if (diff <= tolerance_ms && diff < min_diff) {
                min_diff = diff;
                best_idx = static_cast<int>(g);
            }
        }

        if (best_idx >= 0) {
            gt_matched[static_cast<size_t>(best_idx)] = true;
            res.true_positives++;
            total_offset += min_diff;
        } else {
            res.false_positives++;
        }
    }

    for (bool matched : gt_matched) {
        if (!matched) res.false_negatives++;
    }

    if (res.true_positives > 0) {
        res.mean_offset_ms = total_offset / static_cast<double>(res.true_positives);
    }

    int total_pred = res.true_positives + res.false_positives;
    res.precision = (total_pred > 0) ? (static_cast<double>(res.true_positives) / total_pred) : 0.0;

    int total_actual = res.true_positives + res.false_negatives;
    res.recall = (total_actual > 0) ? (static_cast<double>(res.true_positives) / total_actual) : 0.0;

    if (res.precision + res.recall > 0.0) {
        res.f_measure = (2.0 * res.precision * res.recall) / (res.precision + res.recall);
    }

    if (ground_truth_bpm > 0.0) {
        double err = std::abs(detected_bpm - ground_truth_bpm) / ground_truth_bpm;
        res.tempo_accuracy_pct = (std::max)(0.0, (1.0 - err) * 100.0);
    }

    return res;
}

std::string SyntheticEvalHarness::generate_synthetic_video_ffmpeg_cmd(
    const std::filesystem::path& out_path,
    const std::vector<int>& cut_frame_indices,
    int fps,
    int width,
    int height,
    const std::string& ffmpeg_bin
) {
    (void)cut_frame_indices; // Reserved for complex multi-segment filters
    std::ostringstream oss;
    oss << "\"" << ffmpeg_bin << "\" -y -f lavfi -i testsrc=duration=5:size="
        << width << "x" << height << ":rate=" << fps
        << " -c:v libx264 -preset veryfast -pix_fmt yuv420p \""
        << out_path.string() << "\"";
    return oss.str();
}

std::string SyntheticEvalHarness::generate_synthetic_audio_ffmpeg_cmd(
    const std::filesystem::path& out_path,
    double bpm,
    double duration_sec,
    int sample_rate,
    const std::string& ffmpeg_bin
) {
    (void)bpm;
    std::ostringstream oss;
    oss << "\"" << ffmpeg_bin << "\" -y -f lavfi -i \"sine=frequency=1000:duration="
        << duration_sec << "\" -ar " << sample_rate << " -ac 1 \""
        << out_path.string() << "\"";
    return oss.str();
}

} // namespace ave
