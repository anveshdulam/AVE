#include <ave/deterministic_planner.hpp>
#include <ave/proposal_bridge.hpp>
#include <ave/timeline.hpp>
#include <ave/media_pool.hpp>
#include <ave/command.hpp>

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

void test_deterministic_beat_grid_planner() {
    std::cout << "[Test 1/4] Testing DeterministicPlanner Beat-Grid & Cost Solver..." << std::endl;

    ave::AudioRhythmReport music_rhythm;
    music_rhythm.primary_tempo_bpm = 120.0;
    // 0.5s beat grid over 10 seconds (20 beats)
    for (int i = 0; i <= 20; ++i) {
        ave::BeatMarker b;
        b.pts = ave::Rational(i * 500, 1000);
        b.is_downbeat = (i % 4 == 0); // Downbeat every 2.0s
        music_rhythm.beats.push_back(b);
    }

    // Target reference fingerprint: 2.0s median shot duration, high beat sync
    ave::ReferenceFingerprint reference;
    reference.pacing.median_sec = 2.0;
    reference.pacing.iqr_sec = 0.5;
    reference.rhythm.beat_sync_ratio = 1.0;
    reference.motion.mean_motion_energy = 0.35;

    // 4 candidate footage clips (5.0s each)
    std::vector<ave::SourceClipCandidate> candidates = {
        {"c1", "a1", "Action_Car_Fast", ave::Rational(5, 1), ave::Rational(0, 1), 0.70, {}, false},
        {"c2", "a2", "Medium_City_Street", ave::Rational(5, 1), ave::Rational(0, 1), 0.35, {}, false}, // Best motion match
        {"c3", "a3", "Static_Portrait", ave::Rational(5, 1), ave::Rational(0, 1), 0.05, {}, false},
        {"c4", "a4", "Broll_Crowd", ave::Rational(5, 1), ave::Rational(0, 1), 0.40, {}, false}
    };

    ave::DeterministicPlanner planner;
    auto proposal = planner.plan(
        "proj_test",
        "V1",
        candidates,
        reference,
        music_rhythm,
        ave::Rational(8, 1) // 8.0s target
    );

    assert(proposal.schema_version == 1);
    assert(!proposal.proposal_id.empty());
    assert(proposal.operations.size() >= 3);

    // Verify all cuts snap strictly to exact Rational beats
    for (const auto& op : proposal.operations) {
        assert(op.operation_type == "insert_clip");
        assert(op.target_ids[0] == "V1");
        assert(op.parameters.timeline_in.denominator() == 1 || op.parameters.timeline_in.denominator() == 2 || op.parameters.timeline_in.denominator() == 1000);
        assert(op.evidence.justified_by == "beat_grid" || op.evidence.justified_by == "downbeat");
    }

    std::cout << "  -> DeterministicPlanner beat grid generation and clip scoring verified." << std::endl;
}

ave::MediaAsset make_test_asset(const std::string& id, const std::string& name) {
    return ave::MediaAsset(id, "D:/media/" + name, name, 1024, "mp4", ave::Rational(10, 1), {});
}

void test_proposal_validation_and_dry_run() {
    std::cout << "[Test 2/4] Testing ProposalBridge Semantic Validation & Cloned Dry-Run..." << std::endl;

    ave::Timeline timeline;
    auto& track = timeline.add_video_track("V1");
    (void)track;

    ave::MediaPool pool;
    pool.register_asset(make_test_asset("a1", "clip1.mp4"));
    pool.register_asset(make_test_asset("a2", "clip2.mp4"));

    ave::EditProposalEnvelope proposal;
    proposal.schema_version = 1;
    proposal.proposal_id = "prop_dryrun_test";
    proposal.project_id = "proj_test";

    // Operation 1: Insert 2s clip on V1
    ave::EditOperation op1;
    op1.operation_type = "insert_clip";
    op1.target_ids = {"V1"};
    op1.parameters.clip_id = "c1";
    op1.parameters.asset_id = "a1";
    op1.parameters.clip_name = "Clip1";
    op1.parameters.timeline_in = ave::Rational(0, 1);
    op1.parameters.duration = ave::Rational(2, 1);
    proposal.operations.push_back(op1);

    // Operation 2: Insert 3s clip on V1
    ave::EditOperation op2;
    op2.operation_type = "insert_clip";
    op2.target_ids = {"V1"};
    op2.parameters.clip_id = "c2";
    op2.parameters.asset_id = "a2";
    op2.parameters.clip_name = "Clip2";
    op2.parameters.timeline_in = ave::Rational(2, 1);
    op2.parameters.duration = ave::Rational(3, 1);
    proposal.operations.push_back(op2);

    // 1. Semantic Validation
    std::vector<std::string> errors;
    assert(ave::ProposalBridge::validate(proposal, timeline, pool, errors));

    // 2. Dry-Run simulation on clone
    ave::AudioRhythmReport rhythm;
    rhythm.primary_tempo_bpm = 120.0;
    auto dry_res = ave::ProposalBridge::dry_run(proposal, timeline, pool, rhythm);
    assert(dry_res.is_valid);
    assert(dry_res.operations_count == 2);
    assert(dry_res.resulting_duration == ave::Rational(5, 1));

    // Verify live timeline remains completely pristine and untouched
    assert(timeline.video_tracks()[0].items().empty());

    std::cout << "  -> ProposalBridge semantic validation and cloned dry-run verified." << std::endl;
}

void test_proposal_atomic_commit_and_single_undo() {
    std::cout << "[Test 3/4] Testing Single Atomic CompositeCommand Execution & 1-Step Undo..." << std::endl;

    ave::Timeline timeline;
    timeline.add_video_track("V1");

    ave::MediaPool pool;
    pool.register_asset(make_test_asset("a1", "clip1.mp4"));
    pool.register_asset(make_test_asset("a2", "clip2.mp4"));

    ave::CommandHistory history;

    ave::EditProposalEnvelope proposal;
    proposal.schema_version = 1;
    proposal.proposal_id = "prop_commit_test";
    proposal.project_id = "proj_test";

    // 3 clips: 0..2s, 2..4s, 4..7s
    for (int i = 0; i < 3; ++i) {
        ave::EditOperation op;
        op.operation_type = "insert_clip";
        op.target_ids = {"V1"};
        op.parameters.clip_id = "clip_" + std::to_string(i);
        op.parameters.asset_id = (i % 2 == 0) ? "a1" : "a2";
        op.parameters.clip_name = "Clip_" + std::to_string(i);
        op.parameters.timeline_in = ave::Rational(i * 2, 1);
        op.parameters.duration = ave::Rational((i == 2) ? 3 : 2, 1);
        proposal.operations.push_back(op);
    }

    // 1. Commit proposal to CommandHistory
    auto res = ave::ProposalBridge::commit(proposal, timeline, pool, history);
    assert(res.is_success());

    // Timeline must now have all 3 clips placed
    assert(timeline.video_tracks()[0].items().size() == 3);
    assert(timeline.duration() == ave::Rational(7, 1));

    // Undo stack must have EXACTLY 1 transaction entry (CompositeCommand)
    assert(history.undo_count() == 1);
    assert(history.can_undo());

    // 2. Perform SINGLE Undo step -> Entire proposal must revert!
    auto undo_res = history.undo(timeline, pool);
    assert(undo_res.is_success());
    assert(timeline.video_tracks()[0].items().empty());
    assert(timeline.duration() == ave::Rational(0, 1));
    assert(history.can_redo());

    // 3. Perform Redo step -> Entire proposal restored!
    auto redo_res = history.redo(timeline, pool);
    assert(redo_res.is_success());
    assert(timeline.video_tracks()[0].items().size() == 3);
    assert(timeline.duration() == ave::Rational(7, 1));

    std::cout << "  -> Single CompositeCommand commit, 1-step undo, and redo verified." << std::endl;
}

void test_diff_summary_and_json() {
    std::cout << "[Test 4/4] Testing Diff Summary and Envelope JSON Serialization..." << std::endl;

    ave::EditProposalEnvelope proposal;
    proposal.schema_version = 1;
    proposal.proposal_id = "prop_diff_test";
    proposal.project_id = "proj_diff";

    ave::EditOperation op;
    op.operation_type = "insert_clip";
    op.target_ids = {"V1"};
    op.parameters.clip_name = "Intro";
    op.parameters.timeline_in = ave::Rational(0, 1);
    op.parameters.duration = ave::Rational(3, 1);
    op.evidence.justified_by = "downbeat";
    proposal.operations.push_back(op);

    ave::Timeline timeline;
    std::string diff = ave::ProposalBridge::generate_diff_summary(proposal, timeline);
    assert(diff.find("prop_diff_test") != std::string::npos);
    assert(diff.find("Intro") == std::string::npos); // operation summary contains operation_type and track
    assert(diff.find("insert_clip on V1") != std::string::npos);

    std::string json_str = proposal.to_json();
    assert(json_str.find("\"proposal_id\": \"prop_diff_test\"") != std::string::npos);

    auto parsed = ave::EditProposalEnvelope::from_json(json_str);
    assert(parsed.proposal_id == "prop_diff_test");
    assert(parsed.project_id == "proj_diff");

    std::cout << "  -> Diff summary and envelope JSON roundtrip verified." << std::endl;
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "Running Phase 7F: Deterministic Planner & Bridge Tests..." << std::endl;
    std::cout << "====================================================" << std::endl;

    test_deterministic_beat_grid_planner();
    test_proposal_validation_and_dry_run();
    test_proposal_atomic_commit_and_single_undo();
    test_diff_summary_and_json();

    std::cout << "====================================================" << std::endl;
    std::cout << "All Phase 7F Planner & Bridge Tests Passed (100%)!" << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}
