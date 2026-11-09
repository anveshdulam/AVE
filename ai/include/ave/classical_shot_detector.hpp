#pragma once

#include <ave/perception_report.hpp>
#include <ave/video_frame.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace ave {

struct ClassicalShotDetectorConfig {
    double cut_threshold = 0.50;           // Normalized delta threshold for hard cuts
    double transition_threshold = 0.25;    // Threshold for gradual transition ranges (dissolves)
    int min_transition_frames = 4;         // Minimum frames to qualify as a gradual transition
    int bins_per_channel = 16;             // Color histogram bins per RGB channel (16x16x16 = 4096 bins)
    double histogram_weight = 0.60;        // Weight for color histogram delta
    double edge_weight = 0.40;             // Weight for edge change ratio delta
};

/**
 * @brief Fast, deterministic shot boundary detector using color histograms and edge change ratio (ECR).
 *
 * Functions as both the primary lightweight baseline and the frame-accurate refinement stage
 * for deep learning detectors (TransNetV2).
 */
class ClassicalShotDetector {
public:
    explicit ClassicalShotDetector(ClassicalShotDetectorConfig config = {});

    [[nodiscard]] const ClassicalShotDetectorConfig& config() const noexcept { return m_config; }
    void set_config(ClassicalShotDetectorConfig config) noexcept { m_config = config; }

    /**
     * @brief Computes normalized Chi-Square histogram distance between two RGB/RGBA frames.
     * @return Distance in range [0.0, 1.0].
     */
    [[nodiscard]] double compute_histogram_distance(const VideoFrame& prev, const VideoFrame& curr) const;

    /**
     * @brief Computes Edge Change Ratio (ECR) between two frames using luminance Sobel gradients.
     * @return Ratio in range [0.0, 1.0].
     */
    [[nodiscard]] double compute_edge_change_ratio(const VideoFrame& prev, const VideoFrame& curr) const;

    /**
     * @brief Computes weighted combined delta between two consecutive frames.
     */
    [[nodiscard]] double compute_frame_delta(const VideoFrame& prev, const VideoFrame& curr) const;

    /**
     * @brief Detects shot boundaries across a sequential list of decoded video frames.
     *
     * @param frames Decoded video frames in monotonic PTS order.
     * @param frame_rate Timeline or stream frame rate.
     * @param out_raw_curve Optional pointer to store the continuous (pts, delta) probability curve.
     * @return List of detected shot boundaries with exact ave::Rational timestamps.
     */
    [[nodiscard]] std::vector<ShotBoundary> detect_shots(
        const std::vector<VideoFrame>& frames,
        const Rational& frame_rate,
        std::vector<std::pair<Rational, double>>* out_raw_curve = nullptr
    ) const;

private:
    ClassicalShotDetectorConfig m_config;

    void select_keyframes(ShotBoundary& shot) const;
};

} // namespace ave
