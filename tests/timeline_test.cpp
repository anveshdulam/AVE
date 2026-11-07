#include "ave/timeline.hpp"
#include <iostream>
#include <cassert>

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

    // 5. JSON roundtrip serialization
    {
        ave::Timeline tl("tl-json", "JSON Test", ave::Rational{30, 1});
        auto* v1 = tl.find_track_mut(tl.video_tracks()[0].id());
        v1->add_item(ave::TimelineItem("json-v1", "c1", "a1", "Item 1",
                                       ave::Rational{0, 1}, ave::Rational{8, 1},
                                       ave::Rational{2, 1}));

        std::string json = tl.to_json();
        ASSERT_TRUE(!json.empty());

        ave::Timeline restored = ave::Timeline::from_json(json);
        ASSERT_EQ(restored.id(), "tl-json");
        ASSERT_EQ(restored.name(), "JSON Test");
        ASSERT_EQ(restored.frame_rate(), ave::Rational(30, 1));
        ASSERT_EQ(restored.video_tracks().size(), 1);
        ASSERT_EQ(restored.video_tracks()[0].items().size(), 1);
        ASSERT_EQ(restored.video_tracks()[0].items()[0].name(), "Item 1");
    }

    std::cout << "[TimelineTest] All timeline model assertions passed!" << std::endl;
    return 0;
}
