#include "ave/timeline.hpp"
#include <iostream>
#include <cassert>
#include <cmath>
#include <stdexcept>

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            std::cerr << "Assertion failed: " #a " == " #b << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

int main() {
    std::cout << "[TimelineTest] Starting timeline model verification..." << std::endl;

    // 1. TimelineItem basics
    {
        ave::TimelineItem item("item-1", "clip-1", "asset-1", "Shot A",
                               ave::Rational{0, 1}, ave::Rational{5, 1},
                               ave::Rational{10, 1}, ave::Rational{1, 1});

        ASSERT_EQ(item.id(), "item-1");
        ASSERT_EQ(item.timeline_in(), ave::Rational(0, 1));
        ASSERT_EQ(item.duration(), ave::Rational(5, 1));
        ASSERT_EQ(item.timeline_out(), ave::Rational(5, 1));
        ASSERT_EQ(item.source_in(), ave::Rational(10, 1));
        ASSERT_EQ(item.source_out(), ave::Rational(15, 1));
        ASSERT_TRUE(item.contains_time(ave::Rational(2, 1)));
        ASSERT_TRUE(!item.contains_time(ave::Rational(5, 1))); // half-open [in, out)
        ASSERT_EQ(item.source_time_at(ave::Rational(2, 1)), ave::Rational(12, 1));
    }

    // 2. Track non-overlapping and sorting
    {
        ave::Track track("trk-1", "V1", ave::TrackType::Video, 0);
        ave::TimelineItem item1("item-1", "clip-1", "asset-1", "Shot A",
                                ave::Rational{0, 1}, ave::Rational{4, 1},
                                ave::Rational{0, 1});
        ave::TimelineItem item2("item-2", "clip-2", "asset-1", "Shot B",
                                ave::Rational{4, 1}, ave::Rational{6, 1},
                                ave::Rational{0, 1});
        ave::TimelineItem overlap("item-bad", "clip-3", "asset-1", "Shot Overlap",
                                  ave::Rational{2, 1}, ave::Rational{4, 1},
                                  ave::Rational{0, 1});

        ASSERT_TRUE(track.add_item(item1));
        ASSERT_TRUE(track.add_item(item2));
        ASSERT_TRUE(!track.add_item(overlap)); // Overlap must be rejected
        ASSERT_EQ(track.items().size(), 2);
        ASSERT_EQ(track.duration(), ave::Rational(10, 1));

        const auto* found = track.item_at(ave::Rational(3, 1));
        ASSERT_TRUE(found != nullptr);
        ASSERT_EQ(found->name(), "Shot A");

        const auto* found2 = track.item_at(ave::Rational(7, 1));
        ASSERT_TRUE(found2 != nullptr);
        ASSERT_EQ(found2->name(), "Shot B");
    }

    // 3. Timeline hierarchy & active items query
    {
        ave::Timeline tl("tl-1", "Master Edit", ave::Rational{24, 1}, 1920, 1080);
        // Default constructor creates V1 and A1
        ASSERT_EQ(tl.video_tracks().size(), 1);
        ASSERT_EQ(tl.audio_tracks().size(), 1);

        auto* v1 = tl.find_track_mut(tl.video_tracks()[0].id());
        auto* a1 = tl.find_track_mut(tl.audio_tracks()[0].id());
        ASSERT_TRUE(v1 != nullptr);
        ASSERT_TRUE(a1 != nullptr);

        v1->add_item(ave::TimelineItem("v-item-1", "clip-1", "asset-1", "Video Clip",
                                       ave::Rational{0, 1}, ave::Rational{10, 1},
                                       ave::Rational{0, 1}));
        a1->add_item(ave::TimelineItem("a-item-1", "clip-2", "asset-2", "Audio Track",
                                       ave::Rational{0, 1}, ave::Rational{15, 1},
                                       ave::Rational{0, 1}));

        ASSERT_EQ(tl.duration(), ave::Rational(15, 1));
        ASSERT_TRUE(!v1->locked());
        v1->set_locked(true);
        ASSERT_TRUE(v1->locked());
        v1->set_locked(false);
        ASSERT_TRUE(!v1->locked());

        auto active_at_5 = tl.active_items_at(ave::Rational(5, 1));
        ASSERT_EQ(active_at_5.size(), 2);

        auto active_at_12 = tl.active_items_at(ave::Rational(12, 1));
        ASSERT_EQ(active_at_12.size(), 1);
        ASSERT_EQ(active_at_12[0]->name(), "Audio Track");
    }

    // 4. Blade Cut (split_item)
    {
        ave::Timeline tl("tl-split", "Blade Sequence", ave::Rational{24, 1});
        const auto& v1 = tl.video_tracks()[0];
        auto* v1_mut = tl.find_track_mut(v1.id());
        v1_mut->add_item(ave::TimelineItem("clip-to-cut", "clip-1", "asset-1", "Continuous Shot",
                                           ave::Rational{0, 1}, ave::Rational{10, 1},
                                           ave::Rational{0, 1}));

        auto [left_id, right_id] = tl.split_item(v1.id(), "clip-to-cut", ave::Rational{4, 1});
        ASSERT_TRUE(!left_id.empty());
        ASSERT_TRUE(!right_id.empty());

        const auto* track = tl.find_track(v1.id());
        ASSERT_TRUE(track != nullptr);
        ASSERT_EQ(track->items().size(), 2);

        const auto* left = track->find_item(left_id);
        const auto* right = track->find_item(right_id);
        ASSERT_TRUE(left != nullptr);
        ASSERT_TRUE(right != nullptr);

        ASSERT_EQ(left->timeline_in(), ave::Rational(0, 1));
        ASSERT_EQ(left->duration(), ave::Rational(4, 1));
        ASSERT_EQ(left->timeline_out(), ave::Rational(4, 1));
        ASSERT_EQ(left->source_in(), ave::Rational(0, 1));
        ASSERT_EQ(left->source_out(), ave::Rational(4, 1));

        ASSERT_EQ(right->timeline_in(), ave::Rational(4, 1));
        ASSERT_EQ(right->duration(), ave::Rational(6, 1));
        ASSERT_EQ(right->timeline_out(), ave::Rational(10, 1));
        ASSERT_EQ(right->source_in(), ave::Rational(4, 1));
        ASSERT_EQ(right->source_out(), ave::Rational(10, 1));
    }

    // 4.1 Blade Cut metadata & speed preservation
    {
        ave::Timeline tl("tl-split-meta", "Split Metadata Sequence", ave::Rational{24, 1});
        const std::string v1_id = tl.video_tracks()[0].id();
        auto* v1_mut = tl.find_track_mut(v1_id);
        ASSERT_TRUE(v1_mut != nullptr);

        ave::TimelineItem styled("clip-styled", "clip-ref-s", "asset-s", "Styled Shot",
                                 ave::Rational{1, 1}, ave::Rational{6, 1},
                                 ave::Rational{10, 1}, ave::Rational{2, 1});
        styled.set_transform(42.5f, -18.25f);
        styled.set_scale(1.75f, 0.6f);
        styled.set_rotation(-33.5f);
        styled.set_anchor(0.2f, 0.8f);
        styled.set_opacity(0.65f);
        styled.set_blend_mode("Multiply");
        styled.set_enabled(false);
        ASSERT_TRUE(v1_mut->add_item(std::move(styled)));

        auto [left_id, right_id] = tl.split_item(v1_id, "clip-styled", ave::Rational{3, 1});
        const auto* track = tl.find_track(v1_id);
        ASSERT_TRUE(track != nullptr);
        ASSERT_EQ(track->items().size(), 2);

        const auto* left = track->find_item(left_id);
        const auto* right = track->find_item(right_id);
        ASSERT_TRUE(left != nullptr);
        ASSERT_TRUE(right != nullptr);

        ASSERT_EQ(left->timeline_in(), ave::Rational(1, 1));
        ASSERT_EQ(left->duration(), ave::Rational(2, 1));
        ASSERT_EQ(left->source_in(), ave::Rational(10, 1));
        ASSERT_EQ(left->source_out(), ave::Rational(14, 1));

        ASSERT_EQ(right->timeline_in(), ave::Rational(3, 1));
        ASSERT_EQ(right->duration(), ave::Rational(4, 1));
        ASSERT_EQ(right->source_in(), ave::Rational(14, 1));
        ASSERT_EQ(right->source_out(), ave::Rational(22, 1));

        for (const auto* half : {left, right}) {
            ASSERT_EQ(half->clip_id(), "clip-ref-s");
            ASSERT_EQ(half->asset_id(), "asset-s");
            ASSERT_EQ(half->name(), "Styled Shot");
            ASSERT_EQ(half->speed(), ave::Rational(2, 1));
            ASSERT_EQ(half->enabled(), false);
            ASSERT_EQ(half->transform_x(), 42.5f);
            ASSERT_EQ(half->transform_y(), -18.25f);
            ASSERT_EQ(half->scale_x(), 1.75f);
            ASSERT_EQ(half->scale_y(), 0.6f);
            ASSERT_EQ(half->rotation(), -33.5f);
            ASSERT_EQ(half->anchor_x(), 0.2f);
            ASSERT_EQ(half->anchor_y(), 0.8f);
            ASSERT_TRUE(std::abs(half->opacity() - 0.65f) < 0.0001f);
            ASSERT_EQ(half->blend_mode(), "Multiply");
        }
    }

    // 5. JSON roundtrip serialization
    {
        ave::Timeline tl("tl-json", "JSON Test", ave::Rational{30, 1});
        auto* v1 = tl.find_track_mut(tl.video_tracks()[0].id());
        ave::TimelineItem item("json-v1", "c1", "a1", "Item 1",
                               ave::Rational{0, 1}, ave::Rational{8, 1},
                               ave::Rational{2, 1});
        item.set_transform(50.0f, -25.0f);
        item.set_scale(1.5f, 0.75f);
        item.set_rotation(45.0f);
        item.set_anchor(0.25f, 0.75f);
        item.set_opacity(0.85f);
        item.set_blend_mode("Screen");
        v1->add_item(std::move(item));

        std::string json = tl.to_json();
        ASSERT_TRUE(!json.empty());
        ASSERT_TRUE(json.find("\"schema_version\"") != std::string::npos);
        ASSERT_TRUE(json.find("\"1.1.0\"") != std::string::npos);
        ASSERT_TRUE(json.find("\"duration\"") != std::string::npos);
        ASSERT_TRUE(json.find("\"soloed\"") != std::string::npos);
        ASSERT_TRUE(json.find("\"locked\"") != std::string::npos);
        ASSERT_TRUE(json.find("\"enabled\"") != std::string::npos);

        ave::Timeline restored = ave::Timeline::from_json(json);
        ASSERT_EQ(restored.id(), "tl-json");
        ASSERT_EQ(restored.name(), "JSON Test");
        ASSERT_EQ(restored.frame_rate(), ave::Rational(30, 1));
        ASSERT_EQ(restored.video_tracks().size(), 1);
        ASSERT_EQ(restored.video_tracks()[0].items().size(), 1);
        const auto& rest_item = restored.video_tracks()[0].items()[0];
        ASSERT_EQ(rest_item.name(), "Item 1");
        ASSERT_EQ(rest_item.transform_x(), 50.0f);
        ASSERT_EQ(rest_item.transform_y(), -25.0f);
        ASSERT_EQ(rest_item.scale_x(), 1.5f);
        ASSERT_EQ(rest_item.scale_y(), 0.75f);
        ASSERT_EQ(rest_item.rotation(), 45.0f);
        ASSERT_EQ(rest_item.anchor_x(), 0.25f);
        ASSERT_EQ(rest_item.anchor_y(), 0.75f);
        ASSERT_TRUE(std::abs(rest_item.opacity() - 0.85f) < 0.001f);
        ASSERT_EQ(rest_item.blend_mode(), "Screen");
        ASSERT_TRUE(rest_item.has_transform());
        ASSERT_EQ(restored, tl);
    }

    // 6. Extended JSON roundtrip (track flags, enabled, special chars/Unicode) & strict validation
    {
        ave::Timeline tl("tl-rich", "Master \"Cut\"\nSeq\t— 編集", ave::Rational{24000, 1001}, 3840, 2160, 96000);
        auto* v1 = tl.find_track_mut(tl.video_tracks()[0].id());
        auto* a1 = tl.find_track_mut(tl.audio_tracks()[0].id());
        ASSERT_TRUE(v1 != nullptr && a1 != nullptr);
        v1->set_name("Track \\ \"V1\"");
        v1->set_soloed(true);
        v1->set_locked(true);
        a1->set_muted(true);

        ave::TimelineItem vitem("rich-v1", "clip-r1", "asset-r1",
                                "D:\\Footage\\Take \"A\"\\shot_01_最終.mov\nLine2\tOK",
                                ave::Rational{1001, 24000}, ave::Rational{1001, 240},
                                ave::Rational{1001, 3000}, ave::Rational{3, 2});
        vitem.set_enabled(false);
        vitem.set_transform(-12.5f, 34.25f);
        vitem.set_scale(1.25f, 0.8f);
        vitem.set_rotation(-15.0f);
        vitem.set_anchor(0.1f, 0.9f);
        vitem.set_opacity(0.6f);
        vitem.set_blend_mode("Multiply");
        ASSERT_TRUE(v1->add_item(std::move(vitem)));

        ave::TimelineItem aitem("rich-a1", "clip-r2", "asset-r2", "Dialogue — हिन्दी",
                                ave::Rational{0, 1}, ave::Rational{5, 1},
                                ave::Rational{0, 1}, ave::Rational{1, 1});
        ASSERT_TRUE(a1->add_item(std::move(aitem)));

        const std::string encoded = tl.to_json();
        const ave::Timeline decoded = ave::Timeline::from_json(encoded);
        ASSERT_EQ(decoded, tl);
        ASSERT_EQ(decoded.video_tracks()[0].soloed(), true);
        ASSERT_EQ(decoded.video_tracks()[0].locked(), true);
        ASSERT_EQ(decoded.audio_tracks()[0].muted(), true);
        ASSERT_EQ(decoded.video_tracks()[0].items()[0].enabled(), false);
        ASSERT_EQ(decoded.duration(), tl.duration());

        auto expect_throw = [](const std::string& bad_json) {
            bool threw = false;
            try {
                (void)ave::Timeline::from_json(bad_json);
            } catch (const std::exception&) {
                threw = true;
            }
            return threw;
        };

        // Malformed JSON
        ASSERT_TRUE(expect_throw("{not valid json"));
        // Unsupported schema version
        ASSERT_TRUE(expect_throw(R"({"schema_version":"9.9.0","frame_rate":{"num":24,"den":1}})"));
        // Float instead of integer inside Rational
        ASSERT_TRUE(expect_throw(R"({"schema_version":"1.1.0","frame_rate":{"num":23.976,"den":1}})"));
        // Zero denominator in Rational
        ASSERT_TRUE(expect_throw(R"({"schema_version":"1.1.0","frame_rate":{"num":24,"den":0}})"));
        // Out-of-range opacity
        ASSERT_TRUE(expect_throw(R"({
            "schema_version":"1.1.0",
            "frame_rate":{"num":24,"den":1},
            "video_tracks":[{
                "id":"v1","type":"video",
                "items":[{"id":"i1","timeline_in":{"num":0,"den":1},"duration":{"num":1,"den":1},"source_in":{"num":0,"den":1},"opacity":1.5}]
            }]
        })"));
        // Track type mismatch
        ASSERT_TRUE(expect_throw(R"({
            "schema_version":"1.1.0",
            "frame_rate":{"num":24,"den":1},
            "video_tracks":[{"id":"v1","type":"audio","items":[]}]
        })"));
        // Overlapping items on track
        ASSERT_TRUE(expect_throw(R"({
            "schema_version":"1.1.0",
            "frame_rate":{"num":24,"den":1},
            "video_tracks":[{
                "id":"v1","type":"video",
                "items":[
                    {"id":"i1","timeline_in":{"num":0,"den":1},"duration":{"num":4,"den":1},"source_in":{"num":0,"den":1}},
                    {"id":"i2","timeline_in":{"num":2,"den":1},"duration":{"num":4,"den":1},"source_in":{"num":0,"den":1}}
                ]
            }]
        })"));
    }

    std::cout << "[TimelineTest] All timeline model assertions passed!" << std::endl;
    return 0;
}
