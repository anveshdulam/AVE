#pragma once

#include <ave/time.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ave {

struct CutEvalResult {
    int true_positives = 0;
    int false_positives = 0;
    int false_negatives = 0;
    double precision = 0.0;
    double recall = 0.0;
    double f1_score = 0.0;
    double mean_frame_offset = 0.0;
};

struct TransitionEvalResult {
    double mean_iou = 0.0;
    double coverage = 0.0;
    double f1_score = 0.0;
};

struct BeatEvalResult {
    int true_positives = 0;
    int false_positives = 0;
    int false_negatives = 0;
    double precision = 0.0;
    double recall = 0.0;
    double f_measure = 0.0;
    double mean_offset_ms = 0.0;
    double tempo_accuracy_pct = 0.0;
};

/**
 * @brief Evaluation harness for perception benchmarks and synthetic ground truth.
 *
 * Implements standard benchmark metrics:
 * - Cut F1 score evaluated at +/- tolerance frames (typically +/- 2 frames).
 * - Gradual transition temporal Intersection-over-Union (IoU).
 * - Beat F-measure evaluated at +/- tolerance ms (typically +/- 70 ms per ISMIR standard).
 */
class SyntheticEvalHarness {
public:
    /**
     * @brief Evaluates cut detection against ground-truth cut positions.
     */
    [[nodiscard]] static CutEvalResult evaluate_cuts(
        const std::vector<Rational>& detected_cuts,
        const std::vector<Rational>& ground_truth_cuts,
        int tolerance_frames = 2,
        const Rational& frame_rate = Rational(25, 1)
    );

    /**
     * @brief Evaluates gradual transitions (dissolves, fades) using temporal IoU.
     */
    [[nodiscard]] static TransitionEvalResult evaluate_transitions(
        const std::vector<std::pair<Rational, Rational>>& detected_ranges,
        const std::vector<std::pair<Rational, Rational>>& ground_truth_ranges
    );

    /**
     * @brief Evaluates musical beat and downbeat tracking against ground-truth pulses.
     */
    [[nodiscard]] static BeatEvalResult evaluate_beats(
        const std::vector<Rational>& detected_beats,
        const std::vector<Rational>& ground_truth_beats,
        double tolerance_ms = 70.0,
        double detected_bpm = 0.0,
        double ground_truth_bpm = 0.0
    );

    /**
     * @brief Constructs FFmpeg command to generate synthetic video test footage with known cuts.
     */
    [[nodiscard]] static std::string generate_synthetic_video_ffmpeg_cmd(
        const std::filesystem::path& out_path,
        const std::vector<int>& cut_frame_indices,
        int fps = 25,
        int width = 480,
        int height = 270,
        const std::string& ffmpeg_bin = "ffmpeg"
    );

    /**
     * @brief Constructs FFmpeg command to generate synthetic audio click-track at known BPM.
     */
    [[nodiscard]] static std::string generate_synthetic_audio_ffmpeg_cmd(
        const std::filesystem::path& out_path,
        double bpm = 120.0,
        double duration_sec = 10.0,
        int sample_rate = 22050,
        const std::string& ffmpeg_bin = "ffmpeg"
    );
};

} // namespace ave
