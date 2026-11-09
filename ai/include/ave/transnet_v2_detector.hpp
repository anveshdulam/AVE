#pragma once

#include <ave/model_runtime.hpp>
#include <ave/perception_report.hpp>
#include <ave/video_frame.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace ave {

struct TransNetV2DetectorConfig {
    float cut_threshold = 0.75f;           // Probability threshold for hard cuts
    float transition_threshold = 0.35f;    // Probability threshold for gradual transitions
    int min_transition_frames = 4;         // Minimum frames for dissolve range
    int window_size = 100;                 // TransNetV2 rolling window size
    int target_width = 48;                 // Standard TransNetV2 spatial input width
    int target_height = 27;                // Standard TransNetV2 spatial input height
};

/**
 * @brief TransNetV2 deep learning shot boundary detector.
 *
 * Evaluates continuous rolling 100-frame windows with overlap to predict:
 * - Frame transition probability curve [0.0, 1.0]
 * - Hard cut frame locations
 * - Gradual transition ranges [t_in, t_out]
 */
class TransNetV2Detector {
public:
    explicit TransNetV2Detector(std::shared_ptr<IModelRunner> runner, TransNetV2DetectorConfig config = {});

    [[nodiscard]] const TransNetV2DetectorConfig& config() const noexcept { return m_config; }
    void set_config(TransNetV2DetectorConfig config) noexcept { m_config = config; }

    /**
     * @brief Evaluates per-frame transition probabilities across the entire video.
     */
    [[nodiscard]] std::vector<float> compute_transition_probabilities(const std::vector<VideoFrame>& frames) const;

    /**
     * @brief Detects shot boundaries and transition ranges from video frames.
     */
    [[nodiscard]] std::vector<ShotBoundary> detect_shots(
        const std::vector<VideoFrame>& frames,
        const Rational& frame_rate,
        std::vector<std::pair<Rational, double>>* out_raw_curve = nullptr
    ) const;

private:
    std::shared_ptr<IModelRunner> m_runner;
    TransNetV2DetectorConfig m_config;

    void select_keyframes(ShotBoundary& shot) const;
    void resize_frame_to_tensor(const VideoFrame& frame, std::vector<float>& out_rgb) const;
};

} // namespace ave
