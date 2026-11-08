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

    // 9. Test Listeners
    ASSERT_TRUE(listener_notifications > 10);
    std::cout << "  Total change notifications triggered: " << listener_notifications << std::endl;

    std::cout << "All Command Bus & Undo/Redo Engine Tests PASSED!" << std::endl;
    return 0;
}
