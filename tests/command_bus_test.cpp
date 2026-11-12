#include "ave/command.hpp"
#include "ave/timeline.hpp"
#include "ave/media_pool.hpp"

#include <iostream>
#include <cassert>

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

#define ASSERT_FALSE(cond) \
    do { \
        if ((cond)) { \
            std::cerr << "Assertion failed (expected false): " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            std::cerr << "Assertion failed: " #a " == " #b " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

int main() {
    std::cout << "Running Command Bus & Undo/Redo Engine Tests..." << std::endl;

    // Setup Timeline & MediaPool
    ave::MediaPool pool;
    ave::Timeline timeline("tl-cmd", "Command Test Timeline", ave::Rational{24, 1});
    timeline.add_video_track("V2");

    std::string v1_id = timeline.video_tracks()[0].id();
    std::string v2_id = timeline.video_tracks()[1].id();

    ave::CommandHistory history(10);
    int listener_notifications = 0;
    history.add_change_listener([&]() {
        listener_notifications++;
    });

    ASSERT_FALSE(history.is_dirty());
    ASSERT_FALSE(history.can_undo());
    ASSERT_FALSE(history.can_redo());

    // 1. Test InsertClipCommand
    std::cout << "  Testing InsertClipCommand..." << std::endl;
    ave::TimelineItem clip1("c1", "clip-ref-1", "asset-1", "Clip 1",
                           ave::Rational{0, 1}, ave::Rational{2, 1}, ave::Rational{0, 1});
    auto ins_cmd = std::make_unique<ave::InsertClipCommand>(v1_id, clip1);
    auto res_ins = history.execute(std::move(ins_cmd), timeline, pool);
    ASSERT_TRUE(res_ins.is_success());
    ASSERT_TRUE(history.is_dirty());
    ASSERT_TRUE(history.can_undo());
    ASSERT_EQ(history.undo_count(), 1);
    ASSERT_EQ(timeline.find_track(v1_id)->items().size(), 1);
    ASSERT_EQ(timeline.find_track(v1_id)->items()[0].name(), "Clip 1");

    // Overlapping insert validation failure
    ave::TimelineItem overlap_clip("c-bad", "clip-ref-bad", "asset-1", "Overlap",
                                  ave::Rational{1, 1}, ave::Rational{2, 1}, ave::Rational{0, 1});
    auto bad_ins_cmd = std::make_unique<ave::InsertClipCommand>(v1_id, overlap_clip);
    auto res_bad = history.execute(std::move(bad_ins_cmd), timeline, pool);
    ASSERT_FALSE(res_bad.is_success());
    ASSERT_EQ(res_bad.status, ave::CommandStatus::FailedPrecondition);
    ASSERT_EQ(timeline.find_track(v1_id)->items().size(), 1); // Not inserted

    // Undo Insert
    auto res_undo_ins = history.undo(timeline, pool);
    ASSERT_TRUE(res_undo_ins.is_success());
    ASSERT_EQ(timeline.find_track(v1_id)->items().size(), 0);
    ASSERT_FALSE(history.can_undo());
    ASSERT_TRUE(history.can_redo());
    ASSERT_FALSE(history.is_dirty()); // Returned to initial clean state!

    // Redo Insert
    auto res_redo_ins = history.redo(timeline, pool);
    ASSERT_TRUE(res_redo_ins.is_success());
    ASSERT_EQ(timeline.find_track(v1_id)->items().size(), 1);
    ASSERT_TRUE(history.is_dirty());

    // 2. Test SetTransformCommand
    std::cout << "  Testing SetTransformCommand..." << std::endl;
    ave::TransformParams tf_params;
    tf_params.scale_x = 0.5f;
    tf_params.scale_y = 0.5f;
    tf_params.transform_x = 100.0f;
    tf_params.opacity = 0.75f;
    tf_params.blend_mode = "Screen";

    auto tf_cmd = std::make_unique<ave::SetTransformCommand>(v1_id, "c1", tf_params);
    auto res_tf = history.execute(std::move(tf_cmd), timeline, pool);
    ASSERT_TRUE(res_tf.is_success());

    const auto* item_tf = timeline.find_item("c1");
    ASSERT_TRUE(item_tf != nullptr);
    ASSERT_EQ(item_tf->scale_x(), 0.5f);
    ASSERT_EQ(item_tf->transform_x(), 100.0f);
    ASSERT_EQ(item_tf->opacity(), 0.75f);
    ASSERT_EQ(item_tf->blend_mode(), "Screen");

    // Undo Transform
    history.undo(timeline, pool);
    item_tf = timeline.find_item("c1");
    ASSERT_EQ(item_tf->scale_x(), 1.0f);
    ASSERT_EQ(item_tf->transform_x(), 0.0f);
    ASSERT_EQ(item_tf->opacity(), 1.0f);
    ASSERT_EQ(item_tf->blend_mode(), "Over");

    // Redo Transform
    history.redo(timeline, pool);
    item_tf = timeline.find_item("c1");
    ASSERT_EQ(item_tf->scale_x(), 0.5f);
    ASSERT_EQ(item_tf->opacity(), 0.75f);

    // 3. Test MoveClipCommand
    std::cout << "  Testing MoveClipCommand..." << std::endl;
    // Move from V1 [0..2s] to V2 [1..3s]
    auto move_cmd = std::make_unique<ave::MoveClipCommand>(v1_id, "c1", v2_id, ave::Rational{1, 1});
    auto res_move = history.execute(std::move(move_cmd), timeline, pool);
    ASSERT_TRUE(res_move.is_success());
    ASSERT_EQ(timeline.find_track(v1_id)->items().size(), 0);
    ASSERT_EQ(timeline.find_track(v2_id)->items().size(), 1);
    ASSERT_EQ(timeline.find_track(v2_id)->items()[0].timeline_in(), ave::Rational(1, 1));

    // Undo Move
    history.undo(timeline, pool);
    ASSERT_EQ(timeline.find_track(v1_id)->items().size(), 1);
    ASSERT_EQ(timeline.find_track(v2_id)->items().size(), 0);
    ASSERT_EQ(timeline.find_track(v1_id)->items()[0].timeline_in(), ave::Rational(0, 1));

    // Redo Move
    history.redo(timeline, pool);
    ASSERT_EQ(timeline.find_track(v1_id)->items().size(), 0);
    ASSERT_EQ(timeline.find_track(v2_id)->items().size(), 1);

    // 4. Test SplitClipCommand (Blade Cut)
    std::cout << "  Testing SplitClipCommand..." << std::endl;
    // Clip on V2 is [1..3s]; split at 2.0s
    auto split_cmd = std::make_unique<ave::SplitClipCommand>(v2_id, "c1", ave::Rational{2, 1});
    auto res_split = history.execute(std::move(split_cmd), timeline, pool);
    ASSERT_TRUE(res_split.is_success());
    ASSERT_EQ(timeline.find_track(v2_id)->items().size(), 2);
    ASSERT_EQ(timeline.find_track(v2_id)->items()[0].timeline_in(), ave::Rational(1, 1));
    ASSERT_EQ(timeline.find_track(v2_id)->items()[0].duration(), ave::Rational(1, 1));
    ASSERT_EQ(timeline.find_track(v2_id)->items()[1].timeline_in(), ave::Rational(2, 1));
    ASSERT_EQ(timeline.find_track(v2_id)->items()[1].duration(), ave::Rational(1, 1));

    // Undo Split
    history.undo(timeline, pool);
    ASSERT_EQ(timeline.find_track(v2_id)->items().size(), 1);
    ASSERT_EQ(timeline.find_track(v2_id)->items()[0].id(), "c1");
    ASSERT_EQ(timeline.find_track(v2_id)->items()[0].duration(), ave::Rational(2, 1));

    // Redo Split
    history.redo(timeline, pool);
    ASSERT_EQ(timeline.find_track(v2_id)->items().size(), 2);

    // 5. Test DeleteClipCommand
    std::cout << "  Testing DeleteClipCommand..." << std::endl;
    std::string right_clip_id = timeline.find_track(v2_id)->items()[1].id();
    auto del_cmd = std::make_unique<ave::DeleteClipCommand>(v2_id, right_clip_id);
    auto res_del = history.execute(std::move(del_cmd), timeline, pool);
    ASSERT_TRUE(res_del.is_success());
    ASSERT_EQ(timeline.find_track(v2_id)->items().size(), 1);

    // Undo Delete
    history.undo(timeline, pool);
    ASSERT_EQ(timeline.find_track(v2_id)->items().size(), 2);

    // Redo Delete
    history.redo(timeline, pool);
    ASSERT_EQ(timeline.find_track(v2_id)->items().size(), 1);

    // 6. Test SetTrackMuteCommand
    std::cout << "  Testing SetTrackMuteCommand..." << std::endl;
    ASSERT_FALSE(timeline.find_track(v2_id)->muted());
    auto mute_cmd = std::make_unique<ave::SetTrackMuteCommand>(v2_id, true);
    auto res_mute = history.execute(std::move(mute_cmd), timeline, pool);
    ASSERT_TRUE(res_mute.is_success());
    ASSERT_TRUE(timeline.find_track(v2_id)->muted());

    history.undo(timeline, pool);
    ASSERT_FALSE(timeline.find_track(v2_id)->muted());

    history.redo(timeline, pool);
    ASSERT_TRUE(timeline.find_track(v2_id)->muted());

    // 7. Test CompositeCommand (Atomic Transaction & Rollback)
    std::cout << "  Testing CompositeCommand Transaction & Rollback..." << std::endl;
    // Successful Composite transaction
    std::vector<std::unique_ptr<ave::Command>> batch_good;
    ave::TimelineItem b1("batch-1", "c-ref", "asset-1", "Batch 1",
                        ave::Rational{0, 1}, ave::Rational{1, 1}, ave::Rational{0, 1});
    ave::TimelineItem b2("batch-2", "c-ref", "asset-1", "Batch 2",
                        ave::Rational{2, 1}, ave::Rational{1, 1}, ave::Rational{0, 1});
    batch_good.push_back(std::make_unique<ave::InsertClipCommand>(v1_id, b1));
    batch_good.push_back(std::make_unique<ave::InsertClipCommand>(v1_id, b2));

    auto comp_good = std::make_unique<ave::CompositeCommand>("Batch Insert", std::move(batch_good));
    auto res_cg = history.execute(std::move(comp_good), timeline, pool);
    ASSERT_TRUE(res_cg.is_success());
    ASSERT_EQ(timeline.find_track(v1_id)->items().size(), 2);

    // Single undo undid the entire transaction atomically
    history.undo(timeline, pool);
    ASSERT_EQ(timeline.find_track(v1_id)->items().size(), 0);

    // Redo re-applies entire transaction atomically
    history.redo(timeline, pool);
    ASSERT_EQ(timeline.find_track(v1_id)->items().size(), 2);

    // Failing Composite transaction (rollback verification)
    std::vector<std::unique_ptr<ave::Command>> batch_bad;
    ave::TimelineItem b3("batch-3", "c-ref", "asset-1", "Batch 3",
                        ave::Rational{5, 1}, ave::Rational{1, 1}, ave::Rational{0, 1});
    // Deliberate overlap: b4 overlaps b2 [2..3s]
    ave::TimelineItem b4_bad("batch-4", "c-ref", "asset-1", "Batch 4",
                            ave::Rational{2, 1}, ave::Rational{2, 1}, ave::Rational{0, 1});
    batch_bad.push_back(std::make_unique<ave::InsertClipCommand>(v1_id, b3));
    batch_bad.push_back(std::make_unique<ave::InsertClipCommand>(v1_id, b4_bad));

    auto comp_bad = std::make_unique<ave::CompositeCommand>("Bad Batch", std::move(batch_bad));
    auto res_cb = history.execute(std::move(comp_bad), timeline, pool);
    ASSERT_FALSE(res_cb.is_success());
    // Verification: b3 was NOT left on v1_id; total items on v1_id remain 2!
    ASSERT_EQ(timeline.find_track(v1_id)->items().size(), 2);
    ASSERT_TRUE(timeline.find_item("batch-3") == nullptr);

    // 8. Test Dirty Tracking & Mark Clean
    std::cout << "  Testing Dirty Tracking..." << std::endl;
    ASSERT_TRUE(history.is_dirty());
    history.mark_clean();
    ASSERT_FALSE(history.is_dirty());

    // One more action makes it dirty
    auto mute_v1 = std::make_unique<ave::SetTrackMuteCommand>(v1_id, true);
    history.execute(std::move(mute_v1), timeline, pool);
    ASSERT_TRUE(history.is_dirty());

    // Undo returns to the save point, so is_dirty becomes false again!
    history.undo(timeline, pool);
    ASSERT_FALSE(history.is_dirty());

    // 8.1 Semantic mutation_revision ignores mark_clean() and listener add/remove with ID works
    const uint64_t rev_before_clean = history.mutation_revision();
    history.mark_clean();
    ASSERT_EQ(history.mutation_revision(), rev_before_clean);

    int scoped_listener_hits = 0;
    const auto scoped_id = history.add_change_listener_with_id([&]() {
        scoped_listener_hits++;
    });
    history.execute(std::make_unique<ave::SetTrackMuteCommand>(v1_id, true), timeline, pool);
    ASSERT_EQ(history.mutation_revision(), rev_before_clean + 1);
    ASSERT_EQ(scoped_listener_hits, 1);
    history.remove_change_listener(scoped_id);
    history.undo(timeline, pool);
    ASSERT_EQ(history.mutation_revision(), rev_before_clean + 2);
    ASSERT_EQ(scoped_listener_hits, 1);

    // 9. Test Listeners
    ASSERT_TRUE(listener_notifications > 10);
    std::cout << "  Total change notifications triggered: " << listener_notifications << std::endl;

    // 10. M1.1A Regression: SplitClipCommand preserves all attributes across undo/redo
    std::cout << "  Testing M1.1A SplitClipCommand metadata preservation + undo/redo..." << std::endl;
    {
        ave::TimelineItem styled("styled-c1", "clip-ref-styled", "asset-styled", "Styled Split",
                                 ave::Rational{10, 1}, ave::Rational{4, 1},
                                 ave::Rational{4, 1}, ave::Rational{3, 2});
        styled.set_transform(25.0f, -12.5f);
        styled.set_scale(1.25f, 0.8f);
        styled.set_rotation(15.0f);
        styled.set_anchor(0.3f, 0.7f);
        styled.set_opacity(0.72f);
        styled.set_blend_mode("Add");
        styled.set_enabled(false);

        auto ins_styled = std::make_unique<ave::InsertClipCommand>(v1_id, styled);
        ASSERT_TRUE(history.execute(std::move(ins_styled), timeline, pool).is_success());

        auto split_styled = std::make_unique<ave::SplitClipCommand>(v1_id, "styled-c1", ave::Rational{12, 1});
        auto* split_raw = split_styled.get();
        auto res_split_styled = history.execute(std::move(split_styled), timeline, pool);
        ASSERT_TRUE(res_split_styled.is_success());

        auto verify_split_halves = [&]() -> bool {
            const auto* trk = timeline.find_track(v1_id);
            if (!trk) return false;
            const auto* left = trk->find_item(split_raw->left_item_id());
            const auto* right = trk->find_item(split_raw->right_item_id());
            if (!left || !right) return false;
            if (left->timeline_in() != ave::Rational(10, 1) || left->duration() != ave::Rational(2, 1)) return false;
            if (left->source_in() != ave::Rational(4, 1)) return false;
            if (right->timeline_in() != ave::Rational(12, 1) || right->duration() != ave::Rational(2, 1)) return false;
            if (right->source_in() != ave::Rational(7, 1)) return false;
            for (const auto* half : {left, right}) {
                if (half->clip_id() != "clip-ref-styled" || half->asset_id() != "asset-styled") return false;
                if (half->name() != "Styled Split" || half->speed() != ave::Rational(3, 2)) return false;
                if (half->enabled()) return false;
                if (half->transform_x() != 25.0f || half->transform_y() != -12.5f) return false;
                if (half->scale_x() != 1.25f || half->scale_y() != 0.8f) return false;
                if (half->rotation() != 15.0f) return false;
                if (half->anchor_x() != 0.3f || half->anchor_y() != 0.7f) return false;
                if (half->opacity() != 0.72f) return false;
                if (half->blend_mode() != "Add") return false;
            }
            return true;
        };

        ASSERT_TRUE(verify_split_halves());

        // Undo restores original single clip cleanly
        ASSERT_TRUE(history.undo(timeline, pool).is_success());
        const auto* restored_orig = timeline.find_track(v1_id)->find_item("styled-c1");
        ASSERT_TRUE(restored_orig != nullptr);
        ASSERT_EQ(restored_orig->duration(), ave::Rational(4, 1));
        ASSERT_EQ(restored_orig->source_in(), ave::Rational(4, 1));
        ASSERT_EQ(restored_orig->blend_mode(), "Add");
        ASSERT_FALSE(restored_orig->enabled());

        // Redo restores both halves with all preserved properties
        ASSERT_TRUE(history.redo(timeline, pool).is_success());
        ASSERT_TRUE(verify_split_halves());
    }

    // 11. M1.1A Regression: Track-type validation on MoveClipCommand
    std::cout << "  Testing M1.1A MoveClipCommand track-type validation..." << std::endl;
    {
        timeline.add_audio_track("A2");
        const std::string a1_id = timeline.audio_tracks()[0].id();
        const std::string a2_id = timeline.audio_tracks()[1].id();

        ave::TimelineItem vid_clip("mv-v1", "c-mv-v", "asset-v", "Video For Move",
                                   ave::Rational{30, 1}, ave::Rational{3, 1}, ave::Rational{0, 1});
        ave::TimelineItem aud_clip("mv-a1", "c-mv-a", "asset-a", "Audio For Move",
                                   ave::Rational{30, 1}, ave::Rational{3, 1}, ave::Rational{0, 1});
        ASSERT_TRUE(history.execute(std::make_unique<ave::InsertClipCommand>(v1_id, vid_clip), timeline, pool).is_success());
        ASSERT_TRUE(history.execute(std::make_unique<ave::InsertClipCommand>(a1_id, aud_clip), timeline, pool).is_success());

        const size_t undo_before_reject = history.undo_count();
        const size_t v1_count_before = timeline.find_track(v1_id)->items().size();
        const size_t a1_count_before = timeline.find_track(a1_id)->items().size();

        // Video -> Audio rejected
        auto bad_v_to_a = std::make_unique<ave::MoveClipCommand>(v1_id, "mv-v1", a1_id, ave::Rational{35, 1});
        auto res_v_to_a = history.execute(std::move(bad_v_to_a), timeline, pool);
        ASSERT_FALSE(res_v_to_a.is_success());
        ASSERT_EQ(res_v_to_a.status, ave::CommandStatus::FailedPrecondition);
        ASSERT_TRUE(res_v_to_a.message.find("video") != std::string::npos);
        ASSERT_TRUE(res_v_to_a.message.find("audio") != std::string::npos);
        ASSERT_EQ(timeline.find_track(v1_id)->items().size(), v1_count_before);
        ASSERT_EQ(timeline.find_track(a1_id)->items().size(), a1_count_before);
        ASSERT_EQ(timeline.find_track(v1_id)->find_item("mv-v1")->timeline_in(), ave::Rational(30, 1));
        ASSERT_EQ(history.undo_count(), undo_before_reject);

        // Audio -> Video rejected
        auto bad_a_to_v = std::make_unique<ave::MoveClipCommand>(a1_id, "mv-a1", v1_id, ave::Rational{35, 1});
        auto res_a_to_v = history.execute(std::move(bad_a_to_v), timeline, pool);
        ASSERT_FALSE(res_a_to_v.is_success());
        ASSERT_EQ(res_a_to_v.status, ave::CommandStatus::FailedPrecondition);
        ASSERT_TRUE(res_a_to_v.message.find("audio") != std::string::npos);
        ASSERT_TRUE(res_a_to_v.message.find("video") != std::string::npos);
        ASSERT_EQ(timeline.find_track(v1_id)->items().size(), v1_count_before);
        ASSERT_EQ(timeline.find_track(a1_id)->items().size(), a1_count_before);
        ASSERT_EQ(timeline.find_track(a1_id)->find_item("mv-a1")->timeline_in(), ave::Rational(30, 1));
        ASSERT_EQ(history.undo_count(), undo_before_reject);

        // Video -> Video succeeds & undoes cleanly
        auto good_v_to_v = std::make_unique<ave::MoveClipCommand>(v1_id, "mv-v1", v2_id, ave::Rational{35, 1});
        ASSERT_TRUE(history.execute(std::move(good_v_to_v), timeline, pool).is_success());
        ASSERT_TRUE(timeline.find_track(v2_id)->find_item("mv-v1") != nullptr);
        ASSERT_EQ(timeline.find_track(v2_id)->find_item("mv-v1")->timeline_in(), ave::Rational(35, 1));
        ASSERT_TRUE(history.undo(timeline, pool).is_success());
        ASSERT_TRUE(timeline.find_track(v1_id)->find_item("mv-v1") != nullptr);

        // Audio -> Audio succeeds & undoes cleanly
        auto good_a_to_a = std::make_unique<ave::MoveClipCommand>(a1_id, "mv-a1", a2_id, ave::Rational{35, 1});
        ASSERT_TRUE(history.execute(std::move(good_a_to_a), timeline, pool).is_success());
        ASSERT_TRUE(timeline.find_track(a2_id)->find_item("mv-a1") != nullptr);
        ASSERT_EQ(timeline.find_track(a2_id)->find_item("mv-a1")->timeline_in(), ave::Rational(35, 1));
        ASSERT_TRUE(history.undo(timeline, pool).is_success());
        ASSERT_TRUE(timeline.find_track(a1_id)->find_item("mv-a1") != nullptr);
    }

    std::cout << "All Command Bus & Undo/Redo Engine Tests PASSED!" << std::endl;
    return 0;
}

