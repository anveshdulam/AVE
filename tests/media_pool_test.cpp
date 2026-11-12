#include "ave/media_pool.hpp"

#include <iostream>
#include <cassert>
#include <stdexcept>

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

int test_media_pool_json_roundtrip_hierarchy_and_validation() {
    ave::MediaPool pool;
    const std::string root_id = pool.root_bin_id();
    const std::string bin_footage = pool.create_bin("Footage — \"A\"\\Master", root_id);
    const std::string bin_interviews = pool.create_bin("Interviews (映像)", bin_footage);
    const std::string bin_audio = pool.create_bin("Audio Stems", root_id);

    const std::filesystem::path cam_path(
        std::u8string(u8"D:\\Studio Media\\Take \"1\"\\映像\\cam_a_最終.mov"));
    ave::SourceStream vs(0, ave::StreamType::Video, "prores", ave::Rational{1001, 24000}, 2400);
    ave::VideoStreamInfo vi;
    vi.width = 3840;
    vi.height = 2160;
    vi.frame_rate = ave::Rational{24000, 1001};
    vi.pixel_format = "yuv422p10le";
    vi.bit_depth = 10;
    vs.set_video_info(vi);

    ave::MediaAsset cam_asset("asset-cam-1", cam_path, "cam_a_最終.mov\nMain", 5000000ULL, "mov",
                              ave::Rational{1001, 10}, {vs});

    ave::SourceStream as(0, ave::StreamType::Audio, "pcm_s24le", ave::Rational{1, 48000}, 960000);
    ave::AudioStreamInfo ai;
    ai.sample_rate = 48000;
    ai.channels = 2;
    ai.channel_layout = "stereo";
    ai.sample_format = "s32";
    ai.bit_depth = 24;
    as.set_audio_info(ai);
    ave::MediaAsset wav_asset("asset-wav-1", "D:\\Studio Media\\boom_01.wav", "boom_01.wav", 1920000ULL, "wav",
                              ave::Rational{20, 1}, {as});

    pool.register_asset(cam_asset);
    pool.register_asset(wav_asset);

    const std::string c1 = pool.create_clip("asset-cam-1", bin_interviews, "Intro \"Hero\"");
    pool.find_clip_mut(c1)->set_source_range(ave::Rational{5005, 1000}, ave::Rational{25025, 1000});
    pool.find_clip_mut(c1)->set_color_tag("Warm Orange");

    const std::string c2 = pool.create_clip("asset-cam-1", bin_interviews, "Cutaway\tLine\nTwo");
    pool.find_clip_mut(c2)->set_source_range(ave::Rational{30, 1}, ave::Rational{45, 1});
    pool.find_clip_mut(c2)->set_color_tag("Cerulean");

    const std::string c3 = pool.create_clip("asset-wav-1", bin_audio, "Boom Clean");
    pool.find_clip_mut(c3)->set_color_tag("Forest Green");

    const std::string json_a = pool.to_json();
    const std::string json_b = pool.to_json();
    ASSERT_EQ(json_a, json_b);

    const ave::MediaPool restored = ave::MediaPool::from_json(json_a);
    ASSERT_EQ(restored, pool);
    ASSERT_EQ(restored.root_bin_id(), root_id);
    ASSERT_EQ(restored.asset_count(), 2u);
    ASSERT_EQ(restored.find_asset("asset-cam-1")->file_path().wstring(), cam_path.wstring());

    const auto* r_root = restored.find_bin(root_id);
    ASSERT_TRUE(r_root != nullptr);
    ASSERT_EQ(r_root->child_bin_ids().size(), 2u);
    ASSERT_EQ(r_root->child_bin_ids()[0], bin_footage);
    ASSERT_EQ(r_root->child_bin_ids()[1], bin_audio);

    const auto* r_interviews = restored.find_bin(bin_interviews);
    ASSERT_TRUE(r_interviews != nullptr);
    ASSERT_EQ(r_interviews->parent_id(), bin_footage);
    ASSERT_EQ(r_interviews->clip_ids().size(), 2u);
    ASSERT_EQ(r_interviews->clip_ids()[0], c1);
    ASSERT_EQ(r_interviews->clip_ids()[1], c2);

    const auto* rc1 = restored.find_clip(c1);
    ASSERT_TRUE(rc1 != nullptr);
    ASSERT_EQ(rc1->color_tag(), "Warm Orange");
    ASSERT_EQ(rc1->source_in(), ave::Rational(5005, 1000));
    ASSERT_EQ(rc1->source_out(), ave::Rational(25025, 1000));

    auto expect_throw = [](const std::string& bad_json) {
        try {
            (void)ave::MediaPool::from_json(bad_json);
            return false;
        } catch (const std::exception&) {
            return true;
        }
    };

    // Malformed JSON & bad version
    ASSERT_TRUE(expect_throw("{not valid"));
    ASSERT_TRUE(expect_throw(R"({"schema_version":"9.9.0"})"));
    // Duplicate asset ID
    ASSERT_TRUE(expect_throw(R"({"assets":[{"id":"a1"},{"id":"a1"}]})"));
    // Broken asset reference in clip
    ASSERT_TRUE(expect_throw(R"({"assets":[],"clips":[{"id":"c1","asset_id":"missing-asset"}]})"));
    // Duplicate clip ID
    ASSERT_TRUE(expect_throw(R"({"assets":[{"id":"a1"}],"clips":[{"id":"c1","asset_id":"a1"},{"id":"c1","asset_id":"a1"}]})"));
    // Negative / inverted subclip range
    ASSERT_TRUE(expect_throw(R"({"assets":[{"id":"a1"}],"clips":[{"id":"c1","asset_id":"a1","source_in":{"num":5,"den":1},"source_out":{"num":2,"den":1}}]})"));
    ASSERT_TRUE(expect_throw(R"({"assets":[{"id":"a1"}],"clips":[{"id":"c1","asset_id":"a1","source_in":{"num":-1,"den":1},"source_out":{"num":2,"den":1}}]})"));
    // Invalid parent bin reference
    ASSERT_TRUE(expect_throw(R"({"root_bin_id":"r","bins":[{"id":"r","parent_id":""},{"id":"b1","parent_id":"no-such-bin"}]})"));
    // Cycle in bin hierarchy
    ASSERT_TRUE(expect_throw(R"({"root_bin_id":"r","bins":[{"id":"r","parent_id":"","child_bin_ids":[]},{"id":"b1","parent_id":"b2","child_bin_ids":["b2"]},{"id":"b2","parent_id":"b1","child_bin_ids":["b1"]}]})"));
    // Duplicate clip membership across bins
    ASSERT_TRUE(expect_throw(R"({
        "root_bin_id":"r",
        "assets":[{"id":"a1"}],
        "clips":[{"id":"c1","asset_id":"a1"}],
        "bins":[
            {"id":"r","parent_id":"","clip_ids":["c1"],"child_bin_ids":["b1"]},
            {"id":"b1","parent_id":"r","clip_ids":["c1"],"child_bin_ids":[]}
        ]
    })"));

    return 0;
}

int test_m13a_media_pool_mutators_revisions_and_digest() {
    ave::MediaPool pool;
    int callback_count = 0;
    const auto listener_id = pool.add_change_listener_with_id([&]() { ++callback_count; });

    ASSERT_EQ(pool.mutation_revision(), 0u);
    const uint64_t empty_digest = pool.content_digest();

    // Register asset -> bumps revision & digest, notifies listener
    ave::MediaAsset a1("asset-1", "D:/footage/cam1.mp4", "cam1.mp4", 4096, "mp4", ave::Rational{20, 1}, {});
    pool.register_asset(a1);
    ASSERT_EQ(pool.mutation_revision(), 1u);
    ASSERT_EQ(callback_count, 1);
    ASSERT_TRUE(pool.content_digest() != empty_digest);

    // Re-register identical asset -> idempotent no-op (no bump, no notification)
    const uint64_t rev_before_idem = pool.mutation_revision();
    pool.register_asset(a1);
    ASSERT_EQ(pool.mutation_revision(), rev_before_idem);
    ASSERT_EQ(callback_count, 1);

    // Bin creation, rename, move, cycle/root/self rejection, and no-op move
    const std::string bin_a = pool.create_bin("Bin A");
    const std::string bin_b = pool.create_bin("Bin B");
    ASSERT_EQ(pool.mutation_revision(), 3u);
    ASSERT_EQ(callback_count, 3);

    ASSERT_TRUE(pool.rename_bin(bin_a, "Bin A")); // Same name -> no-op
    ASSERT_EQ(pool.mutation_revision(), 3u);
    ASSERT_TRUE(!pool.rename_bin(bin_a, "")); // Empty -> rejected
    ASSERT_EQ(pool.mutation_revision(), 3u);
    ASSERT_TRUE(pool.rename_bin(bin_a, "Dailies"));
    ASSERT_EQ(pool.mutation_revision(), 4u);
    ASSERT_EQ(pool.find_bin(bin_a)->name(), "Dailies");

    ASSERT_TRUE(pool.move_bin(bin_b, pool.root_bin_id())); // Already under root -> no-op
    ASSERT_EQ(pool.mutation_revision(), 4u);
    ASSERT_TRUE(!pool.move_bin(pool.root_bin_id(), bin_a)); // Cannot move root
    ASSERT_TRUE(!pool.move_bin(bin_a, bin_a)); // Self parent
    ASSERT_EQ(pool.mutation_revision(), 4u);

    ASSERT_TRUE(pool.move_bin(bin_b, bin_a));
    ASSERT_EQ(pool.mutation_revision(), 5u);
    ASSERT_TRUE(!pool.move_bin(bin_a, bin_b)); // Cycle A -> B -> A rejected
    ASSERT_EQ(pool.mutation_revision(), 5u);

    // Subclip creation & mutators
    const std::string clip_id = pool.create_clip("asset-1", bin_b, "Shot 01");
    ASSERT_EQ(pool.mutation_revision(), 6u);

    // Invalid range must throw WITHOUT bumping revision
    bool threw_invalid_range = false;
    try {
        (void)pool.set_clip_source_range(clip_id, ave::Rational{15, 1}, ave::Rational{5, 1});
    } catch (const std::invalid_argument&) {
        threw_invalid_range = true;
    }
    ASSERT_TRUE(threw_invalid_range);
    ASSERT_EQ(pool.mutation_revision(), 6u);

    ASSERT_TRUE(pool.set_clip_source_range(clip_id, ave::Rational{2, 1}, ave::Rational{12, 1}));
    ASSERT_EQ(pool.mutation_revision(), 7u);
    ASSERT_TRUE(pool.set_clip_source_range(clip_id, ave::Rational{2, 1}, ave::Rational{12, 1})); // Identical -> no-op
    ASSERT_EQ(pool.mutation_revision(), 7u);

    ASSERT_TRUE(pool.rename_clip(clip_id, "Shot 01 Hero"));
    ASSERT_TRUE(pool.set_clip_color_tag(clip_id, "Amber"));
    ASSERT_EQ(pool.mutation_revision(), 9u);

    // Relink path checks: normalized same path is no-op; different path bumps
    ASSERT_TRUE(pool.relink_asset_path("asset-1", "D:/footage/./cam1.mp4", 4096));
    ASSERT_EQ(pool.mutation_revision(), 9u);
    ASSERT_TRUE(pool.relink_asset_path("asset-1", "E:/archive/cam1.mp4", 8192));
    ASSERT_EQ(pool.mutation_revision(), 10u);
    ASSERT_EQ(pool.find_asset("asset-1")->file_size_bytes(), 8192u);

    // Digest detects raw find_clip_mut() mutation even if caller didn't use pool method
    const uint64_t digest_before_raw = pool.content_digest();
    pool.find_clip_mut(clip_id)->set_color_tag("Violet");
    ASSERT_TRUE(pool.content_digest() != digest_before_raw);

    // Equality compares persisted domain state ONLY (ignores mutation_revision & listeners)
    const ave::MediaPool rt = ave::MediaPool::from_json(pool.to_json());
    ASSERT_EQ(rt.mutation_revision(), 0u);
    ASSERT_EQ(rt, pool);
    ASSERT_EQ(rt.content_digest(), pool.content_digest());

    // remove_change_listener stops notifications; remove_asset removes dependent clips cleanly
    pool.remove_change_listener(listener_id);
    const int count_at_detach = callback_count;
    ASSERT_TRUE(pool.remove_asset("asset-1"));
    ASSERT_EQ(callback_count, count_at_detach);
    ASSERT_EQ(pool.asset_count(), 0u);
    ASSERT_TRUE(pool.find_clip(clip_id) == nullptr);

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

    if (test_media_pool_json_roundtrip_hierarchy_and_validation() != 0) return 1;
    std::cout << "  [PASS] test_media_pool_json_roundtrip_hierarchy_and_validation" << std::endl;

    if (test_m13a_media_pool_mutators_revisions_and_digest() != 0) return 1;
    std::cout << "  [PASS] test_m13a_media_pool_mutators_revisions_and_digest" << std::endl;

    std::cout << "All MediaPool tests passed successfully." << std::endl;
    return 0;
}

