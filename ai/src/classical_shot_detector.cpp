#include <ave/classical_shot_detector.hpp>

#include <algorithm>
#include <cmath>

namespace ave {

namespace {

inline uint8_t luminance(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint8_t>((299u * r + 587u * g + 114u * b) / 1000u);
}

} // namespace

ClassicalShotDetector::ClassicalShotDetector(ClassicalShotDetectorConfig config)
    : m_config(config) {}

double ClassicalShotDetector::compute_histogram_distance(const VideoFrame& prev, const VideoFrame& curr) const {
    if (prev.rgba_data.empty() || curr.rgba_data.empty()) return 0.0;
    if (prev.width <= 0 || prev.height <= 0 || curr.width <= 0 || curr.height <= 0) return 0.0;

    const size_t total_bins = 16 * 16 * 16;
    std::vector<double> h_prev(total_bins, 0.0);
    std::vector<double> h_curr(total_bins, 0.0);

    size_t prev_pixels = static_cast<size_t>(prev.width) * static_cast<size_t>(prev.height);
    size_t curr_pixels = static_cast<size_t>(curr.width) * static_cast<size_t>(curr.height);

    for (size_t i = 0; i < prev_pixels; ++i) {
        size_t idx = i * 4;
        if (idx + 2 >= prev.rgba_data.size()) break;
        uint32_t r = prev.rgba_data[idx] >> 4;
        uint32_t g = prev.rgba_data[idx + 1] >> 4;
        uint32_t b = prev.rgba_data[idx + 2] >> 4;
        h_prev[(r << 8) | (g << 4) | b] += 1.0;
    }

    for (size_t i = 0; i < curr_pixels; ++i) {
        size_t idx = i * 4;
        if (idx + 2 >= curr.rgba_data.size()) break;
        uint32_t r = curr.rgba_data[idx] >> 4;
        uint32_t g = curr.rgba_data[idx + 1] >> 4;
        uint32_t b = curr.rgba_data[idx + 2] >> 4;
        h_curr[(r << 8) | (g << 4) | b] += 1.0;
    }

    double inv_prev = prev_pixels > 0 ? (1.0 / static_cast<double>(prev_pixels)) : 0.0;
    double inv_curr = curr_pixels > 0 ? (1.0 / static_cast<double>(curr_pixels)) : 0.0;

    double chi_sq = 0.0;
    for (size_t i = 0; i < total_bins; ++i) {
        double p1 = h_prev[i] * inv_prev;
        double p2 = h_curr[i] * inv_curr;
        double sum = p1 + p2;
        if (sum > 1e-9) {
            double diff = p1 - p2;
            chi_sq += (diff * diff) / sum;
        }
    }

    return (std::min)(1.0, chi_sq * 0.5);
}

double ClassicalShotDetector::compute_edge_change_ratio(const VideoFrame& prev, const VideoFrame& curr) const {
    if (prev.rgba_data.empty() || curr.rgba_data.empty()) return 0.0;
    int w = prev.width;
    int h = prev.height;
    if (w < 3 || h < 3 || curr.width != w || curr.height != h) return 0.0;

    int edge_prev = 0;
    int edge_curr = 0;
    int entering_edges = 0;
    int exiting_edges = 0;

    const int edge_threshold = 40;

    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            size_t idx_l = static_cast<size_t>(y * w + (x - 1)) * 4;
            size_t idx_r = static_cast<size_t>(y * w + (x + 1)) * 4;
            size_t idx_u = static_cast<size_t>((y - 1) * w + x) * 4;
            size_t idx_d = static_cast<size_t>((y + 1) * w + x) * 4;

            // Prev luminance gradients
            uint8_t y_pl = luminance(prev.rgba_data[idx_l], prev.rgba_data[idx_l + 1], prev.rgba_data[idx_l + 2]);
            uint8_t y_pr = luminance(prev.rgba_data[idx_r], prev.rgba_data[idx_r + 1], prev.rgba_data[idx_r + 2]);
            uint8_t y_pu = luminance(prev.rgba_data[idx_u], prev.rgba_data[idx_u + 1], prev.rgba_data[idx_u + 2]);
            uint8_t y_pd = luminance(prev.rgba_data[idx_d], prev.rgba_data[idx_d + 1], prev.rgba_data[idx_d + 2]);
            int grad_prev = std::abs(static_cast<int>(y_pr) - static_cast<int>(y_pl)) +
                            std::abs(static_cast<int>(y_pd) - static_cast<int>(y_pu));
            bool is_prev_edge = grad_prev > edge_threshold;

            // Curr luminance gradients
            uint8_t y_cl = luminance(curr.rgba_data[idx_l], curr.rgba_data[idx_l + 1], curr.rgba_data[idx_l + 2]);
            uint8_t y_cr = luminance(curr.rgba_data[idx_r], curr.rgba_data[idx_r + 1], curr.rgba_data[idx_r + 2]);
            uint8_t y_cu = luminance(curr.rgba_data[idx_u], curr.rgba_data[idx_u + 1], curr.rgba_data[idx_u + 2]);
            uint8_t y_cd = luminance(curr.rgba_data[idx_d], curr.rgba_data[idx_d + 1], curr.rgba_data[idx_d + 2]);
            int grad_curr = std::abs(static_cast<int>(y_cr) - static_cast<int>(y_cl)) +
                            std::abs(static_cast<int>(y_cd) - static_cast<int>(y_cu));
            bool is_curr_edge = grad_curr > edge_threshold;

            if (is_prev_edge) edge_prev++;
            if (is_curr_edge) edge_curr++;
            if (is_curr_edge && !is_prev_edge) entering_edges++;
            if (is_prev_edge && !is_curr_edge) exiting_edges++;
        }
    }

    int max_edges = (std::max)(edge_prev, edge_curr);
    if (max_edges == 0) return 0.0;

    double r_in = static_cast<double>(entering_edges) / static_cast<double>(max_edges);
    double r_out = static_cast<double>(exiting_edges) / static_cast<double>(max_edges);
    return (std::min)(1.0, (std::max)(r_in, r_out));
}

double ClassicalShotDetector::compute_frame_delta(const VideoFrame& prev, const VideoFrame& curr) const {
    double hist = compute_histogram_distance(prev, curr);
    double ecr = compute_edge_change_ratio(prev, curr);
    return (std::min)(1.0, hist * m_config.histogram_weight + ecr * m_config.edge_weight);
}

void ClassicalShotDetector::select_keyframes(ShotBoundary& shot) const {
    shot.keyframes.clear();
    Rational dur = shot.duration();
    double dur_sec = dur.to_seconds();

    if (dur_sec < 2.0) {
        // Short shot: single midpoint keyframe
        shot.keyframes.push_back(shot.start_pts + Rational(dur.numerator(), dur.denominator() * 2));
    } else {
        // Multi-second shot: 3 keyframes at 20%, 50%, 80%
        shot.keyframes.push_back(shot.start_pts + Rational(dur.numerator() * 2, dur.denominator() * 10));
        shot.keyframes.push_back(shot.start_pts + Rational(dur.numerator() * 5, dur.denominator() * 10));
        shot.keyframes.push_back(shot.start_pts + Rational(dur.numerator() * 8, dur.denominator() * 10));
    }
}

std::vector<ShotBoundary> ClassicalShotDetector::detect_shots(
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

    std::vector<double> deltas;
    deltas.reserve(frames.size());
    deltas.push_back(0.0); // Frame 0 delta is 0

    if (out_raw_curve) {
        out_raw_curve->clear();
        out_raw_curve->push_back({frames[0].pts, 0.0});
    }

    for (size_t i = 1; i < frames.size(); ++i) {
        double d = compute_frame_delta(frames[i - 1], frames[i]);
        deltas.push_back(d);
        if (out_raw_curve) {
            out_raw_curve->push_back({frames[i].pts, d});
        }
    }

    std::vector<ShotBoundary> shots;
    Rational current_shot_start = frames[0].pts;
    size_t in_transition_start = 0;
    bool in_transition = false;

    for (size_t i = 1; i < frames.size(); ++i) {
        double d = deltas[i];

        if (d >= m_config.cut_threshold) {
            // Hard Cut detected
            ShotBoundary cut_shot;
            cut_shot.start_pts = current_shot_start;
            cut_shot.end_pts = frames[i].pts;
            cut_shot.type = ShotTransitionType::Cut;
            cut_shot.confidence = (std::min)(1.0, 0.5 + d * 0.5);
            select_keyframes(cut_shot);
            shots.push_back(cut_shot);

            current_shot_start = frames[i].pts;
            in_transition = false;
        } else if (d >= m_config.transition_threshold) {
            if (!in_transition) {
                in_transition = true;
                in_transition_start = i;
            }
        } else {
            if (in_transition) {
                int trans_len = static_cast<int>(i - in_transition_start);
                if (trans_len >= m_config.min_transition_frames) {
                    // Gradual Transition (Dissolve / Cross-fade)
                    ShotBoundary dissolve_shot;
                    dissolve_shot.start_pts = current_shot_start;
                    dissolve_shot.end_pts = frames[i].pts;
                    dissolve_shot.type = ShotTransitionType::Dissolve;
                    dissolve_shot.confidence = 0.85;
                    select_keyframes(dissolve_shot);
                    shots.push_back(dissolve_shot);

                    current_shot_start = frames[i].pts;
                }
                in_transition = false;
            }
        }
    }

    // Final shot to end of stream
    ShotBoundary final_shot;
    final_shot.start_pts = current_shot_start;
    final_shot.end_pts = frames.back().pts_end();
    final_shot.type = ShotTransitionType::Cut;
    final_shot.confidence = 1.0;
    select_keyframes(final_shot);
    shots.push_back(final_shot);

    return shots;
}

} // namespace ave
