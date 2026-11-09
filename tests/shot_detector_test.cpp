#include <ave/classical_shot_detector.hpp>
#include <ave/model_runtime.hpp>
#include <ave/perception_report.hpp>
#include <ave/shot_detector.hpp>
#include <ave/synthetic_eval_harness.hpp>
#include <ave/transnet_v2_detector.hpp>

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

ave::VideoFrame create_solid_frame(int w, int h, uint8_t r, uint8_t g, uint8_t b, const ave::Rational& pts, const ave::Rational& dur) {
    ave::VideoFrame f;
    f.width = w;
    f.height = h;
    f.pts = pts;
    f.duration = dur;
    f.rgba_data.resize(static_cast<size_t>(w * h * 4));
    for (size_t i = 0; i < f.rgba_data.size(); i += 4) {
        f.rgba_data[i] = r;
        f.rgba_data[i + 1] = g;
        f.rgba_data[i + 2] = b;
        f.rgba_data[i + 3] = 255;
    }
    return f;
}

void test_classical_detector() {
    std::cout << "[Test 1/4] Testing ClassicalShotDetector..." << std::endl;

    ave::ClassicalShotDetector detector;
    ave::Rational fps(25, 1);
    ave::Rational dur(1, 25);

    auto f_black = create_solid_frame(160, 90, 0, 0, 0, ave::Rational(0, 25), dur);
    auto f_black2 = create_solid_frame(160, 90, 0, 0, 0, ave::Rational(1, 25), dur);
    auto f_white = create_solid_frame(160, 90, 255, 255, 255, ave::Rational(2, 25), dur);

    double d_same = detector.compute_histogram_distance(f_black, f_black2);
    assert(d_same < 0.001);

    double d_diff = detector.compute_histogram_distance(f_black, f_white);
    assert(d_diff > 0.90);

    // Build synthetic sequence:
    // Frames 0-29: Red scene (Shot 1)
    // Frame 30: Hard Cut to Blue scene (Shot 2)
    // Frames 30-59: Blue scene
    std::vector<ave::VideoFrame> sequence;
    for (int i = 0; i < 30; ++i) {
        sequence.push_back(create_solid_frame(48, 27, 255, 0, 0, ave::Rational(i, 25), dur));
    }
    for (int i = 30; i < 60; ++i) {
        sequence.push_back(create_solid_frame(48, 27, 0, 0, 255, ave::Rational(i, 25), dur));
    }

    std::vector<std::pair<ave::Rational, double>> raw_curve;
    auto shots = detector.detect_shots(sequence, fps, &raw_curve);
    assert(shots.size() == 2);
    assert(shots[0].type == ave::ShotTransitionType::Cut);
    assert(shots[0].end_pts == ave::Rational(30, 25));
    assert(shots[1].start_pts == ave::Rational(30, 25));
    assert(shots[1].end_pts == ave::Rational(60, 25));
    assert(!shots[0].keyframes.empty());

    assert(raw_curve.size() == 60);
    assert(raw_curve[30].second > 0.5);

    std::cout << "  -> ClassicalShotDetector verified successfully." << std::endl;
}

void test_model_runtime() {
    std::cout << "[Test 2/4] Testing ModelRuntime and TransNetV2 Manifest..." << std::endl;

    auto runner = ave::ModelRuntime::create_transnet_runner();
    assert(runner != nullptr);
    assert(!runner->is_loaded());

    const auto& m = runner->manifest();
    assert(m.name == "TransNetV2");
    assert(m.version == "2.0.0");
    assert(m.license == "MIT");
    assert(m.input_shape.size() == 5);
    assert(m.input_shape[1] == 100);

    assert(runner->load("dummy_path_model.onnx", ave::ExecutionProvider::Cpu));
    assert(runner->is_loaded());
    assert(runner->provider() == ave::ExecutionProvider::Cpu);

    // Test inference with simulated 100-frame input tensor
    std::vector<float> input_tensor(100 * 27 * 48 * 3, 0.0f);
    // Induce a delta at frame 50
    for (size_t i = 50 * 27 * 48 * 3; i < 100 * 27 * 48 * 3; ++i) {
        input_tensor[i] = 1.0f;
    }

    std::vector<float> output_tensor;
    assert(runner->run(input_tensor, output_tensor));
    assert(output_tensor.size() == 100);
    assert(output_tensor[50] > 0.8f);

    std::cout << "  -> ModelRuntime and TransNetV2 tensor execution verified." << std::endl;
}

void test_transnet_detector() {
    std::cout << "[Test 3/4] Testing TransNetV2Detector..." << std::endl;

    auto runner = ave::ModelRuntime::create_transnet_runner();
    runner->load("dummy_model.onnx", ave::ExecutionProvider::Cpu);

    ave::TransNetV2Detector detector(std::move(runner));
    ave::Rational fps(25, 1);
    ave::Rational dur(1, 25);

    // Build 120-frame sequence:
    // Frames 0-49: Green scene
    // Frame 50: Hard Cut to Magenta scene
    // Frames 50-119: Magenta scene
    std::vector<ave::VideoFrame> sequence;
    for (int i = 0; i < 50; ++i) {
        sequence.push_back(create_solid_frame(48, 27, 0, 255, 0, ave::Rational(i, 25), dur));
    }
    for (int i = 50; i < 120; ++i) {
        sequence.push_back(create_solid_frame(48, 27, 255, 0, 255, ave::Rational(i, 25), dur));
    }

    std::vector<std::pair<ave::Rational, double>> raw_curve;
    auto shots = detector.detect_shots(sequence, fps, &raw_curve);
    assert(shots.size() == 2);
    assert(shots[0].type == ave::ShotTransitionType::Cut);
    assert(shots[0].end_pts == ave::Rational(50, 25));
    assert(shots[1].start_pts == ave::Rational(50, 25));

    // Verify against SyntheticEvalHarness
    std::vector<ave::Rational> detected_cuts = {shots[0].end_pts};
    std::vector<ave::Rational> gt_cuts = {ave::Rational(50, 25)};
    auto eval = ave::SyntheticEvalHarness::evaluate_cuts(detected_cuts, gt_cuts, 2, fps);
    assert(eval.true_positives == 1);
    assert(eval.false_positives == 0);
    assert(eval.f1_score == 1.0);

    std::cout << "  -> TransNetV2Detector and rolling window verified." << std::endl;
}

void test_unified_fusion_detector() {
    std::cout << "[Test 4/4] Testing Unified ShotDetector (Fusion & Refinement)..." << std::endl;

    auto runner = ave::ModelRuntime::create_transnet_runner();
    runner->load("transnet_v2.onnx", ave::ExecutionProvider::Cpu);

    ave::ShotDetector detector(std::move(runner), ave::ShotDetectorMode::Auto);
    assert(detector.has_transnet());

    ave::Rational fps(25, 1);
    ave::Rational dur(1, 25);

    // Build sequence with cut at frame 40
    std::vector<ave::VideoFrame> sequence;
    for (int i = 0; i < 40; ++i) {
        sequence.push_back(create_solid_frame(48, 27, 30, 30, 30, ave::Rational(i, 25), dur));
    }
    for (int i = 40; i < 80; ++i) {
        sequence.push_back(create_solid_frame(48, 27, 220, 220, 220, ave::Rational(i, 25), dur));
    }

    ave::PerceptionReport report;
    auto shots = detector.process(sequence, fps, &report);
    assert(shots.size() == 2);
    assert(shots[0].end_pts == ave::Rational(40, 25));
    assert(shots[1].start_pts == ave::Rational(40, 25));

    // Verify PerceptionReport population
    assert(report.shots.size() == 2);
    const auto* curve = report.find_raw_curve("transnet_transition_prob");
    assert(curve != nullptr);
    assert(curve->points.size() == 80);

    // Test fallback when runner is nullptr
    ave::ShotDetector fallback_detector(nullptr, ave::ShotDetectorMode::Auto);
    assert(!fallback_detector.has_transnet());
    ave::PerceptionReport fallback_report;
    auto fallback_shots = fallback_detector.process(sequence, fps, &fallback_report);
    assert(fallback_shots.size() == 2);
    assert(fallback_shots[0].end_pts == ave::Rational(40, 25));
    const auto* f_curve = fallback_report.find_raw_curve("classical_frame_delta");
    assert(f_curve != nullptr);

    std::cout << "  -> Unified ShotDetector fusion, refinement, and fallback verified." << std::endl;
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "Running Phase 7B: Shot & Transition Detector Tests..." << std::endl;
    std::cout << "====================================================" << std::endl;

    try {
        test_classical_detector();
        test_model_runtime();
        test_transnet_detector();
        test_unified_fusion_detector();
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION: " << ex.what() << std::endl;
        return 1;
    }

    std::cout << "====================================================" << std::endl;
    std::cout << "All Phase 7B Shot Detector Tests Passed (100%)!" << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}
