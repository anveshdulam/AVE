#include "ave/project.hpp"
#include "ave/project_recovery.hpp"
#include "ave/otio_interchange.hpp"
#include "ave/asset_relinker.hpp"
#include "ave/command.hpp"

#include <atomic>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <cassert>
#include <thread>

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

int main(int argc, char* argv[]) {
    std::cout << "Running Project Persistence & OTIO Interchange Tests..." << std::endl;

    std::filesystem::path fixture_path = (argc > 1) ? argv[1] : "tests/fixtures/sample_test.mp4";
    if (!std::filesystem::exists(fixture_path)) {
        fixture_path = std::filesystem::current_path() / "tests/fixtures/sample_test.mp4";
    }
    ASSERT_TRUE(std::filesystem::exists(fixture_path));

    // 1. Test Native Project Creation & Command Bus Integration
    std::cout << "  Testing Project Creation & Command Bus..." << std::endl;
    ave::Project project("Persistence Master");
    project.timeline_mut().set_name("Persistence Master");
    ASSERT_EQ(project.schema_version(), ave::Project::CURRENT_SCHEMA_VERSION);
    ASSERT_EQ(project.schema_version(), "1.2.0");
    ASSERT_EQ(project.name(), "Persistence Master");
    ASSERT_FALSE(project.is_dirty());

    // Register asset in MediaPool
    std::string asset_id = "asset-p1";
    ave::MediaAsset asset(asset_id, fixture_path, "sample_test.mp4", 48796, "mp4", ave::Rational{1, 1}, {});
    project.media_pool_mut().register_asset(std::move(asset));

    // Add V2 track and exercise track/clip persistence fidelity
    project.timeline_mut().add_video_track("V2");
    std::string v1_id = project.timeline().video_tracks()[0].id();
    std::string v2_id = project.timeline().video_tracks()[1].id();
    auto* v2_mut = project.timeline_mut().find_track_mut(v2_id);
    ASSERT_TRUE(v2_mut != nullptr);
    v2_mut->set_soloed(true);
    v2_mut->set_locked(true);

    // Insert clip via Project Command Bus
    ave::TimelineItem item1("item-1", "c1", asset_id, "Background Clip",
                            ave::Rational{0, 1}, ave::Rational{1, 1}, ave::Rational{0, 1});
    item1.set_scale(1.0f, 1.0f);
    item1.set_opacity(0.95f);

    auto ins_cmd = std::make_unique<ave::InsertClipCommand>(v1_id, item1);
    auto res_ins = project.execute_command(std::move(ins_cmd));
    ASSERT_TRUE(res_ins.is_success());
    ASSERT_TRUE(project.is_dirty());

    // 2. Test Atomic Save and Load (.aveproj)
    std::cout << "  Testing Atomic Save & Load (.aveproj)..." << std::endl;
    std::filesystem::path test_proj_path = std::filesystem::current_path() / "test_output" / "test_session.aveproj";
    bool saved = project.save(test_proj_path);
    ASSERT_TRUE(saved);
    ASSERT_FALSE(project.is_dirty());
    ASSERT_TRUE(std::filesystem::exists(test_proj_path));

    // Load back
    ave::Project loaded_proj = ave::Project::load(test_proj_path);
    ASSERT_EQ(loaded_proj.id(), project.id());
    ASSERT_EQ(loaded_proj.name(), "Persistence Master");
    ASSERT_EQ(loaded_proj.schema_version(), ave::Project::CURRENT_SCHEMA_VERSION);
    ASSERT_FALSE(loaded_proj.is_dirty());
    ASSERT_EQ(loaded_proj.timeline().video_tracks().size(), 2);
    ASSERT_EQ(loaded_proj.timeline(), project.timeline());
    ASSERT_TRUE(loaded_proj.timeline().video_tracks()[1].soloed());
    ASSERT_TRUE(loaded_proj.timeline().video_tracks()[1].locked());
    ASSERT_EQ(loaded_proj.media_pool().asset_count(), 1);
    ASSERT_EQ(loaded_proj.media_pool(), project.media_pool());

    const auto* loaded_item = loaded_proj.timeline().find_item("item-1");
    ASSERT_TRUE(loaded_item != nullptr);
    ASSERT_EQ(loaded_item->name(), "Background Clip");
    ASSERT_EQ(loaded_item->opacity(), 0.95f);

    // 3. Test OpenTimelineIO (OTIO) Export
    std::cout << "  Testing OpenTimelineIO (OTIO) Export..." << std::endl;
    ave::OtioLossReport export_report;
    std::string otio_json = ave::OtioInterchange::export_to_json(project.timeline(), project.media_pool(), export_report);
    ASSERT_TRUE(otio_json.find("\"OTIO_SCHEMA\": \"Timeline.1\"") != std::string::npos);
    ASSERT_TRUE(otio_json.find("\"OTIO_SCHEMA\": \"Clip.1\"") != std::string::npos);
    ASSERT_TRUE(otio_json.find("\"OTIO_SCHEMA\": \"ExternalReference.1\"") != std::string::npos);
    std::cout << "  OTIO Export completed. Warnings: " << export_report.warnings.size() << std::endl;

    // 4. Test OpenTimelineIO (OTIO) Import
    std::cout << "  Testing OpenTimelineIO (OTIO) Import..." << std::endl;
    ave::Timeline imported_timeline;
    ave::MediaPool imported_pool;
    ave::OtioLossReport import_report;

    bool imported = ave::OtioInterchange::import_from_json(otio_json, imported_timeline, imported_pool, import_report);
    ASSERT_TRUE(imported);
    ASSERT_EQ(imported_timeline.name(), "Persistence Master");
    ASSERT_EQ(imported_timeline.video_tracks().size(), 2);
    ASSERT_EQ(imported_timeline.video_tracks()[0].items().size(), 1);

    const auto& imp_item = imported_timeline.video_tracks()[0].items()[0];
    ASSERT_EQ(imp_item.name(), "Background Clip");
    ASSERT_EQ(imp_item.timeline_in(), ave::Rational(0, 1));
    ASSERT_EQ(imp_item.duration(), ave::Rational(1, 1));
    ASSERT_EQ(imp_item.opacity(), 0.95f);

    // 5. Test AssetRelinker
    std::cout << "  Testing AssetRelinker & Offline Media Reconciliation..." << std::endl;
    ave::MediaPool relink_pool;

    // Create asset with a missing fake path
    std::filesystem::path fake_missing = "D:/dummy/non_existent_footage/sample_test.mp4";
    ave::MediaAsset missing_asset("missing-1", fake_missing, "sample_test.mp4", 48796, "mp4", ave::Rational{1, 1}, {});
    relink_pool.register_asset(std::move(missing_asset));

    auto missing_list = ave::AssetRelinker::find_missing_assets(relink_pool);
    ASSERT_EQ(missing_list.size(), 1);
    ASSERT_EQ(missing_list[0].asset_id, "missing-1");

    // Explicit relink to fixture
    bool relinked = ave::AssetRelinker::relink_asset(relink_pool, "missing-1", fixture_path);
    ASSERT_TRUE(relinked);

    auto missing_after = ave::AssetRelinker::find_missing_assets(relink_pool);
    ASSERT_EQ(missing_after.size(), 0);

    // Directory auto-relink test
    ave::MediaPool batch_pool;
    ave::MediaAsset batch_missing("missing-batch", "C:/lost/sample_test.mp4", "sample_test.mp4", 0, "mp4", ave::Rational{1, 1}, {});
    batch_pool.register_asset(std::move(batch_missing));

    auto relink_report = ave::AssetRelinker::auto_relink_directory(batch_pool, fixture_path.parent_path());
    ASSERT_EQ(relink_report.total_checked, 1);
    ASSERT_EQ(relink_report.relinked_count, 1);
    ASSERT_EQ(relink_report.still_missing_count, 0);

    // 6. Test Versioned Fixture Loading, Legacy 1.0.0 Migration & Special-Char Roundtrip
    std::cout << "  Testing Versioned Project Fixtures & Migration..." << std::endl;
    const std::filesystem::path fixtures_projects_dir = fixture_path.parent_path() / "projects";
    ASSERT_TRUE(std::filesystem::exists(fixtures_projects_dir));

    {
        // 6a. Legacy 1.0.0 migration -> 1.1.0
        ave::Project legacy = ave::Project::load(fixtures_projects_dir / "legacy_1_0_0.aveproj");
        ASSERT_EQ(legacy.schema_version(), ave::Project::CURRENT_SCHEMA_VERSION);
        ASSERT_EQ(legacy.id(), "proj-legacy-100");
        ASSERT_EQ(legacy.name(), "Legacy 1.0.0 Project");
        ASSERT_EQ(legacy.timeline().frame_rate(), ave::Rational(24, 1));
        ASSERT_EQ(legacy.timeline().video_tracks().size(), 1);
        ASSERT_EQ(legacy.timeline().audio_tracks().size(), 1);
        ASSERT_FALSE(legacy.timeline().video_tracks()[0].soloed());
        ASSERT_FALSE(legacy.timeline().video_tracks()[0].locked());
        ASSERT_TRUE(legacy.timeline().audio_tracks()[0].muted());

        const auto* vitem = legacy.timeline().find_item("vitem-legacy-1");
        ASSERT_TRUE(vitem != nullptr);
        ASSERT_TRUE(vitem->enabled());
        ASSERT_EQ(vitem->speed(), ave::Rational(3, 2));
        ASSERT_EQ(vitem->transform_x(), 25.5f);
        ASSERT_EQ(vitem->transform_y(), -12.0f);
        ASSERT_EQ(vitem->scale_x(), 1.25f);
        ASSERT_EQ(vitem->scale_y(), 0.9f);
        ASSERT_EQ(vitem->rotation(), 15.0f);
        ASSERT_EQ(vitem->anchor_x(), 0.3f);
        ASSERT_EQ(vitem->anchor_y(), 0.7f);
        ASSERT_TRUE(std::abs(vitem->opacity() - 0.85f) < 0.0001f);
        ASSERT_EQ(vitem->blend_mode(), "Add");

        const auto* aitem = legacy.timeline().find_item("aitem-legacy-1");
        ASSERT_TRUE(aitem != nullptr);
        ASSERT_EQ(aitem->timeline_in(), ave::Rational(1, 2));
        ASSERT_EQ(aitem->duration(), ave::Rational(9, 2));
    }

    {
        // 6b. Legacy 1.1.0 project (24000/1001, solo/lock, disabled item) -> migrates to 1.2.0 + save/reload roundtrip
        ave::Project v11 = ave::Project::load(fixtures_projects_dir / "valid_1_1_0.aveproj");
        ASSERT_EQ(v11.schema_version(), ave::Project::CURRENT_SCHEMA_VERSION);
        ASSERT_EQ(v11.schema_version(), "1.2.0");
        ASSERT_EQ(v11.timeline().frame_rate(), ave::Rational(24000, 1001));
        ASSERT_EQ(v11.timeline().width(), 3840);
        ASSERT_EQ(v11.timeline().height(), 2160);
        ASSERT_EQ(v11.timeline().sample_rate(), 96000);
        ASSERT_TRUE(v11.timeline().video_tracks()[0].soloed());
        ASSERT_TRUE(v11.timeline().video_tracks()[0].locked());
        ASSERT_TRUE(v11.timeline().audio_tracks()[0].locked());
        ASSERT_EQ(v11.media_pool().asset_count(), 0);
        ASSERT_TRUE(v11.media_pool().find_bin(v11.media_pool().root_bin_id()) != nullptr);

        const auto* vitem = v11.timeline().find_item("vitem-110-1");
        ASSERT_TRUE(vitem != nullptr);
        ASSERT_FALSE(vitem->enabled());
        ASSERT_EQ(vitem->blend_mode(), "Screen");

        const std::filesystem::path rt_path = std::filesystem::current_path() / "test_output" / "valid_110_rt.aveproj";
        ASSERT_TRUE(v11.save(rt_path));
        ave::Project v11_reloaded = ave::Project::load(rt_path);
        ASSERT_EQ(v11_reloaded.timeline(), v11.timeline());
        ASSERT_EQ(v11_reloaded.media_pool(), v11.media_pool());
        ASSERT_EQ(v11_reloaded.id(), v11.id());
        ASSERT_EQ(v11_reloaded.name(), v11.name());
    }

    {
        // 6c. Optional fields omitted -> deterministic defaults
        ave::Project opt = ave::Project::load(fixtures_projects_dir / "optional_fields_missing.aveproj");
        ASSERT_EQ(opt.timeline().width(), 1920);
        ASSERT_EQ(opt.timeline().height(), 1080);
        ASSERT_EQ(opt.timeline().sample_rate(), 48000);
        ASSERT_EQ(opt.timeline().audio_tracks().size(), 0);
        ASSERT_EQ(opt.timeline().video_tracks().size(), 1);
        const auto& trk = opt.timeline().video_tracks()[0];
        ASSERT_EQ(trk.name(), "V1");
        ASSERT_FALSE(trk.muted());
        ASSERT_FALSE(trk.soloed());
        ASSERT_FALSE(trk.locked());
        const auto& it = trk.items()[0];
        ASSERT_EQ(it.name(), "Untitled Item");
        ASSERT_EQ(it.speed(), ave::Rational(1, 1));
        ASSERT_TRUE(it.enabled());
        ASSERT_EQ(it.opacity(), 1.0f);
        ASSERT_EQ(it.blend_mode(), "Over");
    }

    {
        // 6d. Unicode, Windows backslashes, quotes, newlines & tabs across save/reload
        ave::Project uni = ave::Project::load(fixtures_projects_dir / "unicode_and_special_chars.aveproj");
        ASSERT_EQ(uni.name(), "Cinematic Edit — \"Final Cut\" 日本語 / हिन्दी");
        ASSERT_EQ(uni.timeline().name(), "Seq: \"Scene\\Take_01\"\nLine2\tTabbed");
        ASSERT_EQ(uni.timeline().video_tracks()[0].name(), "Track \\\"Alpha\\\" — 映像");
        ASSERT_EQ(uni.timeline().video_tracks()[0].items()[0].name(),
                  "D:\\Footage\\Take \"A\"\\shot_01_最終.mov\nNote:\tOK");

        const std::filesystem::path uni_rt = std::filesystem::current_path() / "test_output" / "unicode_rt.aveproj";
        ASSERT_TRUE(uni.save(uni_rt));
        ave::Project uni_reloaded = ave::Project::load(uni_rt);
        ASSERT_EQ(uni_reloaded.name(), uni.name());
        ASSERT_EQ(uni_reloaded.timeline(), uni.timeline());
    }

    {
        // 6e. Malformed JSON & future unsupported schema_version
        bool malformed_threw = false;
        try {
            (void)ave::Project::load(fixtures_projects_dir / "malformed.aveproj");
        } catch (const std::exception& e) {
            malformed_threw = (std::string(e.what()).find("parse") != std::string::npos);
        }
        ASSERT_TRUE(malformed_threw);

        bool future_threw = false;
        try {
            (void)ave::Project::load(fixtures_projects_dir / "future_unsupported_schema.aveproj");
        } catch (const std::exception& e) {
            future_threw = (std::string(e.what()).find("schema_version") != std::string::npos);
        }
        ASSERT_TRUE(future_threw);

        bool missing_file_threw = false;
        try {
            (void)ave::Project::load(fixtures_projects_dir / "does_not_exist.aveproj");
        } catch (const std::exception& e) {
            missing_file_threw = (std::string(e.what()).find("I/O") != std::string::npos);
        }
        ASSERT_TRUE(missing_file_threw);
    }

    // 7. Test Failed Save Preserves Existing Valid Project File & Dirty State
    std::cout << "  Testing Failed Save Preserves Existing Valid Project..." << std::endl;
    {
        const std::filesystem::path guarded_dir = std::filesystem::current_path() / "test_output" / "guarded_save";
        std::filesystem::create_directories(guarded_dir);
        const std::filesystem::path guarded_file = guarded_dir / "safe_project.aveproj";

        ave::Project safe_proj("Original Valid State");
        ASSERT_TRUE(safe_proj.save(guarded_file));
        ASSERT_FALSE(safe_proj.is_dirty());
        ASSERT_EQ(safe_proj.file_path(), guarded_file);

        // Mutate project so it becomes dirty
        std::string g_v1 = safe_proj.timeline().video_tracks()[0].id();
        ave::TimelineItem dirty_clip("dirty-1", "c-d", "a-d", "Unsaved Clip",
                                     ave::Rational{0, 1}, ave::Rational{3, 1}, ave::Rational{0, 1});
        ASSERT_TRUE(safe_proj.execute_command(std::make_unique<ave::InsertClipCommand>(g_v1, dirty_clip)).is_success());
        ASSERT_TRUE(safe_proj.is_dirty());

        // Attempting to save into a destination whose parent is a regular file must fail cleanly
        const std::filesystem::path blocker_file = guarded_dir / "not_a_directory";
        {
            std::ofstream ofs(blocker_file, std::ios::out | std::ios::binary | std::ios::trunc);
            ofs << "blocker";
        }
        ASSERT_FALSE(safe_proj.save(blocker_file / "broken.aveproj"));
        ASSERT_TRUE(safe_proj.is_dirty());
        ASSERT_EQ(safe_proj.file_path(), guarded_file);
        ASSERT_FALSE(std::filesystem::exists(guarded_file.string() + ".tmp"));

        // Make guarded_file a directory/read-only collision test: on Windows, std::filesystem::rename fails
        // when target is an existing directory or when destination path is invalid; also check read-only file
        // cannot be corrupted on failure and .tmp is cleaned up.
        std::filesystem::path dir_target = guarded_dir / "target_is_dir.aveproj";
        std::filesystem::create_directories(dir_target);
        ASSERT_FALSE(safe_proj.save(dir_target));
        ASSERT_FALSE(std::filesystem::exists(dir_target.string() + ".tmp"));
        ASSERT_TRUE(safe_proj.is_dirty());
        ASSERT_EQ(safe_proj.file_path(), guarded_file);

        // Attempting to save directly over existing guarded_file when staging (.tmp) fails
        // (simulate blocked .tmp path via a directory collision at <guarded_file>.tmp)
        // must fail cleanly and leave the original valid guarded_file untouched on disk.
        const std::filesystem::path blocked_tmp = std::filesystem::path(guarded_file.string() + ".tmp");
        std::filesystem::create_directories(blocked_tmp / "nested");
        ASSERT_FALSE(safe_proj.save(guarded_file));
        ASSERT_TRUE(safe_proj.is_dirty());
        ASSERT_EQ(safe_proj.file_path(), guarded_file);
        std::filesystem::remove_all(blocked_tmp);

        // Verify original valid file on disk is untouched and reloads cleanly
        ave::Project reloaded_safe = ave::Project::load(guarded_file);
        ASSERT_EQ(reloaded_safe.name(), "Original Valid State");
        ASSERT_TRUE(reloaded_safe.timeline().find_item("dirty-1") == nullptr);

        // Untitled dirty project failing initial save preserves empty file_path and dirty state, then saves cleanly once fixed
        ave::Project untitled_dirty("Untitled M13B");
        untitled_dirty.media_pool_mut().create_bin("Scratch Bin");
        ASSERT_TRUE(untitled_dirty.is_dirty());
        ASSERT_TRUE(untitled_dirty.file_path().empty());
        ASSERT_FALSE(untitled_dirty.save(blocker_file / "cannot_create.aveproj"));
        ASSERT_TRUE(untitled_dirty.is_dirty());
        ASSERT_TRUE(untitled_dirty.file_path().empty());
        const std::filesystem::path rescued_file = guarded_dir / "rescued_untitled.aveproj";
        ASSERT_TRUE(untitled_dirty.save(rescued_file));
        ASSERT_FALSE(untitled_dirty.is_dirty());
        ASSERT_EQ(untitled_dirty.file_path(), rescued_file);
    }

    // 8. M1.2B MediaPool & MediaAsset Fixtures, Organization Round-Trip & Error Rejection
    std::cout << "  Testing M1.2B MediaPool Persistence Fixtures & Invariants..." << std::endl;
    {
        // 8a. Valid 1.2.0 Project + Timeline + MediaPool (multi-asset, nested bins, subclips, color tags)
        ave::Project p120 = ave::Project::load(fixtures_projects_dir / "valid_1_2_0.aveproj");
        ASSERT_EQ(p120.schema_version(), "1.2.0");
        ASSERT_EQ(p120.id(), "proj-valid-120");
        ASSERT_EQ(p120.media_pool().asset_count(), 2u);
        ASSERT_EQ(p120.media_pool().root_bin_id(), "bin-root-120");

        const auto* cam = p120.media_pool().find_asset("asset-cam-a");
        const auto* wav = p120.media_pool().find_asset("asset-boom-wav");
        ASSERT_TRUE(cam != nullptr && wav != nullptr);
        ASSERT_EQ(cam->file_path(), std::filesystem::path("D:\\Media\\Studio\\CamA_001.mov"));
        ASSERT_EQ(cam->duration(), ave::Rational(1001, 10));
        ASSERT_TRUE(cam->primary_video_stream() != nullptr);
        ASSERT_EQ(cam->primary_video_stream()->video_info()->width, 3840);
        ASSERT_EQ(cam->primary_video_stream()->video_info()->frame_rate, ave::Rational(24000, 1001));
        ASSERT_TRUE(wav->primary_audio_stream() != nullptr);
        ASSERT_EQ(wav->primary_audio_stream()->audio_info()->sample_rate, 96000);

        // Verify nested bins and ordering
        const auto* root_bin = p120.media_pool().find_bin("bin-root-120");
        const auto* vid_bin = p120.media_pool().find_bin("bin-video-120");
        const auto* int_bin = p120.media_pool().find_bin("bin-interviews-120");
        const auto* broll_bin = p120.media_pool().find_bin("bin-broll-120");
        ASSERT_TRUE(root_bin && vid_bin && int_bin && broll_bin);
        ASSERT_EQ(root_bin->child_bin_ids().size(), 2u);
        ASSERT_EQ(root_bin->child_bin_ids()[0], "bin-video-120");
        ASSERT_EQ(root_bin->child_bin_ids()[1], "bin-audio-120");
        ASSERT_EQ(vid_bin->parent_id(), "bin-root-120");
        ASSERT_EQ(vid_bin->child_bin_ids().size(), 2u);
        ASSERT_EQ(vid_bin->child_bin_ids()[0], "bin-interviews-120");
        ASSERT_EQ(vid_bin->child_bin_ids()[1], "bin-broll-120");
        ASSERT_EQ(int_bin->clip_ids().size(), 1u);
        ASSERT_EQ(int_bin->clip_ids()[0], "mclip-sub-1");

        // Verify subclips + color tags
        const auto* sub1 = p120.media_pool().find_clip("mclip-sub-1");
        const auto* sub2 = p120.media_pool().find_clip("mclip-sub-2");
        const auto* sfx1 = p120.media_pool().find_clip("mclip-sfx-1");
        ASSERT_TRUE(sub1 && sub2 && sfx1);
        ASSERT_EQ(sub1->color_tag(), "Orange");
        ASSERT_EQ(sub1->source_in(), ave::Rational(10, 1));
        ASSERT_EQ(sub1->source_out(), ave::Rational(25, 1));
        ASSERT_EQ(sub2->color_tag(), "Blue");
        ASSERT_EQ(sub2->source_in(), ave::Rational(1001, 20));
        ASSERT_EQ(sub2->source_out(), ave::Rational(1601, 20));
        ASSERT_EQ(sfx1->color_tag(), "Green");

        // Full save & reload round-trip equality across Project, Timeline, and MediaPool
        const std::filesystem::path rt120 = std::filesystem::current_path() / "test_output" / "valid_120_rt.aveproj";
        ASSERT_TRUE(p120.save(rt120));
        ave::Project p120_rt = ave::Project::load(rt120);
        ASSERT_EQ(p120_rt.id(), p120.id());
        ASSERT_EQ(p120_rt.name(), p120.name());
        ASSERT_EQ(p120_rt.schema_version(), "1.2.0");
        ASSERT_EQ(p120_rt.timeline(), p120.timeline());
        ASSERT_EQ(p120_rt.media_pool(), p120.media_pool());
    }

    {
        // 8b. Unicode asset/clip/bin names & Windows backslash/quote/space paths
        ave::Project p_uni = ave::Project::load(fixtures_projects_dir / "media_unicode_and_windows_paths.aveproj");
        const auto* a = p_uni.media_pool().find_asset("asset-uni-win");
        ASSERT_TRUE(a != nullptr);
        const std::filesystem::path expected_win_path(
            std::u8string(u8"D:\\Studio Projects\\Take \"A\"\\映像\\shot_01_最終_हिन्दी.mov"));
        ASSERT_EQ(a->file_path().wstring(), expected_win_path.wstring());
        ASSERT_EQ(a->display_name(), "Shot \"Alpha\" — 最終 (हिन्दी)\nTake 2");
        ASSERT_EQ(p_uni.media_pool().find_clip("clip-uni-1")->name(), "Subclip \"A\"\\Cut\t— 映像");
        ASSERT_EQ(p_uni.media_pool().find_bin("bin-uni-sub")->name(), "映像フォルダ — \"हिन्दी\"\\Dailies");

        const std::filesystem::path rt_uni = std::filesystem::current_path() / "test_output" / "media_uni_rt.aveproj";
        ASSERT_TRUE(p_uni.save(rt_uni));
        ave::Project p_uni_rt = ave::Project::load(rt_uni);
        ASSERT_EQ(p_uni_rt.timeline(), p_uni.timeline());
        ASSERT_EQ(p_uni_rt.media_pool(), p_uni.media_pool());
        ASSERT_EQ(p_uni_rt.media_pool().find_asset("asset-uni-win")->file_path().wstring(),
                  expected_win_path.wstring());
    }

    {
        // 8c. Missing optional media fields apply deterministic defaults & reconcile hierarchy
        ave::Project p_opt = ave::Project::load(fixtures_projects_dir / "media_optional_fields_missing.aveproj");
        ASSERT_EQ(p_opt.media_pool().root_bin_id(), "bin-root-opt");
        ASSERT_EQ(p_opt.media_pool().asset_count(), 1u);
        const auto* asset_opt = p_opt.media_pool().find_asset("asset-opt-1");
        ASSERT_TRUE(asset_opt != nullptr);
        ASSERT_EQ(asset_opt->display_name(), "minimal_clip.mp4");
        ASSERT_EQ(asset_opt->container_format(), "unknown");
        ASSERT_EQ(asset_opt->streams().size(), 1u);
        ASSERT_EQ(asset_opt->streams()[0].type(), ave::StreamType::Video);
        const auto* root_opt = p_opt.media_pool().find_bin("bin-root-opt");
        ASSERT_TRUE(root_opt != nullptr);
        ASSERT_EQ(root_opt->child_bin_ids().size(), 1u);
        ASSERT_EQ(root_opt->child_bin_ids()[0], "bin-child-opt");
        const auto* clip_opt = p_opt.media_pool().find_clip("clip-opt-1");
        ASSERT_TRUE(clip_opt != nullptr);
        ASSERT_EQ(clip_opt->color_tag(), "default");
    }

    {
        // 8d. Legacy 1.0.0 and 1.1.0 without media_pool migrate deterministically to empty MediaPool
        ave::Project l10_a = ave::Project::load(fixtures_projects_dir / "legacy_1_0_0_no_media.aveproj");
        ave::Project l10_b = ave::Project::load(fixtures_projects_dir / "legacy_1_0_0_no_media.aveproj");
        ASSERT_EQ(l10_a.schema_version(), "1.2.0");
        ASSERT_EQ(l10_a.media_pool().asset_count(), 0u);
        ASSERT_EQ(l10_a.media_pool(), l10_b.media_pool());
        ASSERT_EQ(l10_a.to_json(), l10_b.to_json());

        ave::Project l11_a = ave::Project::load(fixtures_projects_dir / "legacy_1_1_0_no_media.aveproj");
        ave::Project l11_b = ave::Project::load(fixtures_projects_dir / "legacy_1_1_0_no_media.aveproj");
        ASSERT_EQ(l11_a.schema_version(), "1.2.0");
        ASSERT_EQ(l11_a.timeline().frame_rate(), ave::Rational(30000, 1001));
        ASSERT_TRUE(l11_a.timeline().video_tracks()[0].locked());
        ASSERT_EQ(l11_a.media_pool().asset_count(), 0u);
        ASSERT_EQ(l11_a.media_pool(), l11_b.media_pool());
    }

    {
        // 8e. Negative MediaPool fixture rejections
        auto expect_load_fail = [](const std::filesystem::path& p) {
            try {
                (void)ave::Project::load(p);
                return false;
            } catch (const std::exception&) {
                return true;
            }
        };
        ASSERT_TRUE(expect_load_fail(fixtures_projects_dir / "media_missing_asset_ref.aveproj"));
        ASSERT_TRUE(expect_load_fail(fixtures_projects_dir / "media_duplicate_asset_id.aveproj"));
        ASSERT_TRUE(expect_load_fail(fixtures_projects_dir / "media_invalid_bin_cycle.aveproj"));
        ASSERT_TRUE(expect_load_fail(fixtures_projects_dir / "media_malformed_pool.aveproj"));
    }

    // 9. M1.3A Unified Dirty-State & Persistence Mutation Invariants
    std::cout << "  Testing M1.3A Unified Dirty-State & Persistence Mutation Invariants..." << std::endl;
    {
        const std::filesystem::path m13a_dir = std::filesystem::current_path() / "test_output" / "m13a_dirty";
        std::filesystem::remove_all(m13a_dir);
        std::filesystem::create_directories(m13a_dir);

        // 9a. Empty new project starts clean; change listener fires on semantic mutations
        ave::Project p("M13A Clean Start");
        int listener_hits = 0;
        const auto p_lid = p.add_change_listener_with_id([&]() { ++listener_hits; });
        ASSERT_FALSE(p.is_dirty());

        // Same-name set_name is a no-op
        p.set_name("M13A Clean Start");
        ASSERT_FALSE(p.is_dirty());
        ASSERT_EQ(listener_hits, 0);

        // Real name change marks dirty & notifies
        p.set_name("M13A Renamed");
        ASSERT_TRUE(p.is_dirty());
        ASSERT_EQ(listener_hits, 1);

        // Failed timeline command on locked track must NOT flip clean -> dirty
        const std::filesystem::path p_file = m13a_dir / "m13a.aveproj";
        ASSERT_TRUE(p.save(p_file));
        ASSERT_FALSE(p.is_dirty());

        const std::string v1 = p.timeline().video_tracks()[0].id();
        p.timeline_mut().find_track_mut(v1)->set_locked(true);
        const int hits_before_locked = listener_hits;
        const ave::TimelineItem locked_item("m13a-locked", "mc-locked", "ma-locked", "Rejected",
                                            ave::Rational{0, 1}, ave::Rational{2, 1}, ave::Rational{0, 1});
        ASSERT_FALSE(p.execute_command(std::make_unique<ave::InsertClipCommand>(v1, locked_item)).is_success());
        ASSERT_FALSE(p.is_dirty());
        ASSERT_EQ(listener_hits, hits_before_locked);
        p.timeline_mut().find_track_mut(v1)->set_locked(false);

        // 9b. MediaPool mutations mark clean project dirty + save clears dirty
        const std::filesystem::path media_v1 = m13a_dir / "cam_v1.mp4";
        const std::filesystem::path media_v2 = m13a_dir / "relocated" / "cam_v1.mp4";
        std::filesystem::create_directories(media_v2.parent_path());
        std::filesystem::copy_file(fixture_path, media_v1, std::filesystem::copy_options::overwrite_existing);

        const ave::MediaAsset asset_a(
            "asset-m13a-1", media_v1, "cam_v1.mp4",
            std::filesystem::file_size(media_v1), "mp4", ave::Rational{4, 1}, {});
        p.media_pool_mut().register_asset(asset_a);
        ASSERT_TRUE(p.is_dirty());

        // Idempotent re-register does not add extra notification after save
        ASSERT_TRUE(p.save());
        ASSERT_FALSE(p.is_dirty());
        p.media_pool_mut().register_asset(asset_a);
        ASSERT_FALSE(p.is_dirty());

        // Bin create -> save -> rename -> save -> move -> save -> delete -> save
        const std::string bin1 = p.media_pool_mut().create_bin("Scene 01");
        const std::string bin2 = p.media_pool_mut().create_bin("Selects");
        ASSERT_TRUE(p.is_dirty());
        ASSERT_TRUE(p.save());
        ASSERT_FALSE(p.is_dirty());

        ASSERT_TRUE(p.media_pool_mut().rename_bin(bin1, "Scene 01 — Dailies"));
        ASSERT_TRUE(p.is_dirty());
        ASSERT_TRUE(p.save());
        ASSERT_FALSE(p.is_dirty());

        ASSERT_TRUE(p.media_pool_mut().move_bin(bin2, bin1));
        ASSERT_TRUE(p.is_dirty());
        ASSERT_TRUE(p.save());
        ASSERT_FALSE(p.is_dirty());

        // Subclip create, range update, rename, color tag, move -> dirty each time
        const std::string sub1 = p.media_pool_mut().create_clip("asset-m13a-1", bin2, "Hero Take");
        ASSERT_TRUE(p.is_dirty());
        ASSERT_TRUE(p.save());
        ASSERT_FALSE(p.is_dirty());

        ASSERT_TRUE(p.media_pool_mut().set_clip_source_range(sub1, ave::Rational{1, 1}, ave::Rational{3, 1}));
        ASSERT_TRUE(p.is_dirty());
        ASSERT_TRUE(p.save());
        ASSERT_FALSE(p.is_dirty());

        ASSERT_TRUE(p.media_pool_mut().rename_clip(sub1, "Hero Take Cut"));
        ASSERT_TRUE(p.is_dirty());
        ASSERT_TRUE(p.save());
        ASSERT_FALSE(p.is_dirty());

        ASSERT_TRUE(p.media_pool_mut().set_clip_color_tag(sub1, "Cyan"));
        ASSERT_TRUE(p.is_dirty());
        ASSERT_TRUE(p.save());
        ASSERT_FALSE(p.is_dirty());

        ASSERT_TRUE(p.media_pool_mut().move_clip(sub1, bin1));
        ASSERT_TRUE(p.is_dirty());
        ASSERT_TRUE(p.save());
        ASSERT_FALSE(p.is_dirty());

        // Raw pointer mutator safety net via content_digest()
        p.media_pool_mut().find_clip_mut(sub1)->set_color_tag("Gold");
        ASSERT_TRUE(p.is_dirty());
        ASSERT_TRUE(p.save());
        ASSERT_FALSE(p.is_dirty());

        // 9c. Relinking: same path is a no-op; missing path failure stays clean; path change marks dirty
        ASSERT_TRUE(ave::AssetRelinker::relink_asset(p.media_pool_mut(), "asset-m13a-1", media_v1));
        ASSERT_FALSE(p.is_dirty());
        ASSERT_FALSE(ave::AssetRelinker::relink_asset(p.media_pool_mut(), "asset-m13a-1", m13a_dir / "missing.mp4"));
        ASSERT_FALSE(p.is_dirty());

        const ave::RelinkReport noop_report = ave::AssetRelinker::auto_relink_directory(p.media_pool_mut(), m13a_dir, true);
        ASSERT_EQ(noop_report.total_checked, 0u);
        ASSERT_FALSE(p.is_dirty());

        std::filesystem::rename(media_v1, media_v2);
        const ave::RelinkReport relinked_m13a = ave::AssetRelinker::auto_relink_directory(p.media_pool_mut(), media_v2.parent_path(), true);
        ASSERT_EQ(relinked_m13a.relinked_count, 1u);
        ASSERT_TRUE(p.is_dirty());
        ASSERT_TRUE(p.save());
        ASSERT_FALSE(p.is_dirty());

        // Removing bin & removing asset mark dirty
        ASSERT_TRUE(p.media_pool_mut().remove_bin(bin1));
        ASSERT_TRUE(p.is_dirty());
        ASSERT_TRUE(p.save());
        ASSERT_FALSE(p.is_dirty());

        ASSERT_TRUE(p.media_pool_mut().remove_asset("asset-m13a-1"));
        ASSERT_TRUE(p.is_dirty());
        ASSERT_TRUE(p.save());
        ASSERT_FALSE(p.is_dirty());

        // 9d. Pure CommandHistory undo/redo symmetry & save+undo/redo
        const ave::TimelineItem cmd_item("m13a-cmd-1", "mc-cmd-1", "ma-cmd-1", "Cut 1",
                                         ave::Rational{0, 1}, ave::Rational{2, 1}, ave::Rational{0, 1});
        ASSERT_TRUE(p.execute_command(std::make_unique<ave::InsertClipCommand>(v1, cmd_item)).is_success());
        ASSERT_TRUE(p.is_dirty());
        ASSERT_TRUE(p.undo().is_success());
        ASSERT_FALSE(p.is_dirty()); // Pure undo back to clean point restores clean state
        ASSERT_TRUE(p.redo().is_success());
        ASSERT_TRUE(p.is_dirty());

        ASSERT_TRUE(p.save());
        ASSERT_FALSE(p.is_dirty());
        ASSERT_TRUE(p.undo().is_success()); // Undoing past saved point marks dirty
        ASSERT_TRUE(p.is_dirty());
        ASSERT_TRUE(p.redo().is_success()); // Redoing back to saved point restores clean
        ASSERT_FALSE(p.is_dirty());

        // 9e. Mixed non-history MediaPool edit + timeline command + undo must REMAIN dirty
        p.media_pool_mut().register_asset(ave::MediaAsset(
            "asset-m13a-2", media_v2, "cam_v1.mp4", 1024, "mp4", ave::Rational{4, 1}, {}));
        ASSERT_TRUE(p.is_dirty());
        const ave::TimelineItem cmd_item2("m13a-cmd-2", "mc-cmd-2", "asset-m13a-2", "Cut 2",
                                          ave::Rational{3, 1}, ave::Rational{2, 1}, ave::Rational{0, 1});
        ASSERT_TRUE(p.execute_command(std::make_unique<ave::InsertClipCommand>(v1, cmd_item2)).is_success());
        ASSERT_TRUE(p.is_dirty());
        ASSERT_TRUE(p.undo().is_success()); // Command history is back at clean_index_, BUT MediaPool has unsaved asset
        ASSERT_TRUE(p.is_dirty());

        // Failed save preserves dirty state
        const std::filesystem::path bad_parent_file = m13a_dir / "blocker_file";
        {
            std::ofstream ofs(bad_parent_file, std::ios::out | std::ios::binary | std::ios::trunc);
            ofs << "block";
        }
        ASSERT_FALSE(p.save(bad_parent_file / "cannot_save.aveproj"));
        ASSERT_TRUE(p.is_dirty());

        // Successful save clears dirty; Project::load() and Project::from_json() start clean
        ASSERT_TRUE(p.save(p_file));
        ASSERT_FALSE(p.is_dirty());
        ave::Project reloaded_m13a = ave::Project::load(p_file);
        ASSERT_FALSE(reloaded_m13a.is_dirty());
        reloaded_m13a.set_name("Post-Load Rename");
        ASSERT_TRUE(reloaded_m13a.is_dirty());

        ave::Project migrated_m13a = ave::Project::load(fixtures_projects_dir / "legacy_1_0_0_no_media.aveproj");
        ASSERT_FALSE(migrated_m13a.is_dirty());
        migrated_m13a.media_pool_mut().create_bin("After Load Bin");
        ASSERT_TRUE(migrated_m13a.is_dirty());

        // 9f. Move constructor & move assignment preserve clean baseline and internal listener forwarding
        int moved_listener_hits = 0;
        p.remove_change_listener(p_lid);
        ave::Project moved_ctor_proj(std::move(p));
        ASSERT_FALSE(moved_ctor_proj.is_dirty());
        (void)moved_ctor_proj.add_change_listener_with_id([&]() { ++moved_listener_hits; });
        moved_ctor_proj.media_pool_mut().create_bin("After Move Ctor Bin");
        ASSERT_TRUE(moved_ctor_proj.is_dirty());
        ASSERT_EQ(moved_listener_hits, 1);

        ave::Project moved_assign_proj("Target Before Assign");
        moved_assign_proj = std::move(moved_ctor_proj);
        ASSERT_TRUE(moved_assign_proj.is_dirty());
        int assign_hits = 0;
        (void)moved_assign_proj.add_change_listener_with_id([&]() { ++assign_hits; });
        ASSERT_TRUE(moved_assign_proj.save(p_file));
        ASSERT_FALSE(moved_assign_proj.is_dirty());
        ASSERT_TRUE(assign_hits >= 1);
        moved_assign_proj.set_name("Renamed After Move Assign");
        ASSERT_TRUE(moved_assign_proj.is_dirty());

        // Execute a timeline command after multiple Project moves (must invoke valid listener without dangling [this])
        const std::string mv_v1 = moved_assign_proj.timeline().video_tracks()[0].id();
        const ave::TimelineItem mv_item("m13a-mv-1", "mc-mv-1", "ma-mv-1", "Moved Cut",
                                        ave::Rational{20, 1}, ave::Rational{2, 1}, ave::Rational{0, 1});
        ASSERT_TRUE(moved_assign_proj.execute_command(std::make_unique<ave::InsertClipCommand>(mv_v1, mv_item)).is_success());
        ASSERT_TRUE(moved_assign_proj.undo().is_success());
    }

    // 10. M1.3C1 Atomic Autosave Engine & Recovery Validation API
    std::cout << "  Testing M1.3C1 Atomic Autosave Engine & Recovery Validation..." << std::endl;
    {
        const std::filesystem::path m13c1_dir = std::filesystem::current_path() / "test_output" / "m13c1_recovery";
        const std::filesystem::path recovery_dir = m13c1_dir / "app_recovery";
        std::filesystem::remove_all(m13c1_dir);
        std::filesystem::create_directories(m13c1_dir);

        // 10a. Default interval is 60 seconds and clean projects never generate autosaves
        ave::ProjectAutosaveEngine engine(recovery_dir);
        ASSERT_EQ(engine.interval(), std::chrono::seconds(60));
        engine.set_interval(std::chrono::milliseconds(50));
        ASSERT_EQ(engine.interval(), std::chrono::milliseconds(50));

        ave::Project untitled_proj("Untitled Recovery Edit");
        ASSERT_FALSE(untitled_proj.is_dirty());
        ASSERT_FALSE(engine.request_autosave(untitled_proj));
        ASSERT_TRUE(engine.wait_for_idle());
        ASSERT_EQ(engine.completed_count(), 0u);
        ASSERT_EQ(ave::RecoveryInspector::inspect_for_project(untitled_proj, recovery_dir).status,
                  ave::RecoveryStatus::Missing);

        // 10b. Dirty untitled project generates stable UUID-keyed recovery snapshot in recovery_dir without clearing dirty
        untitled_proj.media_pool_mut().register_asset(ave::MediaAsset(
            "asset-rec-1", fixture_path, "sample_test.mp4", 48796, "mp4", ave::Rational{3, 1}, {}));
        const std::string rec_bin = untitled_proj.media_pool_mut().create_bin("Recovery Bin");
        const std::string rec_sub = untitled_proj.media_pool_mut().create_clip("asset-rec-1", rec_bin, "Take 1");
        ASSERT_TRUE(untitled_proj.media_pool_mut().set_clip_color_tag(rec_sub, "Amber"));
        const std::string u_v1 = untitled_proj.timeline().video_tracks()[0].id();
        ave::TimelineItem u_item("rec-item-1", "mc-rec-1", "asset-rec-1", "Take 1 Cut",
                                 ave::Rational{1, 1}, ave::Rational{2, 1}, ave::Rational{0, 1});
        u_item.set_transform(12.5f, -6.0f);
        u_item.set_opacity(0.75f);
        ASSERT_TRUE(untitled_proj.execute_command(std::make_unique<ave::InsertClipCommand>(u_v1, u_item)).is_success());
        const uint64_t rev_before_autosave = untitled_proj.semantic_mutation_revision();
        ASSERT_TRUE(untitled_proj.is_dirty());

        ASSERT_TRUE(engine.request_autosave(untitled_proj));
        ASSERT_TRUE(engine.wait_for_idle());
        ASSERT_EQ(engine.completed_count(), 1u);
        ASSERT_TRUE(untitled_proj.is_dirty()); // Autosave MUST NOT clear dirty state
        ASSERT_EQ(untitled_proj.semantic_mutation_revision(), rev_before_autosave);
        ASSERT_TRUE(untitled_proj.file_path().empty());

        const std::filesystem::path untitled_sidecar =
            ave::RecoveryInspector::untitled_recovery_path_for(recovery_dir, untitled_proj.id());
        ASSERT_TRUE(std::filesystem::exists(untitled_sidecar));
        const ave::RecoveryInfo u_info = ave::RecoveryInspector::inspect_for_project(untitled_proj, recovery_dir);
        ASSERT_EQ(u_info.status, ave::RecoveryStatus::Valid);
        ASSERT_EQ(u_info.recovery_format_version, ave::RecoveryInspector::kRecoveryFormatVersion);
        ASSERT_EQ(u_info.project_id, untitled_proj.id());
        ASSERT_EQ(u_info.snapshot_revision, rev_before_autosave);
        ASSERT_TRUE(u_info.snapshot_timestamp_ms > 0);
        ASSERT_FALSE(u_info.project_sha256.empty());

        const ave::Project restored_untitled = ave::RecoveryInspector::load_recovered_project(untitled_sidecar);
        ASSERT_EQ(restored_untitled.id(), untitled_proj.id());
        ASSERT_EQ(restored_untitled.timeline(), untitled_proj.timeline());
        ASSERT_EQ(restored_untitled.media_pool(), untitled_proj.media_pool());
        ASSERT_EQ(restored_untitled.timeline().find_item("rec-item-1")->opacity(), 0.75f);

        // 10c. Named project uses <project>.aveproj.autosave sidecar and NEVER modifies the original .aveproj
        const std::filesystem::path named_proj_path = m13c1_dir / "named_edit.aveproj";
        ASSERT_TRUE(untitled_proj.save(named_proj_path));
        ASSERT_FALSE(untitled_proj.is_dirty());

        std::string saved_disk_bytes_before;
        {
            std::ifstream ifs(named_proj_path, std::ios::in | std::ios::binary);
            std::ostringstream ss;
            ss << ifs.rdbuf();
            saved_disk_bytes_before = ss.str();
        }

        ave::TimelineItem u_item2("rec-item-2", "mc-rec-2", "asset-rec-1", "Follow-up Cut",
                                  ave::Rational{4, 1}, ave::Rational{2, 1}, ave::Rational{0, 1});
        ASSERT_TRUE(untitled_proj.execute_command(std::make_unique<ave::InsertClipCommand>(u_v1, u_item2)).is_success());
        ASSERT_TRUE(untitled_proj.is_dirty());
        ASSERT_TRUE(engine.request_autosave(untitled_proj));
        ASSERT_TRUE(engine.wait_for_idle());
        ASSERT_TRUE(untitled_proj.is_dirty());
        ASSERT_EQ(untitled_proj.file_path(), named_proj_path);

        const std::filesystem::path named_sidecar = ave::RecoveryInspector::named_recovery_path_for(named_proj_path);
        ASSERT_TRUE(std::filesystem::exists(named_sidecar));
        ASSERT_EQ(ave::RecoveryInspector::inspect_for_project(untitled_proj, recovery_dir).status,
                  ave::RecoveryStatus::Valid);

        std::string saved_disk_bytes_after;
        {
            std::ifstream ifs(named_proj_path, std::ios::in | std::ios::binary);
            std::ostringstream ss;
            ss << ifs.rdbuf();
            saved_disk_bytes_after = ss.str();
        }
        ASSERT_EQ(saved_disk_bytes_after, saved_disk_bytes_before);

        // 10d. Failed write preserves previous valid recovery file and leaves no orphaned temp files
        const std::filesystem::path blocker_tmp = std::filesystem::path(named_sidecar.string() + ".tmp");
        std::filesystem::create_directories(blocker_tmp / "nested");

        untitled_proj.set_name("Edited While Temp Blocked");
        ASSERT_TRUE(engine.request_autosave(untitled_proj));
        ASSERT_TRUE(engine.wait_for_idle());
        ASSERT_FALSE(engine.last_result().succeeded);
        ASSERT_TRUE(engine.failed_count() >= 1u);
        std::filesystem::remove_all(blocker_tmp);

        const ave::RecoveryInfo preserved_info = ave::RecoveryInspector::inspect_file(named_sidecar);
        ASSERT_EQ(preserved_info.status, ave::RecoveryStatus::Valid);
        ASSERT_EQ(preserved_info.project_name, "Untitled Recovery Edit");

        // Now unblock and retry on normal (non-forced) request -> failed attempt must NOT poison dedup
        ASSERT_TRUE(engine.request_autosave(untitled_proj, false));
        ASSERT_TRUE(engine.wait_for_idle());
        ASSERT_TRUE(engine.last_result().succeeded);
        const ave::RecoveryInfo updated_info = ave::RecoveryInspector::inspect_file(named_sidecar);
        ASSERT_EQ(updated_info.status, ave::RecoveryStatus::Valid);
        ASSERT_EQ(updated_info.project_name, "Edited While Temp Blocked");

        // 10e. Stale detection: crash scenario stays Valid; normal save marks obsolete recovery Stale
        {
            // Simulated restart after crash (disk .aveproj unchanged while autosave holds newer edits) -> Valid!
            ave::Project disk_reloaded = ave::Project::load(named_proj_path);
            ASSERT_EQ(ave::RecoveryInspector::inspect_for_project(disk_reloaded, recovery_dir).status,
                      ave::RecoveryStatus::Valid);
        }

        // Case 1: Normal save at exact autosaved state -> Stale
        ASSERT_TRUE(untitled_proj.save(named_proj_path));
        ASSERT_EQ(ave::RecoveryInspector::inspect_for_project(untitled_proj, recovery_dir).status,
                  ave::RecoveryStatus::Stale);
        ASSERT_EQ(ave::RecoveryInspector::inspect_file(named_sidecar).status, ave::RecoveryStatus::Stale);

        // Case 2: Edit -> autosave (rev A) -> further edit -> normal save (rev B) -> old autosave (rev A) is Stale!
        untitled_proj.set_name("Rev A");
        ASSERT_TRUE(engine.request_autosave(untitled_proj, true));
        ASSERT_TRUE(engine.wait_for_idle());
        untitled_proj.set_name("Rev B Saved");
        ASSERT_TRUE(untitled_proj.save(named_proj_path));
        ASSERT_EQ(ave::RecoveryInspector::inspect_for_project(untitled_proj, recovery_dir).status,
                  ave::RecoveryStatus::Stale);
        ASSERT_EQ(ave::RecoveryInspector::inspect_file(named_sidecar).status, ave::RecoveryStatus::Stale);

        // 10f. Corrupt recovery snapshot detection (checksum tamper, syntax error, version mismatch)
        const std::filesystem::path corrupt_copy = m13c1_dir / "corrupt_tampered.autosave";
        std::filesystem::copy_file(named_sidecar, corrupt_copy, std::filesystem::copy_options::overwrite_existing);
        {
            std::string json_text;
            {
                std::ifstream ifs(corrupt_copy, std::ios::in | std::ios::binary);
                std::ostringstream ss;
                ss << ifs.rdbuf();
                json_text = ss.str();
            }
            const std::size_t pos = json_text.find("Rev A");
            ASSERT_TRUE(pos != std::string::npos);
            json_text.replace(pos, 5, "Rev Z"); // Tamper without updating checksums
            std::ofstream ofs(corrupt_copy, std::ios::out | std::ios::binary | std::ios::trunc);
            ofs << json_text;
        }
        ASSERT_EQ(ave::RecoveryInspector::inspect_file(corrupt_copy).status, ave::RecoveryStatus::Corrupt);

        const std::filesystem::path malformed_rec = m13c1_dir / "broken.autosave";
        {
            std::ofstream ofs(malformed_rec, std::ios::out | std::ios::binary | std::ios::trunc);
            ofs << "{not valid json";
        }
        ASSERT_EQ(ave::RecoveryInspector::inspect_file(malformed_rec).status, ave::RecoveryStatus::Corrupt);

        // 10g. Duplicate concurrent write prevention + newer snapshot cannot be overwritten by older async write
        {
            ave::ProjectAutosaveEngine race_engine(recovery_dir);
            ave::Project race_proj("Race Project Baseline");
            const std::filesystem::path race_proj_path = m13c1_dir / "race_proj.aveproj";
            ASSERT_TRUE(race_proj.save(race_proj_path));

            race_proj.set_name("Snapshot Seq 1");
            const uint64_t seq1 = race_engine.allocate_capture_sequence();
            auto cap1 = ave::ProjectAutosaveEngine::capture_snapshot_if_dirty(race_proj, recovery_dir, seq1);
            ASSERT_TRUE(cap1.has_value());

            race_proj.set_name("Snapshot Seq 2 Newer");
            const uint64_t seq2 = race_engine.allocate_capture_sequence();
            auto cap2 = ave::ProjectAutosaveEngine::capture_snapshot_if_dirty(race_proj, recovery_dir, seq2);
            ASSERT_TRUE(cap2.has_value());

            std::mutex hook_mu;
            std::condition_variable hook_cv;
            bool seq1_paused_at_hook = false;
            bool release_seq1 = false;

            race_engine.set_pre_replace_hook_for_test([&](const ave::AutosaveCapture& c) {
                if (c.capture_seq == seq1) {
                    std::unique_lock<std::mutex> lk(hook_mu);
                    seq1_paused_at_hook = true;
                    hook_cv.notify_all();
                    hook_cv.wait(lk, [&]() { return release_seq1; });
                }
            });

            ave::AutosaveWriteResult res1;
            std::thread t1([&]() {
                res1 = race_engine.commit_capture_blocking(*cap1);
            });

            {
                std::unique_lock<std::mutex> lk(hook_mu);
                ASSERT_TRUE(hook_cv.wait_for(lk, std::chrono::seconds(3), [&]() { return seq1_paused_at_hook; }));
            }

            // While older write #1 is paused right before atomic rename, commit newer snapshot #2
            const ave::AutosaveWriteResult res2 = race_engine.commit_capture_blocking(*cap2);
            ASSERT_TRUE(res2.succeeded);

            {
                std::lock_guard<std::mutex> lk(hook_mu);
                release_seq1 = true;
            }
            hook_cv.notify_all();
            t1.join();

            ASSERT_FALSE(res1.succeeded);
            ASSERT_TRUE(res1.superseded);

            const ave::RecoveryInfo final_race = ave::RecoveryInspector::inspect_file(cap2->target_recovery_path);
            ASSERT_EQ(final_race.status, ave::RecoveryStatus::Valid);
            ASSERT_EQ(final_race.snapshot_revision, cap2->snapshot_revision);
            ASSERT_EQ(final_race.project_name, "Snapshot Seq 2 Newer");
        }

        // 10h. Duplicate concurrent request_autosave coalesces to reschedule + worker cancellation/shutdown is safe
        {
            ave::ProjectAutosaveEngine cancel_engine(recovery_dir);
            ave::Project cancel_proj("Cancel Shutdown Proj");
            cancel_proj.set_name("In-Flight Capture");

            std::mutex m;
            std::condition_variable cv;
            bool inside_worker = false;
            bool allow_worker_finish = false;

            cancel_engine.set_pre_replace_hook_for_test([&](const ave::AutosaveCapture&) {
                std::unique_lock<std::mutex> lk(m);
                inside_worker = true;
                cv.notify_all();
                cv.wait(lk, [&]() { return allow_worker_finish; });
            });

            ASSERT_TRUE(cancel_engine.request_autosave(cancel_proj));
            {
                std::unique_lock<std::mutex> lk(m);
                ASSERT_TRUE(cv.wait_for(lk, std::chrono::seconds(3), [&]() { return inside_worker; }));
            }

            // Second request while write is active MUST NOT launch duplicate job; sets pending reschedule only
            cancel_proj.set_name("Edited During Write");
            ASSERT_FALSE(cancel_engine.request_autosave(cancel_proj));
            ASSERT_TRUE(cancel_engine.has_pending_reschedule());
            ASSERT_TRUE(cancel_proj.is_dirty());

            // Request shutdown while worker was paused, then unblock worker -> must cancel cleanly and join!
            std::atomic<bool> stopper_entered{false};
            std::thread stopper([&]() {
                stopper_entered.store(true, std::memory_order_release);
                cancel_engine.shutdown();
            });
            while (!stopper_entered.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            {
                std::lock_guard<std::mutex> lk(m);
                allow_worker_finish = true;
            }
            cv.notify_all();
            stopper.join();
            ASSERT_EQ(ave::RecoveryInspector::inspect_for_project(cancel_proj, recovery_dir).status,
                      ave::RecoveryStatus::Missing);
            ASSERT_FALSE(cancel_engine.request_autosave(cancel_proj));
        }

        engine.shutdown();
    }

    // 11. M1.3C2 Startup Discovery, Safe Restoration, Discard & Post-Save Sequence Fencing Tests
    {
        std::cout << "Running M1.3C2 Recovery Workflow & Concurrency Tests..." << std::endl;

        const std::filesystem::path m13c2_root =
            std::filesystem::current_path() / "test_output" / "m13c2_core";
        std::error_code rm_ec;
        std::filesystem::remove_all(m13c2_root, rm_ec);
        const std::filesystem::path rec_dir = m13c2_root / "recovery";
        std::filesystem::create_directories(rec_dir);

        ave::ProjectAutosaveEngine engine(rec_dir);

        // 11a. Multiple untitled recovery records + full timeline/mediapool restore fidelity + restored dirty invariant
        ave::Project p_multi_1("Multi Session A");
        ave::MediaAsset a1("asset-r1", m13c2_root / "clip_a.mp4", "Clip Alpha", 1024, "mp4", ave::Rational{10, 1}, {});
        p_multi_1.media_pool_mut().register_asset(a1);
        const std::string bin_id = p_multi_1.media_pool_mut().create_bin("Selects");
        const std::string pool_clip_id = p_multi_1.media_pool_mut().create_clip("asset-r1", bin_id, "Alpha Clip");
        p_multi_1.media_pool_mut().set_clip_source_range(
            pool_clip_id, ave::Rational{1, 1}, ave::Rational{3, 1});
        p_multi_1.media_pool_mut().set_clip_color_tag(pool_clip_id, "Blue");
        const std::string v1_m1 = p_multi_1.timeline().video_tracks()[0].id();
        ave::TimelineItem clip_m1(
            "rec-clip-1", "ref-r1", "asset-r1", "Rec Shot 1",
            ave::Rational{2, 1}, ave::Rational{4, 1}, ave::Rational{1, 1});
        clip_m1.set_transform(15.5f, -8.25f);
        clip_m1.set_scale(1.25f, 1.25f);
        clip_m1.set_rotation(12.0f);
        clip_m1.set_opacity(0.75f);
        ASSERT_EQ(p_multi_1.execute_command(std::make_unique<ave::InsertClipCommand>(v1_m1, clip_m1)).status,
                  ave::CommandStatus::Success);
        ASSERT_TRUE(p_multi_1.is_dirty());

        const auto cap_m1 = ave::ProjectAutosaveEngine::capture_snapshot_if_dirty(
            p_multi_1, rec_dir, engine.allocate_capture_sequence());
        ASSERT_TRUE(cap_m1.has_value());
        ASSERT_TRUE(engine.commit_capture_blocking(*cap_m1).succeeded);

        ave::Project p_multi_2("Multi Session B");
        p_multi_2.set_name("Multi Session B Edited");
        const auto cap_m2 = ave::ProjectAutosaveEngine::capture_snapshot_if_dirty(
            p_multi_2, rec_dir, engine.allocate_capture_sequence());
        ASSERT_TRUE(cap_m2.has_value());
        ASSERT_TRUE(engine.commit_capture_blocking(*cap_m2).succeeded);

        // Discovery sees both untitled records without scanning unrelated dirs
        const auto scanned = ave::RecoveryInspector::scan_recovery_directory(rec_dir);
        ASSERT_EQ(scanned.size(), 2u);

        // Selective discard deletes ONLY p_multi_2, keeping p_multi_1 intact
        const auto discard_one = engine.discard_recovery_file(cap_m2->target_recovery_path);
        ASSERT_TRUE(discard_one.succeeded);
        ASSERT_EQ(discard_one.removed_count, 1u);
        ASSERT_FALSE(std::filesystem::exists(cap_m2->target_recovery_path));
        ASSERT_TRUE(std::filesystem::exists(cap_m1->target_recovery_path));

        // Restore untitled project and verify full fidelity + dirty state invariant
        ave::Project restored_untitled = ave::RecoveryInspector::load_recovered_project(
            cap_m1->target_recovery_path);
        ASSERT_TRUE(restored_untitled.is_dirty());
        ASSERT_TRUE(restored_untitled.is_restored_from_recovery());
        ASSERT_TRUE(restored_untitled.file_path().empty());
        ASSERT_EQ(restored_untitled.id(), p_multi_1.id());
        ASSERT_EQ(restored_untitled.media_pool().assets().size(), 1u);
        ASSERT_TRUE(restored_untitled.media_pool().find_asset("asset-r1") != nullptr);
        ASSERT_TRUE(restored_untitled.media_pool().find_bin(bin_id) != nullptr);
        ASSERT_TRUE(restored_untitled.media_pool().find_clip(pool_clip_id) != nullptr);
        ASSERT_EQ(restored_untitled.media_pool().find_clip(pool_clip_id)->color_tag(), "Blue");
        const ave::TimelineItem* r_item = restored_untitled.timeline().find_item("rec-clip-1");
        ASSERT_TRUE(r_item != nullptr);
        ASSERT_TRUE(std::abs(r_item->transform_x() - 15.5f) < 1e-4f);
        ASSERT_TRUE(std::abs(r_item->transform_y() - (-8.25f)) < 1e-4f);
        ASSERT_TRUE(std::abs(r_item->scale_x() - 1.25f) < 1e-4f);
        ASSERT_TRUE(std::abs(r_item->rotation() - 12.0f) < 1e-4f);
        ASSERT_TRUE(std::abs(r_item->opacity() - 0.75f) < 1e-4f);

        // Restored project supports further edits and autosaves before explicit Save
        restored_untitled.set_name("Restored Then Edited");
        ASSERT_TRUE(restored_untitled.is_dirty());
        const auto cap_after_restore = ave::ProjectAutosaveEngine::capture_snapshot_if_dirty(
            restored_untitled, rec_dir, engine.allocate_capture_sequence());
        ASSERT_TRUE(cap_after_restore.has_value());
        ASSERT_TRUE(engine.commit_capture_blocking(*cap_after_restore).succeeded);

        const std::filesystem::path saved_from_restored = m13c2_root / "restored_saved.aveproj";
        ASSERT_TRUE(restored_untitled.save(saved_from_restored));
        ASSERT_FALSE(restored_untitled.is_dirty());
        ASSERT_FALSE(restored_untitled.is_restored_from_recovery());

        const auto clean_after_untitled_save = engine.cleanup_after_save(
            restored_untitled.id(),
            saved_from_restored,
            {},
            restored_untitled.saved_mutation_revision());
        ASSERT_TRUE(clean_after_untitled_save.succeeded);
        ASSERT_FALSE(std::filesystem::exists(cap_m1->target_recovery_path));

        // 11b. Named project restore leaves original .aveproj untouched; Stale, Corrupt & identity mismatch rejected
        const std::filesystem::path named_proj_path = m13c2_root / "named_project.aveproj";
        ave::Project named_proj("Named Original");
        ASSERT_TRUE(named_proj.save(named_proj_path));
        std::string orig_disk_bytes;
        {
            std::ifstream ifs(named_proj_path, std::ios::in | std::ios::binary);
            std::ostringstream ss;
            ss << ifs.rdbuf();
            orig_disk_bytes = ss.str();
        }

        named_proj.set_name("Named Unsaved Crash");
        const auto cap_named = ave::ProjectAutosaveEngine::capture_snapshot_if_dirty(
            named_proj, rec_dir, engine.allocate_capture_sequence());
        ASSERT_TRUE(cap_named.has_value());
        ASSERT_TRUE(engine.commit_capture_blocking(*cap_named).succeeded);

        const ave::RecoveryInfo named_valid = ave::RecoveryInspector::inspect_file(
            cap_named->target_recovery_path, named_proj_path);
        ASSERT_EQ(named_valid.status, ave::RecoveryStatus::Valid);

        // Wrong expected_project_id or different target project file ID -> rejected
        bool threw_mismatch = false;
        try {
            (void)ave::RecoveryInspector::load_recovered_project(
                cap_named->target_recovery_path, named_proj_path, "wrong-project-id");
        } catch (const std::runtime_error&) {
            threw_mismatch = true;
        }
        ASSERT_TRUE(threw_mismatch);

        const std::filesystem::path other_proj_path = m13c2_root / "other_proj.aveproj";
        ave::Project other_proj("Other Distinct Project");
        ASSERT_TRUE(other_proj.save(other_proj_path));
        const ave::RecoveryInfo mismatch_info = ave::RecoveryInspector::inspect_file(
            cap_named->target_recovery_path, other_proj_path);
        ASSERT_EQ(mismatch_info.status, ave::RecoveryStatus::Corrupt);

        // Restore named recovery -> must keep named_proj_path untouched and mark restored project dirty
        ave::Project restored_named = ave::RecoveryInspector::load_recovered_project(
            cap_named->target_recovery_path, named_proj_path);
        ASSERT_TRUE(restored_named.is_dirty());
        ASSERT_EQ(restored_named.name(), "Named Unsaved Crash");
        ASSERT_EQ(restored_named.file_path(), named_proj_path);
        std::string after_restore_disk_bytes;
        {
            std::ifstream ifs(named_proj_path, std::ios::in | std::ios::binary);
            std::ostringstream ss;
            ss << ifs.rdbuf();
            after_restore_disk_bytes = ss.str();
        }
        ASSERT_EQ(after_restore_disk_bytes, orig_disk_bytes);

        // Truncated/corrupt recovery file is classified Corrupt and rejected by loader
        const std::filesystem::path corrupt_sidecar = rec_dir / "corrupt_unit.autosave";
        {
            std::ofstream ofs(corrupt_sidecar, std::ios::out | std::ios::binary | std::ios::trunc);
            ofs << "{\"recovery_format_version\":1,\"project_id\":\"broken\",\"record_sha256\":\"bad\"";
        }
        ASSERT_EQ(ave::RecoveryInspector::inspect_file(corrupt_sidecar).status, ave::RecoveryStatus::Corrupt);
        bool threw_corrupt = false;
        try {
            (void)ave::RecoveryInspector::load_recovered_project(corrupt_sidecar);
        } catch (const std::runtime_error&) {
            threw_corrupt = true;
        }
        ASSERT_TRUE(threw_corrupt);

        // Stale recovery file (original saved newer than snapshot) is classified Stale and rejected by loader
        ave::Project stale_base("Stale Base");
        const std::filesystem::path stale_proj_path = m13c2_root / "stale_base.aveproj";
        ASSERT_TRUE(stale_base.save(stale_proj_path));
        stale_base.set_name("Stale Snapshot State");
        const auto cap_stale = ave::ProjectAutosaveEngine::capture_snapshot_if_dirty(
            stale_base, rec_dir, engine.allocate_capture_sequence());
        ASSERT_TRUE(cap_stale.has_value());
        ASSERT_TRUE(engine.commit_capture_blocking(*cap_stale).succeeded);
        stale_base.set_name("Newer Saved On Disk");
        ASSERT_TRUE(stale_base.save(stale_proj_path));
        ASSERT_EQ(ave::RecoveryInspector::inspect_file(cap_stale->target_recovery_path, stale_proj_path).status,
                  ave::RecoveryStatus::Stale);
        bool threw_stale = false;
        try {
            (void)ave::RecoveryInspector::load_recovered_project(
                cap_stale->target_recovery_path, stale_proj_path);
        } catch (const std::runtime_error&) {
            threw_stale = true;
        }
        ASSERT_TRUE(threw_stale);

        // 11c. cleanup_after_save preserves newer revisions and unrelated project IDs
        ave::Project keep_other("Keep Unrelated");
        keep_other.set_name("Keep Unrelated Dirty");
        const auto cap_unrelated = ave::ProjectAutosaveEngine::capture_snapshot_if_dirty(
            keep_other, rec_dir, engine.allocate_capture_sequence());
        ASSERT_TRUE(cap_unrelated.has_value());
        ASSERT_TRUE(engine.commit_capture_blocking(*cap_unrelated).succeeded);

        const std::filesystem::path newer_target_path = m13c2_root / "rev_check.aveproj";
        ave::Project rev_proj("Rev Check");
        rev_proj.set_name("Rev Check Edited");
        ASSERT_TRUE(rev_proj.save(newer_target_path));
        rev_proj.set_name("Rev Check v2"); // semantic_mutation_revision == 2, saved == 1
        const auto cap_newer = ave::ProjectAutosaveEngine::capture_snapshot_if_dirty(
            rev_proj, rec_dir, engine.allocate_capture_sequence());
        ASSERT_TRUE(cap_newer.has_value());
        ASSERT_TRUE(cap_newer->snapshot_revision > rev_proj.saved_mutation_revision());
        ASSERT_TRUE(engine.commit_capture_blocking(*cap_newer).succeeded);

        const auto cleanup_preserve = engine.cleanup_after_save(
            rev_proj.id(), newer_target_path, {}, rev_proj.saved_mutation_revision());
        ASSERT_TRUE(cleanup_preserve.succeeded);
        ASSERT_EQ(cleanup_preserve.removed_count, 0u);
        ASSERT_EQ(cleanup_preserve.preserved_count, 1u);
        ASSERT_TRUE(std::filesystem::exists(cap_newer->target_recovery_path));
        ASSERT_TRUE(std::filesystem::exists(cap_unrelated->target_recovery_path));

        // 11d. Concurrency fence #18 & #19:
        // An in-flight autosave paused before rename MUST NOT recreate sidecar after discard or save!
        {
            ave::ProjectAutosaveEngine fence_engine(rec_dir);
            ave::Project fence_proj("Fence Test Project");
            const std::filesystem::path fence_proj_path = m13c2_root / "fenced.aveproj";
            ASSERT_TRUE(fence_proj.save(fence_proj_path));
            fence_proj.set_name("Fence Dirty 1");

            const uint64_t stale_seq = fence_engine.allocate_capture_sequence();
            const auto stale_cap = ave::ProjectAutosaveEngine::capture_snapshot_if_dirty(
                fence_proj, rec_dir, stale_seq);
            ASSERT_TRUE(stale_cap.has_value());

            std::mutex m;
            std::condition_variable cv;
            bool paused = false;
            bool resume = false;
            fence_engine.set_pre_replace_hook_for_test([&](const ave::AutosaveCapture& c) {
                if (c.capture_seq == stale_seq) {
                    std::unique_lock<std::mutex> lk(m);
                    paused = true;
                    cv.notify_all();
                    cv.wait(lk, [&]() { return resume; });
                }
            });

            ave::AutosaveWriteResult stalled_res;
            std::thread writer([&]() {
                stalled_res = fence_engine.commit_capture_blocking(*stale_cap);
            });
            {
                std::unique_lock<std::mutex> lk(m);
                ASSERT_TRUE(cv.wait_for(lk, std::chrono::seconds(3), [&]() { return paused; }));
            }

            // Simulate user clicking "Don't Save" -> discard_for_project while autosave is in flight
            const auto discard_race = fence_engine.discard_for_project(fence_proj.id(), fence_proj_path);
            ASSERT_TRUE(discard_race.succeeded);

            {
                std::lock_guard<std::mutex> lk(m);
                resume = true;
            }
            cv.notify_all();
            writer.join();

            ASSERT_FALSE(stalled_res.succeeded);
            ASSERT_TRUE(stalled_res.superseded);
            ASSERT_FALSE(std::filesystem::exists(stale_cap->target_recovery_path));

            // Repeat for Save cleanup fencing: paused autosave before rename cannot recreate sidecar after save
            fence_proj.set_name("Fence Dirty 2");
            const uint64_t save_race_seq = fence_engine.allocate_capture_sequence();
            const auto save_race_cap = ave::ProjectAutosaveEngine::capture_snapshot_if_dirty(
                fence_proj, rec_dir, save_race_seq);
            ASSERT_TRUE(save_race_cap.has_value());

            paused = false;
            resume = false;
            fence_engine.set_pre_replace_hook_for_test([&](const ave::AutosaveCapture& c) {
                if (c.capture_seq == save_race_seq) {
                    std::unique_lock<std::mutex> lk(m);
                    paused = true;
                    cv.notify_all();
                    cv.wait(lk, [&]() { return resume; });
                }
            });

            ave::AutosaveWriteResult save_race_res;
            std::thread writer2([&]() {
                save_race_res = fence_engine.commit_capture_blocking(*save_race_cap);
            });
            {
                std::unique_lock<std::mutex> lk(m);
                ASSERT_TRUE(cv.wait_for(lk, std::chrono::seconds(3), [&]() { return paused; }));
            }

            ASSERT_TRUE(fence_proj.save(fence_proj_path));
            const auto save_clean = fence_engine.cleanup_after_save(
                fence_proj.id(), fence_proj_path, {}, fence_proj.saved_mutation_revision());
            ASSERT_TRUE(save_clean.succeeded);

            {
                std::lock_guard<std::mutex> lk(m);
                resume = true;
            }
            cv.notify_all();
            writer2.join();

            ASSERT_FALSE(save_race_res.succeeded);
            ASSERT_TRUE(save_race_res.superseded);
            ASSERT_FALSE(std::filesystem::exists(save_race_cap->target_recovery_path));

            // Later edits after fence still allocate higher sequences and succeed
            fence_proj.set_name("Fence Dirty 3 After Save");
            fence_engine.set_pre_replace_hook_for_test({});
            const uint64_t fresh_seq = fence_engine.allocate_capture_sequence();
            const auto fresh_cap = ave::ProjectAutosaveEngine::capture_snapshot_if_dirty(
                fence_proj, rec_dir, fresh_seq);
            ASSERT_TRUE(fresh_cap.has_value());
            ASSERT_TRUE(fence_engine.commit_capture_blocking(*fresh_cap).succeeded);
            ASSERT_TRUE(std::filesystem::exists(fresh_cap->target_recovery_path));
        }

        engine.shutdown();
    }

    // Clean up temporary files
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::current_path() / "test_output", ec);

    std::cout << "All Project Persistence & OTIO Interchange Tests PASSED!" << std::endl;
    return 0;
}
