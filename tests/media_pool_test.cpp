#include "ave/media_pool.hpp"

#include <iostream>
#include <cassert>

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return 1; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if (!((a) == (b))) { \
        std::cerr << "Assertion failed: (" #a " == " #b ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return 1; \
    } \
} while(0)

int test_pool_init_and_asset_registration() {
    ave::MediaPool pool;
    ASSERT_TRUE(!pool.root_bin_id().empty());

    const auto* root_bin = pool.find_bin(pool.root_bin_id());
    ASSERT_TRUE(root_bin != nullptr);
    ASSERT_EQ(root_bin->name(), "Master Bin");
    ASSERT_EQ(pool.asset_count(), 0);

    // Create a media asset
    std::string asset_id = ave::UUID::generate();
    std::filesystem::path path = "D:/footage/cam_a.mov";
    ave::Rational duration{120, 1}; // 2 minutes

    std::vector<ave::SourceStream> streams;
    ave::SourceStream vs(0, ave::StreamType::Video, "prores", ave::Rational{1, 24}, 2880);
    streams.push_back(vs);

    ave::MediaAsset asset(asset_id, path, "cam_a.mov", 1024 * 1024 * 500, "mov", duration, streams);
    pool.register_asset(asset);

    ASSERT_EQ(pool.asset_count(), 1);
    const auto* found = pool.find_asset(asset_id);
    ASSERT_TRUE(found != nullptr);
    ASSERT_EQ(found->display_name(), "cam_a.mov");

    return 0;
}

int test_bin_hierarchy() {
    ave::MediaPool pool;

    std::string footage_bin_id = pool.create_bin("Footage");
    ASSERT_TRUE(!footage_bin_id.empty());
    ASSERT_TRUE(pool.find_bin(footage_bin_id) != nullptr);
    ASSERT_EQ(pool.find_bin(footage_bin_id)->parent_id(), pool.root_bin_id());

    std::string interviews_bin_id = pool.create_bin("Interviews", footage_bin_id);
    ASSERT_TRUE(pool.find_bin(interviews_bin_id) != nullptr);
    ASSERT_EQ(pool.find_bin(interviews_bin_id)->parent_id(), footage_bin_id);

    // Verify parent contains child
    const auto* p_bin = pool.find_bin(footage_bin_id);
    ASSERT_EQ(p_bin->child_bin_ids().size(), 1);
    ASSERT_EQ(p_bin->child_bin_ids()[0], interviews_bin_id);

    return 0;
}

int test_clip_creation_and_subclipping() {
    ave::MediaPool pool;

    std::string asset_id = ave::UUID::generate();
    ave::MediaAsset asset(asset_id, "D:/footage/clip1.mov", "clip1.mov", 1000, "mov", ave::Rational{60, 1}, {});
    pool.register_asset(asset);

    // Default clip takes full range
    std::string clip_id = pool.create_clip(asset_id);
    const auto* clip = pool.find_clip(clip_id);
    ASSERT_TRUE(clip != nullptr);
    ASSERT_EQ(clip->name(), "clip1.mov");
    ASSERT_EQ(clip->source_in(), ave::Rational(0, 1));
    ASSERT_EQ(clip->source_out(), ave::Rational(60, 1));
    ASSERT_EQ(clip->duration(), ave::Rational(60, 1));

    // Non-destructive sub-clipping
    auto* mut_clip = pool.find_clip_mut(clip_id);
    ASSERT_TRUE(mut_clip != nullptr);
    mut_clip->set_source_range(ave::Rational(10, 1), ave::Rational(25, 1));
    ASSERT_EQ(mut_clip->source_in(), ave::Rational(10, 1));
    ASSERT_EQ(mut_clip->source_out(), ave::Rational(25, 1));
    ASSERT_EQ(mut_clip->duration(), ave::Rational(15, 1));

    // Verify error on invalid range
    bool threw = false;
    try {
        mut_clip->set_source_range(ave::Rational(30, 1), ave::Rational(10, 1));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    ASSERT_TRUE(threw);

    return 0;
}

int test_clip_movement_and_search() {
    ave::MediaPool pool;

    std::string asset_id = ave::UUID::generate();
    ave::MediaAsset asset(asset_id, "D:/footage/interview_01.mp4", "interview_01.mp4", 5000, "mp4", ave::Rational{100, 1}, {});
    pool.register_asset(asset);

    std::string bin_id = pool.create_bin("Interviews");
    std::string clip_id = pool.create_clip(asset_id, pool.root_bin_id(), "Host Interview");

    // Clip starts in root bin
    ASSERT_EQ(pool.find_bin(pool.root_bin_id())->clip_ids().size(), 1);
    ASSERT_EQ(pool.find_bin(bin_id)->clip_ids().size(), 0);

    // Move clip to Interviews bin
    ASSERT_TRUE(pool.move_clip(clip_id, bin_id));
    ASSERT_EQ(pool.find_bin(pool.root_bin_id())->clip_ids().size(), 0);
    ASSERT_EQ(pool.find_bin(bin_id)->clip_ids().size(), 1);
    ASSERT_EQ(pool.find_bin(bin_id)->clip_ids()[0], clip_id);

    // Set color tag
    pool.find_clip_mut(clip_id)->set_color_tag("Warm Orange");

    // Search tests
    auto res_by_name = pool.search_clips("host");
    ASSERT_EQ(res_by_name.size(), 1);
    ASSERT_EQ(res_by_name[0]->id(), clip_id);

    auto res_by_color = pool.search_clips("orange");
    ASSERT_EQ(res_by_color.size(), 1);
    ASSERT_EQ(res_by_color[0]->id(), clip_id);

    auto res_empty = pool.search_clips("nonexistent");
    ASSERT_EQ(res_empty.size(), 0);

    // JSON serialization verification
    std::string json_str = pool.to_json();
    ASSERT_TRUE(!json_str.empty());
    ASSERT_TRUE(json_str.find("Host Interview") != std::string::npos);
    ASSERT_TRUE(json_str.find("Warm Orange") != std::string::npos);

    return 0;
}

int main() {
    std::cout << "Running MediaPool Unit Tests..." << std::endl;

    if (test_pool_init_and_asset_registration() != 0) return 1;
    std::cout << "  [PASS] test_pool_init_and_asset_registration" << std::endl;

    if (test_bin_hierarchy() != 0) return 1;
    std::cout << "  [PASS] test_bin_hierarchy" << std::endl;

    if (test_clip_creation_and_subclipping() != 0) return 1;
    std::cout << "  [PASS] test_clip_creation_and_subclipping" << std::endl;

    if (test_clip_movement_and_search() != 0) return 1;
    std::cout << "  [PASS] test_clip_movement_and_search" << std::endl;

    std::cout << "All MediaPool tests passed successfully." << std::endl;
    return 0;
}

