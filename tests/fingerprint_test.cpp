#include <ave/reference_fingerprint.hpp>
#include <ave/perception_report.hpp>
#include <ave/timeline.hpp>

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

void test_pacing_and_burstiness() {
    std::cout << "[Test 1/5] Testing Pacing & Burstiness Extraction..." << std::endl;

    ave::PerceptionReport report;
    // 5 shots with durations: 1.0, 2.0, 3.0, 4.0, 5.0 seconds
    for (int i = 0; i < 5; ++i) {
        ave::ShotBoundary s;
        s.start_pts = ave::Rational(i * (i + 1) / 2, 1);
        s.end_pts = ave::Rational((i + 1) * (i + 2) / 2, 1);
        report.shots.push_back(s);
    }

    auto fp = ave::FingerprintEngine::from_perception_report(report);
    assert(fp.pacing.median_sec == 3.0);
    assert(fp.pacing.p25_sec == 2.0);
    assert(fp.pacing.p75_sec == 4.0);
    assert(fp.pacing.iqr_sec == 2.0);
    assert(fp.pacing.burstiness > -1.0 && fp.pacing.burstiness < 1.0);

    std::cout << "  -> Pacing median, IQR, and burstiness verified." << std::endl;
}

void test_cut_to_beat_phase() {
    std::cout << "[Test 2/5] Testing Cut-to-Beat Phase Alignment..." << std::endl;

    ave::PerceptionReport report;
    // 4 beats at 0.5s intervals (120 BPM): 0.0, 0.5, 1.0, 1.5, 2.0
    for (int i = 0; i < 5; ++i) {
        ave::BeatMarker b;
        b.pts = ave::Rational(i * 500, 1000);
        b.is_downbeat = (i % 4 == 0);
        report.rhythm.beats.push_back(b);
    }

    // Shots cutting exactly on beat 1 (0.5s), beat 2 (1.0s), and beat 4 (2.0s)
    ave::ShotBoundary s1{ave::Rational(0, 1), ave::Rational(500, 1000)};
    ave::ShotBoundary s2{ave::Rational(500, 1000), ave::Rational(1000, 1000)};
    ave::ShotBoundary s3{ave::Rational(1000, 1000), ave::Rational(2000, 1000)};
    ave::ShotBoundary s4{ave::Rational(2000, 1000), ave::Rational(3000, 1000)};
    report.shots = {s1, s2, s3, s4};

    auto fp = ave::FingerprintEngine::from_perception_report(report);
    assert(fp.rhythm.beat_sync_ratio == 1.0); // 100% of cuts aligned to beats
    assert(fp.rhythm.downbeat_sync_ratio > 0.0); // Cut at 2.0s is on a downbeat
    assert(std::abs(fp.rhythm.mean_offset_ms) < 1.0); // 0ms offset

    std::cout << "  -> Cut-to-beat sync ratio and downbeat alignment verified." << std::endl;
}

void test_distance_metric_properties() {
    std::cout << "[Test 3/5] Testing Mathematical Distance Function D(F1, F2)..." << std::endl;

    ave::ReferenceFingerprint fp1;
    fp1.pacing.median_sec = 2.0;
    fp1.pacing.iqr_sec = 1.0;
    fp1.pacing.burstiness = 0.1;
    fp1.rhythm.beat_sync_ratio = 0.8;
    fp1.rhythm.downbeat_sync_ratio = 0.25;
    fp1.transitions.hard_cut_ratio = 0.9;
    fp1.transitions.dissolve_ratio = 0.1;
    fp1.motion.mean_motion_energy = 0.3;
    fp1.color.lab_l = 60.0;
    fp1.color.lab_a = 5.0;
    fp1.color.lab_b = 10.0;

    // 1. Identity: D(F, F) == 0
    double dist_self = ave::FingerprintEngine::compute_distance(fp1, fp1);
    assert(dist_self == 0.0);

    // 2. Symmetry: D(A, B) == D(B, A)
    ave::ReferenceFingerprint fp2 = fp1;
    fp2.pacing.median_sec = 4.0;
    fp2.rhythm.beat_sync_ratio = 0.2;
    fp2.transitions.dissolve_ratio = 0.5;
    fp2.color.lab_l = 30.0;

    double d_12 = ave::FingerprintEngine::compute_distance(fp1, fp2);
    double d_21 = ave::FingerprintEngine::compute_distance(fp2, fp1);
    assert(std::abs(d_12 - d_21) < 1e-6);

    // 3. Range [0.0, 1.0]
    assert(d_12 > 0.0 && d_12 <= 1.0);

    // 4. Detailed Breakdown verification
    auto bd = ave::FingerprintEngine::compute_distance_detailed(fp1, fp2);
    assert(bd.pacing_dist > 0.0);
    assert(bd.rhythm_dist > 0.0);
    assert(bd.color_dist > 0.0);
    assert(bd.total_distance == d_12);

    std::cout << "  -> Distance metric identity, symmetry, bounds, and breakdown verified." << std::endl;
}

void test_from_timeline_extraction() {
    std::cout << "[Test 4/5] Testing Fingerprint Extraction from Native Timeline..." << std::endl;

    ave::Timeline timeline;
    auto& track = timeline.add_video_track("V1");

    // Place 3 clips on V1: 0..2s, 2..5s, 5..7s
    ave::TimelineItem item1("id1", "c1", "a1", "Clip1", ave::Rational(0, 1), ave::Rational(2, 1), ave::Rational(0, 1));
    ave::TimelineItem item2("id2", "c2", "a1", "Clip2", ave::Rational(2, 1), ave::Rational(3, 1), ave::Rational(0, 1));
    ave::TimelineItem item3("id3", "c3", "a1", "Clip3", ave::Rational(5, 1), ave::Rational(2, 1), ave::Rational(0, 1));

    track.add_item(item1);
    track.add_item(item2);
    track.add_item(item3);

    ave::AudioRhythmReport rhythm;
    rhythm.primary_tempo_bpm = 120.0;
    for (int i = 0; i < 15; ++i) {
        ave::BeatMarker b;
        b.pts = ave::Rational(i, 2); // 0.5s beat grid
        rhythm.beats.push_back(b);
    }

    auto fp = ave::FingerprintEngine::from_timeline(timeline, rhythm, 0.25);
    assert(fp.pacing.median_sec == 2.0);
    assert(fp.motion.mean_motion_energy == 0.25);
    assert(fp.rhythm.beat_sync_ratio == 1.0); // Cuts at 2.0s and 5.0s fall exactly on 0.5s grid

    std::cout << "  -> Fingerprint from native Timeline tracks and items verified." << std::endl;
}

void test_json_roundtrip() {
    std::cout << "[Test 5/5] Testing ReferenceFingerprint JSON Roundtrip..." << std::endl;

    ave::ReferenceFingerprint fp;
    fp.pacing.median_sec = 2.5;
    fp.pacing.iqr_sec = 1.2;
    fp.pacing.burstiness = -0.4;
    fp.rhythm.beat_sync_ratio = 0.85;
    fp.rhythm.mean_offset_ms = -15.0;
    fp.transitions.hard_cut_ratio = 0.8;
    fp.transitions.dissolve_ratio = 0.2;
    fp.motion.mean_motion_energy = 0.42;
    fp.color.lab_l = 55.5;
    fp.color.contrast = 18.2;
    fp.tempo_bpm = 128.0;

    std::string json_str = fp.to_json();
    assert(!json_str.empty());

    auto parsed = ave::ReferenceFingerprint::from_json(json_str);
    assert(std::abs(parsed.pacing.median_sec - 2.5) < 1e-3);
    assert(std::abs(parsed.pacing.burstiness - (-0.4)) < 1e-3);
    assert(std::abs(parsed.rhythm.beat_sync_ratio - 0.85) < 1e-3);
    assert(std::abs(parsed.rhythm.mean_offset_ms - (-15.0)) < 1e-3);
    assert(std::abs(parsed.transitions.hard_cut_ratio - 0.8) < 1e-3);
    assert(std::abs(parsed.color.lab_l - 55.5) < 1e-3);
    assert(std::abs(parsed.tempo_bpm - 128.0) < 1e-3);

    std::cout << "  -> ReferenceFingerprint JSON serialization and parsing verified." << std::endl;
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "Running Phase 7E: Reference Fingerprint v2 Tests..." << std::endl;
    std::cout << "====================================================" << std::endl;

    test_pacing_and_burstiness();
    test_cut_to_beat_phase();
    test_distance_metric_properties();
    test_from_timeline_extraction();
    test_json_roundtrip();

    std::cout << "====================================================" << std::endl;
    std::cout << "All Phase 7E Reference Fingerprint Tests Passed (100%)!" << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}
