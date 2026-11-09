#include <ave/transnet_v2_detector.hpp>

#include <algorithm>
#include <cmath>

namespace ave {

TransNetV2Detector::TransNetV2Detector(std::shared_ptr<IModelRunner> runner, TransNetV2DetectorConfig config)
    : m_runner(std::move(runner)),
      m_config(config) {}

void TransNetV2Detector::resize_frame_to_tensor(const VideoFrame& frame, std::vector<float>& out_rgb) const {
    int tw = m_config.target_width;
    int th = m_config.target_height;
    out_rgb.reserve(out_rgb.size() + static_cast<size_t>(tw * th * 3));

    if (frame.width <= 0 || frame.height <= 0 || frame.rgba_data.empty()) {
        out_rgb.resize(out_rgb.size() + static_cast<size_t>(tw * th * 3), 0.0f);
        return;
    }

    for (int y = 0; y < th; ++y) {
        int src_y = (y * frame.height) / th;
        for (int x = 0; x < tw; ++x) {
            int src_x = (x * frame.width) / tw;
            size_t idx = static_cast<size_t>(src_y * frame.width + src_x) * 4;
            if (idx + 2 < frame.rgba_data.size()) {
                out_rgb.push_back(static_cast<float>(frame.rgba_data[idx]) / 255.0f);
                out_rgb.push_back(static_cast<float>(frame.rgba_data[idx + 1]) / 255.0f);
                out_rgb.push_back(static_cast<float>(frame.rgba_data[idx + 2]) / 255.0f);
            } else {
                out_rgb.push_back(0.0f);
                out_rgb.push_back(0.0f);
                out_rgb.push_back(0.0f);
            }
        }
    }
}

void TransNetV2Detector::select_keyframes(ShotBoundary& shot) const {
    shot.keyframes.clear();
    Rational dur = shot.duration();
    double dur_sec = dur.to_seconds();

    if (dur_sec < 2.0) {
        shot.keyframes.push_back(shot.start_pts + Rational(dur.numerator(), dur.denominator() * 2));
    } else {
        shot.keyframes.push_back(shot.start_pts + Rational(dur.numerator() * 2, dur.denominator() * 10));
        shot.keyframes.push_back(shot.start_pts + Rational(dur.numerator() * 5, dur.denominator() * 10));
        shot.keyframes.push_back(shot.start_pts + Rational(dur.numerator() * 8, dur.denominator() * 10));
    }
}

std::vector<float> TransNetV2Detector::compute_transition_probabilities(const std::vector<VideoFrame>& frames) const {
    if (frames.empty() || !m_runner || !m_runner->is_loaded()) return {};

    const size_t n_frames = frames.size();
    std::vector<float> final_probs(n_frames, 0.0f);

    const int win_size = m_config.window_size;
    const int frame_pixels = m_config.target_width * m_config.target_height * 3;

    // Pre-resize all frames
    std::vector<std::vector<float>> resized_frames;
    resized_frames.reserve(n_frames);
    for (const auto& f : frames) {
        std::vector<float> rgb;
        resize_frame_to_tensor(f, rgb);
        resized_frames.push_back(std::move(rgb));
    }

    // Process windows with 50-frame overlap stride
    const size_t stride = 50;
    for (size_t start_idx = 0; start_idx < n_frames; start_idx += stride) {
        std::vector<float> input_window;
        input_window.reserve(static_cast<size_t>(win_size * frame_pixels));

        for (int w = 0; w < win_size; ++w) {
            size_t src_idx = (std::min)(start_idx + static_cast<size_t>(w), n_frames - 1);
            input_window.insert(input_window.end(), resized_frames[src_idx].begin(), resized_frames[src_idx].end());
        }

        std::vector<float> window_out;
        if (m_runner->run(input_window, window_out) && window_out.size() >= static_cast<size_t>(win_size)) {
            for (int w = 0; w < win_size; ++w) {
                size_t actual_idx = start_idx + static_cast<size_t>(w);
                if (actual_idx < n_frames) {
                    final_probs[actual_idx] = (std::max)(final_probs[actual_idx], window_out[static_cast<size_t>(w)]);
                }
            }
        }

        if (start_idx + static_cast<size_t>(win_size) >= n_frames) break;
    }

    return final_probs;
}

std::vector<ShotBoundary> TransNetV2Detector::detect_shots(
    const std::vector<VideoFrame>& frames,
    const Rational& frame_rate,
    std::vector<std::pair<Rational, double>>* out_raw_curve
) const {
    (void)frame_rate;
    if (frames.empty()) return {};

    if (frames.size() == 1) {
        ShotBoundary s;
        s.start_pts = frames[0].pts;
        s.end_pts = frames[0].pts_end();
        s.type = ShotTransitionType::Cut;
        s.confidence = 1.0;
        select_keyframes(s);
        return {s};
    }

    std::vector<float> probs = compute_transition_probabilities(frames);

    if (out_raw_curve) {
        out_raw_curve->clear();
        for (size_t i = 0; i < probs.size(); ++i) {
            out_raw_curve->push_back({frames[i].pts, static_cast<double>(probs[i])});
        }
    }

    std::vector<ShotBoundary> shots;
    Rational current_start = frames[0].pts;
    size_t in_trans_start = 0;
    bool in_trans = false;

    for (size_t i = 1; i < probs.size(); ++i) {
        float p = probs[i];

        if (p >= m_config.cut_threshold) {
            // Hard Cut
            ShotBoundary cut_shot;
            cut_shot.start_pts = current_start;
            cut_shot.end_pts = frames[i].pts;
            cut_shot.type = ShotTransitionType::Cut;
            cut_shot.confidence = static_cast<double>(p);
            select_keyframes(cut_shot);
            shots.push_back(cut_shot);

            current_start = frames[i].pts;
            in_trans = false;
        } else if (p >= m_config.transition_threshold) {
            if (!in_trans) {
                in_trans = true;
                in_trans_start = i;
            }
        } else {
            if (in_trans) {
                int len = static_cast<int>(i - in_trans_start);
                if (len >= m_config.min_transition_frames) {
                    // Gradual Transition (Dissolve / Fade)
                    ShotBoundary dis_shot;
                    dis_shot.start_pts = current_start;
                    dis_shot.end_pts = frames[i].pts;
                    dis_shot.type = ShotTransitionType::Dissolve;
                    dis_shot.confidence = 0.88;
                    select_keyframes(dis_shot);
                    shots.push_back(dis_shot);

                    current_start = frames[i].pts;
                }
                in_trans = false;
            }
        }
    }

    // Trailing shot
    ShotBoundary final_shot;
    final_shot.start_pts = current_start;
    final_shot.end_pts = frames.back().pts_end();
    final_shot.type = ShotTransitionType::Cut;
    final_shot.confidence = 1.0;
    select_keyframes(final_shot);
    shots.push_back(final_shot);

    return shots;
}

} // namespace ave
