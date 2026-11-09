#pragma once

#include <ave/model_runtime.hpp>
#include <ave/perception_report.hpp>
#include <ave/time.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ave {

struct KeyframeEmbedding {
    Rational pts;
    size_t shot_index = 0;
    std::vector<float> vector; // 1152-d normalized embedding (SigLIP 2)
};

struct VectorSearchResult {
    size_t index = 0;
    Rational pts;
    size_t shot_index = 0;
    float similarity = 0.0f; // Cosine similarity in [-1.0, 1.0]
};

/**
 * @brief Computes color look metrics (CIE L*a*b* mean values and luminance contrast).
 */
class ColorLookAnalyzer {
public:
    ColorLookAnalyzer() = default;

    /**
     * @brief Computes CIE L*a*b* and contrast from RGBA8 pixel buffer.
     * @param rgba_data Interleaved RGBA8 pixel data.
     * @param width Width in pixels.
     * @param height Height in pixels.
     */
    [[nodiscard]] ColorLook analyze(const uint8_t* rgba_data, int width, int height) const;
};

/**
 * @brief Computes inter-frame motion vectors and average motion energy.
 */
class MotionAnalyzer {
public:
    explicit MotionAnalyzer(int downsample_factor = 4);

    /**
     * @brief Computes motion energy between two consecutive grayscale / luminance frames.
     * @return Average motion displacement magnitude across blocks.
     */
    [[nodiscard]] double compute_frame_motion(
        const uint8_t* prev_luma,
        const uint8_t* curr_luma,
        int width,
        int height
    ) const;

private:
    int m_downsample;
};

/**
 * @brief Extracts 1152-dimensional SigLIP 2 visual semantic embeddings.
 */
class SigLIPEmbeddingExtractor {
public:
    explicit SigLIPEmbeddingExtractor(std::shared_ptr<IModelRunner> runner = nullptr);

    /**
     * @brief Extracts normalized 1152-d embedding vector for an RGB image.
     */
    [[nodiscard]] std::vector<float> extract(const uint8_t* rgb_data, int width, int height) const;

private:
    std::shared_ptr<IModelRunner> m_runner;
};

/**
 * @brief Fast local cosine similarity vector index for keyframe search and reference matching.
 */
class VectorSearchIndex {
public:
    VectorSearchIndex() = default;

    void add_embedding(KeyframeEmbedding embedding);
    void clear() noexcept { m_embeddings.clear(); }

    [[nodiscard]] size_t size() const noexcept { return m_embeddings.size(); }
    [[nodiscard]] const std::vector<KeyframeEmbedding>& embeddings() const noexcept { return m_embeddings; }

    /**
     * @brief Queries index for top-K nearest keyframes using cosine similarity.
     */
    [[nodiscard]] std::vector<VectorSearchResult> search_nearest(
        const std::vector<float>& query_vector,
        size_t top_k = 5
    ) const;

    /**
     * @brief Computes cosine similarity between two normalized vectors.
     */
    [[nodiscard]] static float cosine_similarity(
        const std::vector<float>& a,
        const std::vector<float>& b
    );

private:
    std::vector<KeyframeEmbedding> m_embeddings;
};

/**
 * @brief Unified visual perception pipeline analyzing color, motion, and keyframe semantics.
 */
class VisualPerceptionPipeline {
public:
    explicit VisualPerceptionPipeline(std::shared_ptr<IModelRunner> siglip_runner = nullptr);

    /**
     * @brief Populates VisualReport in PerceptionReport and builds vector index for keyframes.
     */
    bool analyze(
        const std::vector<std::vector<uint8_t>>& frame_rgbas,
        int width,
        int height,
        const std::vector<Rational>& frame_pts_list,
        PerceptionReport& out_report,
        VectorSearchIndex* out_index = nullptr
    ) const;

private:
    ColorLookAnalyzer m_color_analyzer;
    MotionAnalyzer m_motion_analyzer;
    SigLIPEmbeddingExtractor m_embedding_extractor;
};

} // namespace ave
