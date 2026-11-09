#include <ave/audio_perception.hpp>
#include <ave/perception_report.hpp>
#include <ave/synthetic_eval_harness.hpp>

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

std::vector<int16_t> generate_synthetic_audio(int sample_rate, double duration_sec, double bpm, bool include_speech = false) {
    size_t total_samples = static_cast<size_t>(sample_rate * duration_sec);
    std::vector<int16_t> samples(total_samples, 0);

    double beat_period = 60.0 / bpm;
    size_t beat_samples = static_cast<size_t>(sample_rate * beat_period);

    // 1. Add periodic sharp percussive click pulses (drum attack)
    for (size_t t = 0; t < total_samples; t += beat_samples) {
        for (size_t k = 0; k < 200 && (t + k) < total_samples; ++k) {
            double env = 1.0 - (static_cast<double>(k) / 200.0);
            double s = std::sin(2.0 * 3.1415926535 * 1000.0 * (static_cast<double>(k) / sample_rate));
            samples[t + k] = static_cast<int16_t>(s * env * 24000.0);
        }
    }

    // 2. Add speech burst between 1.0s and 2.5s if requested
    if (include_speech) {
        size_t speech_start = static_cast<size_t>(sample_rate * 1.0);
        size_t speech_end = static_cast<size_t>(sample_rate * 2.5);
        for (size_t i = speech_start; i < speech_end && i < total_samples; ++i) {
            double s = std::sin(2.0 * 3.1415926535 * 300.0 * (static_cast<double>(i) / sample_rate));
            samples[i] = static_cast<int16_t>(samples[i] + s * 12000.0);
        }
    }

    return samples;
}

void test_spectral_flux_loudness() {
    std::cout << "[Test 1/4] Testing SpectralFluxOnsetDetector & LUFS Loudness..." << std::endl;

    ave::SpectralFluxOnsetDetector detector(22050, 441);

    // Test silence loudness
    std::vector<int16_t> silence(22050, 0);
    double lufs_silence = detector.compute_loudness_lufs(silence);
    assert(lufs_silence <= -69.0);

    // Test synthetic audio loudness
    auto audio = generate_synthetic_audio(22050, 3.0, 120.0);
    double lufs_audio = detector.compute_loudness_lufs(audio);
    assert(lufs_audio > -50.0 && lufs_audio < -5.0);

    // Test onset detection
    std::vector<std::pair<ave::Rational, double>> flux_curve;
    auto onsets = detector.detect_onsets(audio, 0.25, &flux_curve);
    assert(onsets.size() >= 5); // 3 seconds at 120 BPM = 6 beats
    assert(!flux_curve.empty());

    std::cout << "  -> SpectralFluxOnsetDetector and loudness verified." << std::endl;
}

void test_beat_detector() {
    std::cout << "[Test 2/4] Testing BeatDetector and Transient Snapping..." << std::endl;

    ave::AudioPerceptionConfig cfg;
    cfg.sample_rate = 22050;
    cfg.default_tempo_bpm = 120.0;

    ave::BeatDetector detector(nullptr, cfg);
    auto audio = generate_synthetic_audio(22050, 4.0, 120.0);

    std::vector<std::pair<ave::Rational, double>> beat_curve;
    auto rhythm = detector.process(audio, &beat_curve);

    assert(rhythm.primary_tempo_bpm == 120.0);
    assert(rhythm.alternate_tempos.size() == 2);
    assert(rhythm.alternate_tempos[0] == 60.0);
    assert(rhythm.alternate_tempos[1] == 240.0);
    assert(rhythm.beats.size() >= 7);

    // Verify downbeat every 4 beats
    assert(rhythm.beats[0].is_downbeat == true);
    if (rhythm.beats.size() > 4) {
        assert(rhythm.beats[4].is_downbeat == true);
    }

    // Verify against SyntheticEvalHarness at 70ms tolerance
    std::vector<ave::Rational> detected_beats;
    for (const auto& b : rhythm.beats) detected_beats.push_back(b.pts);

    std::vector<ave::Rational> gt_beats;
    for (int i = 0; i < 8; ++i) {
        gt_beats.push_back(ave::Rational(i * 500, 1000)); // 120 BPM = 0.5s
    }

    auto eval = ave::SyntheticEvalHarness::evaluate_beats(detected_beats, gt_beats, 70.0, rhythm.primary_tempo_bpm, 120.0);
    assert(eval.true_positives >= 7);
    assert(eval.f_measure > 0.85);
    assert(eval.tempo_accuracy_pct == 100.0);

    std::cout << "  -> BeatDetector rhythm, downbeats, and tempo curve verified." << std::endl;
}

void test_speech_detector() {
    std::cout << "[Test 3/4] Testing SpeechDetector (VAD & Word Tokens)..." << std::endl;

    ave::AudioPerceptionConfig cfg;
    cfg.sample_rate = 22050;
    cfg.speech_energy_threshold = 0.05;

    ave::SpeechDetector detector(nullptr, cfg);
    // 4 seconds audio with speech burst from 1.0s to 2.5s
    auto audio = generate_synthetic_audio(22050, 4.0, 120.0, true);

    auto speech = detector.process(audio);
    assert(!speech.segments.empty());

    // Speech should start near 1.0s and end near 2.5s
    double start_sec = speech.segments[0].start_pts.to_seconds();
    double end_sec = speech.segments[0].end_pts.to_seconds();
    assert(start_sec >= 0.8 && start_sec <= 1.2);
    assert(end_sec >= 2.3 && end_sec <= 2.7);

    // Words should be populated inside speech segment
    assert(!speech.words.empty());
    assert(speech.words[0].start_pts >= speech.segments[0].start_pts);
    assert(speech.words.back().end_pts <= speech.segments[0].end_pts);

    std::cout << "  -> SpeechDetector VAD segmentation and word timestamps verified." << std::endl;
}

void test_audio_perception_pipeline() {
    std::cout << "[Test 4/4] Testing AudioPerceptionPipeline & PerceptionReport Integration..." << std::endl;

    ave::AudioPerceptionPipeline pipeline;
    auto audio = generate_synthetic_audio(22050, 4.0, 120.0, true);

    ave::PerceptionReport report;
    report.asset_id = "test_audio_asset_001";
    assert(pipeline.analyze(audio, report));

    assert(!report.rhythm.beats.empty());
    assert(report.rhythm.primary_tempo_bpm == 120.0);
    assert(!report.speech.segments.empty());
    assert(!report.speech.words.empty());

    const auto* curve = report.find_raw_curve("spectral_flux_onset");
    assert(curve != nullptr);
    assert(!curve->points.empty());

    // Test JSON roundtrip preservation of audio rhythm & speech
    std::string json_str = report.to_json();
    auto parsed = ave::PerceptionReport::from_json(json_str);
    assert(parsed.asset_id == "test_audio_asset_001");
    assert(parsed.rhythm.primary_tempo_bpm == 120.0);
    assert(parsed.rhythm.beats.size() == report.rhythm.beats.size());
    assert(parsed.speech.segments.size() == report.speech.segments.size());

    std::cout << "  -> AudioPerceptionPipeline full report roundtrip verified." << std::endl;
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "Running Phase 7C: Audio & Speech Perception Tests..." << std::endl;
    std::cout << "====================================================" << std::endl;

    try {
        test_spectral_flux_loudness();
        test_beat_detector();
        test_speech_detector();
        test_audio_perception_pipeline();
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION: " << ex.what() << std::endl;
        return 1;
    }

    std::cout << "====================================================" << std::endl;
    std::cout << "All Phase 7C Audio & Speech Tests Passed (100%)!" << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}
