#include <ave/analysis_proxy.hpp>
#include <ave/perception_cache.hpp>
#include <ave/perception_report.hpp>
#include <ave/perception_scheduler.hpp>
#include <ave/synthetic_eval_harness.hpp>

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <thread>

void test_analysis_proxy() {
    std::cout << "[Test 1/5] Testing AnalysisProxy..." << std::endl;

    std::filesystem::path test_dir = std::filesystem::current_path() / "test_proxy_tmp";
    std::filesystem::path src_video = test_dir / "raw_footage.mov";

    ave::AnalysisProxyConfig cfg;
    cfg.video_width = 480;
    cfg.video_height = 270;
    cfg.video_fps = 25;
    cfg.audio_sample_rate = 22050;
    cfg.audio_channels = 1;

    ave::AnalysisProxy proxy(src_video, test_dir, cfg);
    assert(proxy.config().video_width == 480);
    assert(!proxy.is_ready());

    std::string v_cmd = proxy.build_ffmpeg_video_command("custom_ffmpeg");
    assert(v_cmd.find("custom_ffmpeg") != std::string::npos);
    assert(v_cmd.find("scale=480:270") != std::string::npos);
    assert(v_cmd.find("fps=25") != std::string::npos);

    std::string a_cmd = proxy.build_ffmpeg_audio_command("custom_ffmpeg");
    assert(a_cmd.find("-ar 22050") != std::string::npos);
    assert(a_cmd.find("-ac 1") != std::string::npos);

    assert(proxy.generate_mock_proxies());
    assert(proxy.is_ready());

    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);
    std::cout << "  -> AnalysisProxy verified successfully." << std::endl;
}

void test_perception_report() {
    std::cout << "[Test 2/5] Testing PerceptionReport..." << std::endl;

    ave::PerceptionReport report;
    report.asset_id = "asset-test-001";
    report.asset_hash = "sha256_mock_hash_abc123";
    report.duration = ave::Rational(120, 1);
    report.time_base = ave::Rational(1, 1000);

    // Add shots
    ave::ShotBoundary s1;
    s1.start_pts = ave::Rational(0, 1);
    s1.end_pts = ave::Rational(45, 10);
    s1.type = ave::ShotTransitionType::Cut;
    s1.confidence = 0.98;
    s1.keyframes = {ave::Rational(2, 1)};
    report.shots.push_back(s1);

    ave::ShotBoundary s2;
    s2.start_pts = ave::Rational(45, 10);
    s2.end_pts = ave::Rational(100, 10);
    s2.type = ave::ShotTransitionType::Dissolve;
    s2.confidence = 0.91;
    report.shots.push_back(s2);

    // Add rhythm
    report.rhythm.primary_tempo_bpm = 124.5;
    report.rhythm.alternate_tempos = {62.25, 249.0};
    report.rhythm.integrated_loudness_lufs = -13.8;
    report.rhythm.beats.push_back({ave::Rational(0, 1), true, 0.99});
    report.rhythm.beats.push_back({ave::Rational(500, 1000), false, 0.95});

    // Add raw curve
    std::vector<std::pair<ave::Rational, double>> raw_probs;
    raw_probs.push_back({ave::Rational(0, 1), 0.05});
    raw_probs.push_back({ave::Rational(45, 10), 0.94});
    report.add_raw_curve("transnet_prob", raw_probs);

    std::string json_str = report.to_json();
    assert(json_str.find("\"asset-test-001\"") != std::string::npos);
    assert(json_str.find("\"dissolve\"") != std::string::npos);

    // Roundtrip parse
    ave::PerceptionReport parsed = ave::PerceptionReport::from_json(json_str);
    assert(parsed.asset_id == "asset-test-001");
    assert(parsed.shots.size() == 2);
    assert(parsed.shots[0].type == ave::ShotTransitionType::Cut);
    assert(parsed.shots[1].type == ave::ShotTransitionType::Dissolve);
    assert(parsed.rhythm.primary_tempo_bpm == 124.5);
    assert(parsed.rhythm.beats.size() == 2);
    assert(parsed.rhythm.beats[0].is_downbeat == true);
    assert(parsed.raw_curves.size() == 1);
    assert(parsed.raw_curves[0].name == "transnet_prob");
    assert(parsed.raw_curves[0].points.size() == 2);

    std::cout << "  -> PerceptionReport serialization and roundtrip verified." << std::endl;
}

void test_perception_cache() {
    std::cout << "[Test 3/5] Testing PerceptionCache..." << std::endl;

    std::filesystem::path cache_dir = std::filesystem::current_path() / "test_perception_cache_tmp";
    ave::PerceptionCache cache(cache_dir);

    std::string key1 = ave::PerceptionCache::generate_key("asset_hash_1", "transnet_v2", "2.0.1", "model_sha_xxx", "{}");
    std::string key2 = ave::PerceptionCache::generate_key("asset_hash_1", "transnet_v2", "2.0.1", "model_sha_xxx", "{}");
    std::string key3 = ave::PerceptionCache::generate_key("asset_hash_2", "transnet_v2", "2.0.1", "model_sha_xxx", "{}");

    assert(key1 == key2);
    assert(key1 != key3);
    assert(!cache.has_report(key1));

    ave::PerceptionReport rep;
    rep.asset_id = "asset-42";
    rep.duration = ave::Rational(60, 1);
    rep.shots.push_back({ave::Rational(0, 1), ave::Rational(10, 1), ave::ShotTransitionType::Cut, 0.99, {}});

    assert(cache.save_report(key1, rep));
    assert(cache.has_report(key1));

    auto loaded = cache.load_report(key1);
    assert(loaded.has_value());
    assert(loaded->asset_id == "asset-42");
    assert(loaded->shots.size() == 1);

    assert(cache.invalidate(key1));
    assert(!cache.has_report(key1));

    std::error_code ec;
    std::filesystem::remove_all(cache_dir, ec);
    std::cout << "  -> PerceptionCache atomic persistence verified." << std::endl;
}

void test_perception_scheduler() {
    std::cout << "[Test 4/5] Testing PerceptionScheduler..." << std::endl;

    ave::PerceptionScheduler scheduler(2);
    assert(scheduler.worker_count() == 2);

    std::atomic<bool> task_ran{false};
    ave::JobId id1 = scheduler.submit("test_task", ave::JobPriority::Normal,
        [&](const std::atomic<bool>& cancel_token, std::function<void(float progress)> report_progress) {
            report_progress(0.5f);
            if (cancel_token.load()) return false;
            task_ran.store(true);
            report_progress(1.0f);
            return true;
        }
    );

    assert(scheduler.wait(id1, std::chrono::milliseconds(2000)));
    assert(task_ran.load() == true);
    assert(scheduler.status(id1) == ave::JobStatus::Completed);
    assert(scheduler.progress(id1) == 1.0f);

    // Test priority: Playback outranks analysis
    scheduler.set_playback_active(true);
    assert(scheduler.is_playback_active());

    std::atomic<bool> deferred_ran{false};
    ave::JobId id2 = scheduler.submit("deferred_task", ave::JobPriority::Low,
        [&](const std::atomic<bool>&, std::function<void(float)>) {
            deferred_ran.store(true);
            return true;
        }
    );

    // While playback is active, task should remain queued
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!deferred_ran.load());

    // Once playback stops, worker should resume and execute
    scheduler.set_playback_active(false);
    assert(scheduler.wait(id2, std::chrono::milliseconds(2000)));
    assert(deferred_ran.load());
    assert(scheduler.status(id2) == ave::JobStatus::Completed);

    // Test cancellation
    ave::JobId id3 = scheduler.submit("cancellable_task", ave::JobPriority::Normal,
        [&](const std::atomic<bool>& cancel_token, std::function<void(float)>) {
            for (int i = 0; i < 50; ++i) {
                if (cancel_token.load()) return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return true;
        }
    );
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(scheduler.cancel(id3));
    scheduler.wait(id3, std::chrono::milliseconds(2000));
    assert(scheduler.status(id3) == ave::JobStatus::Cancelled);

    std::cout << "  -> PerceptionScheduler priority, execution, and cancellation verified." << std::endl;
}

void test_synthetic_eval_harness() {
    std::cout << "[Test 5/5] Testing SyntheticEvalHarness..." << std::endl;

    ave::Rational fps(25, 1);

    // 1. Cut evaluation
    // Ground truth cuts at frame 50, frame 100, frame 150
    std::vector<ave::Rational> gt_cuts = {
        ave::Rational(50, 25),
        ave::Rational(100, 25),
        ave::Rational(150, 25)
    };

    // Detected cuts: frame 51 (+1 frame), frame 100 (exact), frame 149 (-1 frame), frame 200 (false positive)
    std::vector<ave::Rational> det_cuts = {
        ave::Rational(51, 25),
        ave::Rational(100, 25),
        ave::Rational(149, 25),
        ave::Rational(200, 25)
    };

    auto cut_res = ave::SyntheticEvalHarness::evaluate_cuts(det_cuts, gt_cuts, 2, fps);
    assert(cut_res.true_positives == 3);
    assert(cut_res.false_positives == 1);
    assert(cut_res.false_negatives == 0);
    assert(std::abs(cut_res.precision - 0.75) < 0.001);
    assert(std::abs(cut_res.recall - 1.0) < 0.001);
    assert(cut_res.f1_score > 0.85);

    // 2. Transition overlap evaluation
    std::vector<std::pair<ave::Rational, ave::Rational>> gt_trans = {
        {ave::Rational(10, 1), ave::Rational(12, 1)}
    };
    std::vector<std::pair<ave::Rational, ave::Rational>> det_trans = {
        {ave::Rational(10, 1), ave::Rational(12, 1)}
    };
    auto trans_res = ave::SyntheticEvalHarness::evaluate_transitions(det_trans, gt_trans);
    assert(std::abs(trans_res.mean_iou - 1.0) < 0.001);
    assert(std::abs(trans_res.f1_score - 1.0) < 0.001);

    // 3. Beat evaluation at 120 BPM (0.5s intervals)
    std::vector<ave::Rational> gt_beats = {
        ave::Rational(0, 1),
        ave::Rational(500, 1000),
        ave::Rational(1000, 1000)
    };
    std::vector<ave::Rational> det_beats = {
        ave::Rational(5, 1000), // +5ms offset
        ave::Rational(503, 1000),
        ave::Rational(1002, 1000)
    };
    auto beat_res = ave::SyntheticEvalHarness::evaluate_beats(det_beats, gt_beats, 70.0, 120.0, 120.0);
    assert(beat_res.true_positives == 3);
    assert(beat_res.false_positives == 0);
    assert(beat_res.false_negatives == 0);
    assert(std::abs(beat_res.f_measure - 1.0) < 0.001);
    assert(beat_res.tempo_accuracy_pct == 100.0);

    // 4. Command generation strings
    std::string v_cmd = ave::SyntheticEvalHarness::generate_synthetic_video_ffmpeg_cmd("test_syn.mp4", {50, 100});
    assert(v_cmd.find("testsrc") != std::string::npos);

    std::string a_cmd = ave::SyntheticEvalHarness::generate_synthetic_audio_ffmpeg_cmd("test_syn.wav", 120.0);
    assert(a_cmd.find("sine=") != std::string::npos);

    std::cout << "  -> SyntheticEvalHarness metrics and test generators verified." << std::endl;
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "Running Phase 7A: Perception Infrastructure Tests..." << std::endl;
    std::cout << "====================================================" << std::endl;

    try {
        test_analysis_proxy();
        test_perception_report();
        test_perception_cache();
        test_perception_scheduler();
        test_synthetic_eval_harness();
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION: " << ex.what() << std::endl;
        return 1;
    }

    std::cout << "====================================================" << std::endl;
    std::cout << "All Phase 7A Infrastructure Tests Passed (100%)!" << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}
