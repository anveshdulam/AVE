#include <ave/language_sidecar.hpp>
#include <ave/prompt_parser.hpp>
#include <ave/proposal_inspector.hpp>
#include <ave/deterministic_planner.hpp>
#include <ave/proposal_bridge.hpp>
#include <ave/timeline.hpp>
#include <ave/media_pool.hpp>
#include <ave/command.hpp>

#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

void test_language_sidecar_lifecycle() {
    std::cout << "[Test 1/4] Testing LanguageSidecar Lifecycle & Crash Isolation..." << std::endl;

    ave::SidecarConfig cfg;
    cfg.model_identifier = "qwen3-vl-4b";
    cfg.autostart = true;

    ave::LanguageSidecar sidecar(cfg);
    assert(sidecar.is_ready());
    assert(sidecar.ping());

    auto resp = sidecar.query("test_task", "{\"prompt\":\"make a montage\"}");
    assert(resp.success);
    assert(!resp.response_payload.empty());

    // Test crash isolation
    sidecar.simulate_crash_for_testing();
    assert(!sidecar.is_ready());
    assert(sidecar.state() == ave::SidecarState::Crashed);

    auto crash_resp = sidecar.query("test_task", "{}");
    assert(!crash_resp.success);
    assert(crash_resp.error_message.find("crashed") != std::string::npos);

    std::cout << "  -> LanguageSidecar lifecycle, query, and crash isolation verified." << std::endl;
}

void test_prompt_parser_semantic_extraction() {
    std::cout << "[Test 2/4] Testing PromptParser Intent & Constraint Extraction..." << std::endl;

    auto sidecar = std::make_shared<ave::LanguageSidecar>();
    ave::PromptParser parser(sidecar);

    // Prompt 1: Fast high-energy sports montage
    std::string prompt1 = "Create a fast energetic 15s action montage cut to the drum beats, preserve speech";
    auto c1 = parser.parse(prompt1);
    assert(c1.target_duration.has_value());
    assert(c1.target_duration->numerator() == 15);
    assert(c1.pacing == ave::PacingIntent::Fast);
    assert(c1.sync_to_beats == true);
    assert(c1.target_motion_energy > 0.50);
    assert(c1.preserve_speech == true);

    auto cfg1 = c1.to_planner_config();
    assert(cfg1.min_shot_duration_sec < 1.0);
    assert(cfg1.max_shot_duration_sec <= 2.5);

    auto fp1 = c1.to_reference_fingerprint();
    assert(fp1.pacing.median_sec <= 1.5);
    assert(fp1.rhythm.beat_sync_ratio == 1.0);

    // Prompt 2: Slow cinematic interview
    std::string prompt2 = "Make a slow relaxed cinematic video about 60 seconds with talking shots";
    auto c2 = parser.parse(prompt2);
    assert(c2.target_duration.has_value());
    assert(c2.target_duration->numerator() == 60);
    assert(c2.pacing == ave::PacingIntent::Slow);
    assert(c2.target_motion_energy < 0.25);

    // Prompt 3: Crash sidecar and verify seamless fallback
    sidecar->simulate_crash_for_testing();
    auto c3 = parser.parse("Fast quick 30s video");
    assert(c3.target_duration.has_value() && c3.target_duration->numerator() == 30);
    assert(c3.pacing == ave::PacingIntent::Fast);

    std::cout << "  -> PromptParser duration, pacing, and sidecar fallback verified." << std::endl;
}

void test_proposal_inspector() {
    std::cout << "[Test 3/4] Testing ProposalInspector Presentation ViewModel..." << std::endl;

    ave::EditProposalEnvelope proposal;
    proposal.proposal_id = "prop_insp_001";
    proposal.source = "ai";

    ave::EditOperation op1;
    op1.operation_type = "insert_clip";
    op1.target_ids = {"V1"};
    op1.parameters.clip_name = "Runner.mp4";
    op1.parameters.timeline_in = ave::Rational(0, 1);
    op1.parameters.duration = ave::Rational(2, 1);
    op1.evidence.justified_by = "beat_grid";
    proposal.operations.push_back(op1);

    ave::DryRunResult dry_run;
    dry_run.is_valid = true;
    dry_run.operations_count = 1;
    dry_run.resulting_duration = ave::Rational(2, 1);
    dry_run.reference_distance = 0.12; // 88% fidelity

    auto vm = ave::ProposalInspector::inspect(proposal, dry_run);
    assert(vm.is_valid);
    assert(vm.proposal_id == "prop_insp_001");
    assert(vm.operations_count == 1);
    assert(vm.stylistic_fidelity_pct >= 87.0 && vm.stylistic_fidelity_pct <= 89.0);
    assert(vm.diff_items.size() == 1);
    assert(vm.diff_items[0].clip_name == "Runner.mp4");

    std::string summary = vm.to_formatted_summary();
    assert(summary.find("Runner.mp4") != std::string::npos);
    assert(summary.find("88.0%") != std::string::npos || summary.find("88%") != std::string::npos);

    std::string json_str = vm.to_json();
    assert(json_str.find("\"proposal_id\": \"prop_insp_001\"") != std::string::npos);

    std::cout << "  -> ProposalInspector ViewModel diffs and formatting verified." << std::endl;
}

void test_end_to_end_user_prompt_to_timeline() {
    std::cout << "[Test 4/4] Testing End-to-End: User Prompt -> Constraints -> Plan -> DryRun -> Commit..." << std::endl;

    // 1. User inputs text prompt
    std::string prompt = "Make a fast 6s action montage synced to drum beats";
    ave::PromptParser parser;
    auto constraints = parser.parse(prompt);
    assert(constraints.target_duration.has_value());
    assert(constraints.target_duration->numerator() == 6);

    // 2. Music rhythm
    ave::AudioRhythmReport music;
    music.primary_tempo_bpm = 120.0;
    for (int i = 0; i <= 15; ++i) {
        ave::BeatMarker b;
        b.pts = ave::Rational(i * 500, 1000); // 0.5s beats
        b.is_downbeat = (i % 4 == 0);
        music.beats.push_back(b);
    }

    // 3. Candidate footage in pool
    std::vector<ave::SourceClipCandidate> candidates = {
        {"c1", "a1", "Skate_Jump", ave::Rational(5, 1), ave::Rational(0, 1), 0.70, {}, false},
        {"c2", "a2", "Bike_Drift", ave::Rational(5, 1), ave::Rational(0, 1), 0.65, {}, false},
        {"c3", "a3", "Crowd_Cheer", ave::Rational(5, 1), ave::Rational(0, 1), 0.40, {}, false}
    };

    // 4. Plan edit
    auto ref = constraints.to_reference_fingerprint();
    ave::DeterministicPlanner planner(constraints.to_planner_config());
    auto proposal = planner.plan("proj_e2e", "V1", candidates, ref, music, *constraints.target_duration);
    assert(!proposal.operations.empty());

    // 5. Timeline & MediaPool
    ave::Timeline timeline;
    timeline.add_video_track("V1");
    ave::MediaPool pool;
    pool.register_asset(ave::MediaAsset("a1", "D:/media/skate.mp4", "skate.mp4", 1024, "mp4", ave::Rational(10, 1), {}));
    pool.register_asset(ave::MediaAsset("a2", "D:/media/bike.mp4", "bike.mp4", 1024, "mp4", ave::Rational(10, 1), {}));
    pool.register_asset(ave::MediaAsset("a3", "D:/media/crowd.mp4", "crowd.mp4", 1024, "mp4", ave::Rational(10, 1), {}));

    // 6. Cloned Dry-Run simulation
    auto dry_res = ave::ProposalBridge::dry_run(proposal, timeline, pool, music, &ref);
    assert(dry_res.is_valid);
    assert(timeline.video_tracks()[0].items().empty()); // Live timeline untouched

    // 7. Inspector review
    auto vm = ave::ProposalInspector::inspect(proposal, dry_res);
    assert(vm.is_valid);
    assert(vm.operations_count == proposal.operations.size());

    // 8. Single atomic commit & 1-step undo
    ave::CommandHistory history;
    auto commit_res = ave::ProposalBridge::commit(proposal, timeline, pool, history);
    assert(commit_res.is_success());
    assert(!timeline.video_tracks()[0].items().empty());
    assert(history.undo_count() == 1);

    // 1-step undo restores clean state
    auto undo_res = history.undo(timeline, pool);
    assert(undo_res.is_success());
    assert(timeline.video_tracks()[0].items().empty());

    std::cout << "  -> End-to-end prompt to timeline commit and undo verified." << std::endl;
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "Running Phase 8: Local Language Layer Tests...       " << std::endl;
    std::cout << "====================================================" << std::endl;

    test_language_sidecar_lifecycle();
    test_prompt_parser_semantic_extraction();
    test_proposal_inspector();
    test_end_to_end_user_prompt_to_timeline();

    std::cout << "====================================================" << std::endl;
    std::cout << "All Phase 8 Language Layer Tests Passed (100%)!      " << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}

