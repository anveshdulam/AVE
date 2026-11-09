#include <ave/visual_perception.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace ave {

namespace {

// sRGB to Linear helper
inline double srgb_to_linear(double c) {
    if (c <= 0.04045) {
        return c / 12.92;
    }
    return std::pow((c + 0.055) / 1.055, 2.4);
}

// Helper f(t) for CIE L*a*b*
inline double lab_f(double t) {
    if (t > 0.008856) {
        return std::cbrt(t);
    }
    return (7.787 * t) + (16.0 / 116.0);
}

} // namespace

ColorLook ColorLookAnalyzer::analyze(const uint8_t* rgba_data, int width, int height) const {
    ColorLook look;
    if (!rgba_data || width <= 0 || height <= 0) {
        return look;
    }

    size_t total_pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (total_pixels == 0) return look;

    // Standard D65 illuminant white point
    constexpr double Xn = 0.95047;
    constexpr double Yn = 1.00000;
    constexpr double Zn = 1.08883;

    double sum_l = 0.0;
    double sum_a = 0.0;
    double sum_b = 0.0;
    std::vector<double> l_values;
    l_values.reserve(total_pixels);

    for (size_t i = 0; i < total_pixels; ++i) {
        double r = rgba_data[i * 4 + 0] / 255.0;
        double g = rgba_data[i * 4 + 1] / 255.0;
        double b = rgba_data[i * 4 + 2] / 255.0;

        double r_lin = srgb_to_linear(r);
        double g_lin = srgb_to_linear(g);
        double b_lin = srgb_to_linear(b);

        // sRGB to XYZ
        double x = 0.4124564 * r_lin + 0.3575761 * g_lin + 0.1804375 * b_lin;
        double y = 0.2126729 * r_lin + 0.7151522 * g_lin + 0.0721750 * b_lin;
        double z = 0.0193339 * r_lin + 0.1191920 * g_lin + 0.9503041 * b_lin;

        // XYZ to L*a*b*
        double fx = lab_f(x / Xn);
        double fy = lab_f(y / Yn);
        double fz = lab_f(z / Zn);

        double l_val = (116.0 * fy) - 16.0;
        double a_val = 500.0 * (fx - fy);
        double b_val = 200.0 * (fy - fz);

        sum_l += l_val;
        sum_a += a_val;
        sum_b += b_val;
        l_values.push_back(l_val);
    }

    look.lab_l = sum_l / total_pixels;
    look.lab_a = sum_a / total_pixels;
    look.lab_b = sum_b / total_pixels;

    // Contrast = standard deviation of L*
    double variance = 0.0;
    for (double l : l_values) {
        double diff = l - look.lab_l;
        variance += diff * diff;
    }
    look.contrast = std::sqrt(variance / total_pixels);

    return look;
}

MotionAnalyzer::MotionAnalyzer(int downsample_factor)
    : m_downsample(std::max(1, downsample_factor)) {}

double MotionAnalyzer::compute_frame_motion(
    const uint8_t* prev_luma,
    const uint8_t* curr_luma,
    int width,
    int height
) const {
    if (!prev_luma || !curr_luma || width <= 0 || height <= 0) {
        return 0.0;
    }

    double total_diff = 0.0;
    size_t count = 0;

    for (int y = 0; y < height; y += m_downsample) {
        for (int x = 0; x < width; x += m_downsample) {
            int idx = y * width + x;
            int diff = std::abs(static_cast<int>(curr_luma[idx]) - static_cast<int>(prev_luma[idx]));
            total_diff += diff;
            count++;
        }
    }

    if (count == 0) return 0.0;
    // Normalize to 0.0 .. 1.0 range
    return (total_diff / static_cast<double>(count)) / 255.0;
}

SigLIPEmbeddingExtractor::SigLIPEmbeddingExtractor(std::shared_ptr<IModelRunner> runner)
    : m_runner(std::move(runner)) {}

std::vector<float> SigLIPEmbeddingExtractor::extract(
    const uint8_t* rgb_data,
    int width,
    int height
) const {
    constexpr size_t EMBEDDING_DIM = 1152; // SigLIP 2 SO400M dimension
    std::vector<float> embedding(EMBEDDING_DIM, 0.0f);

    if (m_runner) {
        std::vector<float> input_tensor;
        input_tensor.reserve(static_cast<size_t>(width) * height * 3);
        for (int i = 0; i < width * height * 3; ++i) {
            input_tensor.push_back(rgb_data ? (rgb_data[i] / 255.0f) : 0.0f);
        }
        std::vector<float> output;
        if (m_runner->run(input_tensor, output) && output.size() == EMBEDDING_DIM) {
            embedding = std::move(output);
        }
    } else {
        // Deterministic feature-based fallback embedding:
        // Distribute multi-scale spatial luminance and color moments across 1152 dimensions
        if (rgb_data && width > 0 && height > 0) {
            size_t total_pixels = static_cast<size_t>(width) * height;
            constexpr size_t BINS = 32;
            std::vector<double> hist_r(BINS, 0.0);
            std::vector<double> hist_g(BINS, 0.0);
            std::vector<double> hist_b(BINS, 0.0);

            for (size_t i = 0; i < total_pixels; ++i) {
                int r_bin = (std::min)(static_cast<int>(rgb_data[i * 3 + 0] / 8), static_cast<int>(BINS - 1));
                int g_bin = (std::min)(static_cast<int>(rgb_data[i * 3 + 1] / 8), static_cast<int>(BINS - 1));
                int b_bin = (std::min)(static_cast<int>(rgb_data[i * 3 + 2] / 8), static_cast<int>(BINS - 1));
                hist_r[r_bin] += 1.0;
                hist_g[g_bin] += 1.0;
                hist_b[b_bin] += 1.0;
            }

            for (size_t d = 0; d < EMBEDDING_DIM; ++d) {
                size_t bin = d % BINS;
                size_t channel = (d / BINS) % 3;
                double val = (channel == 0) ? hist_r[bin] : ((channel == 1) ? hist_g[bin] : hist_b[bin]);
                embedding[d] = static_cast<float>(val / total_pixels);
            }
        }
    }

    // L2 Normalize embedding vector: ||v||_2 = 1.0
    float norm_sq = 0.0f;
    for (float val : embedding) {
        norm_sq += val * val;
    }
    float norm = std::sqrt(norm_sq);
    if (norm > 1e-6f) {
        for (float& val : embedding) {
            val /= norm;
        }
    }

    return embedding;
}

void VectorSearchIndex::add_embedding(KeyframeEmbedding embedding) {
    m_embeddings.push_back(std::move(embedding));
}

float VectorSearchIndex::cosine_similarity(
    const std::vector<float>& a,
    const std::vector<float>& b
) {
    if (a.empty() || a.size() != b.size()) {
        return 0.0f;
    }
    float dot = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
    }
    return dot;
}

std::vector<VectorSearchResult> VectorSearchIndex::search_nearest(
    const std::vector<float>& query_vector,
    size_t top_k
) const {
    std::vector<VectorSearchResult> results;
    if (m_embeddings.empty() || query_vector.empty()) {
        return results;
    }

    results.reserve(m_embeddings.size());
    for (size_t i = 0; i < m_embeddings.size(); ++i) {
        float sim = cosine_similarity(query_vector, m_embeddings[i].vector);
        results.push_back({i, m_embeddings[i].pts, m_embeddings[i].shot_index, sim});
    }

    // Sort descending by similarity
    std::sort(results.begin(), results.end(), [](const VectorSearchResult& a, const VectorSearchResult& b) {
        return a.similarity > b.similarity;
    });

    if (results.size() > top_k) {
        results.resize(top_k);
    }
    return results;
}

VisualPerceptionPipeline::VisualPerceptionPipeline(std::shared_ptr<IModelRunner> siglip_runner)
    : m_color_analyzer()
    , m_motion_analyzer(4)
    , m_embedding_extractor(std::move(siglip_runner)) {}

bool VisualPerceptionPipeline::analyze(
    const std::vector<std::vector<uint8_t>>& frame_rgbas,
    int width,
    int height,
    const std::vector<Rational>& frame_pts_list,
    PerceptionReport& out_report,
    VectorSearchIndex* out_index
) const {
    if (frame_rgbas.empty() || width <= 0 || height <= 0) {
        return false;
    }

    // 1. Color Look on mid-frame
    size_t mid_idx = frame_rgbas.size() / 2;
    out_report.visual.color_look = m_color_analyzer.analyze(frame_rgbas[mid_idx].data(), width, height);

    // 2. Motion Energy across frames
    std::vector<uint8_t> prev_luma(width * height, 0);
    std::vector<uint8_t> curr_luma(width * height, 0);
    double total_motion = 0.0;
    std::vector<std::pair<Rational, double>> motion_pts_curve;

    for (size_t f = 0; f < frame_rgbas.size(); ++f) {
        const auto& rgba = frame_rgbas[f];
        for (int i = 0; i < width * height; ++i) {
            curr_luma[i] = static_cast<uint8_t>(
                0.299 * rgba[i * 4 + 0] + 0.587 * rgba[i * 4 + 1] + 0.114 * rgba[i * 4 + 2]
            );
        }

        if (f > 0) {
            double motion = m_motion_analyzer.compute_frame_motion(
                prev_luma.data(), curr_luma.data(), width, height
            );
            total_motion += motion;
            Rational pts = (f < frame_pts_list.size()) ? frame_pts_list[f] : Rational(static_cast<int64_t>(f), 30);
            motion_pts_curve.push_back({pts, motion});
        }
        prev_luma = curr_luma;
    }

    if (frame_rgbas.size() > 1) {
        out_report.visual.average_motion_energy = total_motion / static_cast<double>(frame_rgbas.size() - 1);
        out_report.visual.motion_curve = motion_pts_curve;
        out_report.add_raw_curve("visual_motion_energy", std::move(motion_pts_curve));
    }

    // 3. Keyframe Embeddings & Vector Indexing
    if (out_index) {
        for (size_t s = 0; s < out_report.shots.size(); ++s) {
            const auto& shot = out_report.shots[s];
            for (const auto& kf_pts : shot.keyframes) {
                // Find closest frame to kf_pts
                size_t closest_idx = 0;
                double min_diff = 1e9;
                for (size_t f = 0; f < frame_pts_list.size(); ++f) {
                    double diff = std::abs((frame_pts_list[f] - kf_pts).to_seconds());
                    if (diff < min_diff) {
                        min_diff = diff;
                        closest_idx = f;
                    }
                }

                if (closest_idx < frame_rgbas.size()) {
                    // Extract RGB from RGBA
                    std::vector<uint8_t> rgb(width * height * 3);
                    const auto& rgba = frame_rgbas[closest_idx];
                    for (int i = 0; i < width * height; ++i) {
                        rgb[i * 3 + 0] = rgba[i * 4 + 0];
                        rgb[i * 3 + 1] = rgba[i * 4 + 1];
                        rgb[i * 3 + 2] = rgba[i * 4 + 2];
                    }

                    auto emb = m_embedding_extractor.extract(rgb.data(), width, height);
                    out_index->add_embedding({kf_pts, s, std::move(emb)});
                }
            }
        }
    }

    return true;
}

} // namespace ave
