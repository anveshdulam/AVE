#include <ave/shot_detector.hpp>

#include <algorithm>

namespace ave {

ShotDetector::ShotDetector(
    std::shared_ptr<IModelRunner> transnet_runner,
    ShotDetectorMode mode
)
    : m_transnet_runner(std::move(transnet_runner)),
      m_classical(),
      m_mode(mode) {
    if (m_transnet_runner && m_transnet_runner->is_loaded()) {
        m_transnet = std::make_unique<TransNetV2Detector>(m_transnet_runner);
    }
}

std::vector<ShotBoundary> ShotDetector::refine_cuts(
    const std::vector<ShotBoundary>& candidate_shots,
    const std::vector<VideoFrame>& frames,
    const Rational& frame_rate
) const {
    if (candidate_shots.size() <= 1 || frames.size() <= 1) {
        return candidate_shots;
    }

    std::vector<ShotBoundary> refined = candidate_shots;

    for (size_t s = 0; s < refined.size() - 1; ++s) {
        if (refined[s].type != ShotTransitionType::Cut) continue;

        Rational cut_pts = refined[s].end_pts;
        int64_t cut_frame = cut_pts.to_frames(frame_rate);

        // Search +/- 2 frames for highest classical gradient
        int64_t window_start = (std::max)(int64_t{1}, cut_frame - 2);
        int64_t window_end = (std::min)(static_cast<int64_t>(frames.size() - 1), cut_frame + 2);

        int64_t best_frame = cut_frame;
        double max_delta = -1.0;

        for (int64_t f = window_start; f <= window_end; ++f) {
            double d = m_classical.compute_frame_delta(frames[static_cast<size_t>(f - 1)],
                                                       frames[static_cast<size_t>(f)]);
            if (d > max_delta) {
                max_delta = d;
                best_frame = f;
            }
        }

        if (best_frame != cut_frame && max_delta > 0.3) {
            Rational refined_pts = frames[static_cast<size_t>(best_frame)].pts;
            refined[s].end_pts = refined_pts;
            refined[s + 1].start_pts = refined_pts;
        }
    }

    return refined;
}

std::vector<ShotBoundary> ShotDetector::process(
    const std::vector<VideoFrame>& frames,
    const Rational& frame_rate,
    PerceptionReport* out_report
) const {
    if (frames.empty()) return {};

    std::vector<std::pair<Rational, double>> raw_curve;
    std::vector<ShotBoundary> result_shots;
    std::string curve_name;

    bool use_transnet = (m_mode == ShotDetectorMode::TransNetV2Only ||
                        (m_mode == ShotDetectorMode::Auto && has_transnet()));

    if (use_transnet && m_transnet) {
        curve_name = "transnet_transition_prob";
        auto candidates = m_transnet->detect_shots(frames, frame_rate, &raw_curve);
        result_shots = refine_cuts(candidates, frames, frame_rate);
    } else {
        curve_name = "classical_frame_delta";
        result_shots = m_classical.detect_shots(frames, frame_rate, &raw_curve);
    }

    if (out_report) {
        out_report->shots = result_shots;
        if (!raw_curve.empty()) {
            out_report->add_raw_curve(curve_name, std::move(raw_curve));
        }
    }

    return result_shots;
}

} // namespace ave
