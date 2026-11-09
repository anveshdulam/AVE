#pragma once

#include <ave/classical_shot_detector.hpp>
#include <ave/perception_report.hpp>
#include <ave/transnet_v2_detector.hpp>
#include <ave/video_frame.hpp>

#include <memory>
#include <vector>

namespace ave {

enum class ShotDetectorMode {
    Auto,           // Prefers TransNetV2 with classical refinement; falls back to Classical if runner unavailable
    ClassicalOnly,  // Uses color histograms and edge change ratio only
    TransNetV2Only  // Uses TransNetV2 rolling window only
};

/**
 * @brief Unified Shot Detector fusing deep learning (TransNetV2) with classical frame refinement.
 *
 * Implements the core architecture rule:
 * TransNetV2 predicts transition probabilities from rolling 100-frame windows,
 * while the color histogram and edge detector stage refines exact cut boundaries
 * to single-frame precision.
 */
class ShotDetector {
public:
    explicit ShotDetector(
        std::shared_ptr<IModelRunner> transnet_runner = nullptr,
        ShotDetectorMode mode = ShotDetectorMode::Auto
    );

    [[nodiscard]] ShotDetectorMode mode() const noexcept { return m_mode; }
    void set_mode(ShotDetectorMode mode) noexcept { m_mode = mode; }

    [[nodiscard]] bool has_transnet() const noexcept {
        return m_transnet != nullptr && m_transnet_runner != nullptr && m_transnet_runner->is_loaded();
    }

    /**
     * @brief Performs shot detection across a sequential series of video frames.
     *
     * @param frames Decoded video frames.
     * @param frame_rate Timeline or stream frame rate.
     * @param out_report Optional PerceptionReport to append shots and raw curves to.
     * @return List of detected shot boundaries with exact ave::Rational timestamps.
     */
    [[nodiscard]] std::vector<ShotBoundary> process(
        const std::vector<VideoFrame>& frames,
        const Rational& frame_rate,
        PerceptionReport* out_report = nullptr
    ) const;

    [[nodiscard]] ClassicalShotDetector& classical() noexcept { return m_classical; }
    [[nodiscard]] const ClassicalShotDetector& classical() const noexcept { return m_classical; }

private:
    std::shared_ptr<IModelRunner> m_transnet_runner;
    std::unique_ptr<TransNetV2Detector> m_transnet;
    ClassicalShotDetector m_classical;
    ShotDetectorMode m_mode;

    [[nodiscard]] std::vector<ShotBoundary> refine_cuts(
        const std::vector<ShotBoundary>& candidate_shots,
        const std::vector<VideoFrame>& frames,
        const Rational& frame_rate
    ) const;
};

} // namespace ave
