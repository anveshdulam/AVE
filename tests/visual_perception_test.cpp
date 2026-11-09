#include <ave/visual_perception.hpp>
#include <ave/perception_report.hpp>

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

void test_color_look() {
    std::cout << "[Test 1/4] Testing ColorLookAnalyzer (CIE L*a*b* & Contrast)..." << std::endl;

    ave::ColorLookAnalyzer analyzer;

    // Test pure black 10x10
    std::vector<uint8_t> black_rgba(10 * 10 * 4, 0);
    for (size_t i = 3; i < black_rgba.size(); i += 4) black_rgba[i] = 255;
    auto look_black = analyzer.analyze(black_rgba.data(), 10, 10);
    assert(look_black.lab_l < 0.01);
    assert(look_black.contrast < 0.01);

    // Test pure white 10x10
    std::vector<uint8_t> white_rgba(10 * 10 * 4, 255);
    auto look_white = analyzer.analyze(white_rgba.data(), 10, 10);
    assert(look_white.lab_l > 99.0);
    assert(look_white.contrast < 0.01);

    // Test split 50% black, 50% white -> contrast ~ 50
    std::vector<uint8_t> split_rgba = black_rgba;
    for (size_t i = 0; i < (split_rgba.size() / 2); i += 4) {
        split_rgba[i] = 255;
        split_rgba[i + 1] = 255;
        split_rgba[i + 2] = 255;
    }
    auto look_split = analyzer.analyze(split_rgba.data(), 10, 10);
    assert(look_split.contrast > 45.0 && look_split.contrast < 55.0);

    // Test red tint (should have positive a*)
    std::vector<uint8_t> red_rgba(10 * 10 * 4, 0);
    for (size_t i = 0; i < red_rgba.size(); i += 4) {
        red_rgba[i] = 240; // Red
        red_rgba[i + 3] = 255; // Alpha
    }
    auto look_red = analyzer.analyze(red_rgba.data(), 10, 10);
    assert(look_red.lab_a > 50.0);

    std::cout << "  -> ColorLookAnalyzer CIE L*a*b* and contrast verified." << std::endl;
}

void test_motion_analyzer() {
    std::cout << "[Test 2/4] Testing MotionAnalyzer (Luma Displacement & Energy)..." << std::endl;

    ave::MotionAnalyzer analyzer(2);

    // Identical frames -> 0 motion
    std::vector<uint8_t> frame1(64 * 64, 128);
    std::vector<uint8_t> frame2 = frame1;
    double motion_zero = analyzer.compute_frame_motion(frame1.data(), frame2.data(), 64, 64);
    assert(motion_zero == 0.0);

    // Inverted frames -> high motion
    std::vector<uint8_t> frame_black(64 * 64, 0);
    std::vector<uint8_t> frame_white(64 * 64, 255);
    double motion_max = analyzer.compute_frame_motion(frame_black.data(), frame_white.data(), 64, 64);
    assert(motion_max > 0.95);

    std::cout << "  -> MotionAnalyzer motion energy computation verified." << std::endl;
}

void test_vector_search_index() {
    std::cout << "[Test 3/4] Testing SigLIPEmbeddingExtractor & VectorSearchIndex..." << std::endl;

    ave::SigLIPEmbeddingExtractor extractor(nullptr); // Deterministic fallback
    ave::VectorSearchIndex index;

    // Generate red image and blue image
    std::vector<uint8_t> red_rgb(32 * 32 * 3, 0);
    for (size_t i = 0; i < red_rgb.size(); i += 3) red_rgb[i] = 255;

    std::vector<uint8_t> blue_rgb(32 * 32 * 3, 0);
    for (size_t i = 2; i < blue_rgb.size(); i += 3) blue_rgb[i] = 255;

    auto emb_red = extractor.extract(red_rgb.data(), 32, 32);
    auto emb_blue = extractor.extract(blue_rgb.data(), 32, 32);

    assert(emb_red.size() == 1152);
    assert(emb_blue.size() == 1152);

    // Test L2 unit normalization
    float norm_red = 0.0f;
    for (float v : emb_red) norm_red += v * v;
    assert(std::abs(std::sqrt(norm_red) - 1.0f) < 1e-4f);

    // Cosine similarity
    float self_sim = ave::VectorSearchIndex::cosine_similarity(emb_red, emb_red);
    assert(std::abs(self_sim - 1.0f) < 1e-4f);

    float cross_sim = ave::VectorSearchIndex::cosine_similarity(emb_red, emb_blue);
    assert(cross_sim < 0.5f); // Red and Blue vectors are well separated

    // Add keyframes to index
    index.add_embedding({ave::Rational(1, 1), 0, emb_red});
    index.add_embedding({ave::Rational(5, 1), 1, emb_blue});
    assert(index.size() == 2);

    // Query with red image -> should match shot 0 (pts 1/1) as nearest
    auto results = index.search_nearest(emb_red, 1);
    assert(results.size() == 1);
    assert(results[0].shot_index == 0);
    assert(results[0].pts == ave::Rational(1, 1));
    assert(results[0].similarity > 0.99f);

    std::cout << "  -> SigLIPEmbeddingExtractor and VectorSearchIndex verified." << std::endl;
}

void test_visual_pipeline_integration() {
    std::cout << "[Test 4/4] Testing VisualPerceptionPipeline Integration..." << std::endl;

    ave::VisualPerceptionPipeline pipeline(nullptr);

    // Create 10 synthetic RGBA frames (64x64) with moving brightness
    int w = 64;
    int h = 64;
    std::vector<std::vector<uint8_t>> frames;
    std::vector<ave::Rational> pts_list;

    for (int f = 0; f < 10; ++f) {
        std::vector<uint8_t> frame(w * h * 4, static_cast<uint8_t>(f * 20));
        for (size_t i = 3; i < frame.size(); i += 4) frame[i] = 255;
        frames.push_back(frame);
        pts_list.push_back(ave::Rational(f, 30));
    }

    ave::PerceptionReport report;
    ave::ShotBoundary shot;
    shot.start_pts = ave::Rational(0, 1);
    shot.end_pts = ave::Rational(10, 30);
    shot.keyframes.push_back(ave::Rational(5, 30));
    report.shots.push_back(shot);

    ave::VectorSearchIndex index;
    assert(pipeline.analyze(frames, w, h, pts_list, report, &index));

    assert(report.visual.average_motion_energy > 0.0);
    assert(report.visual.motion_curve.size() == 9);
    assert(report.find_raw_curve("visual_motion_energy") != nullptr);
    assert(index.size() == 1);

    std::cout << "  -> VisualPerceptionPipeline full report population verified." << std::endl;
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "Running Phase 7D: Visual Perception & Search Tests..." << std::endl;
    std::cout << "====================================================" << std::endl;

    test_color_look();
    test_motion_analyzer();
    test_vector_search_index();
    test_visual_pipeline_integration();

    std::cout << "====================================================" << std::endl;
    std::cout << "All Phase 7D Visual Perception Tests Passed (100%)!" << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}
