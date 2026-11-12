#include "ave/project.hpp"
#include "ave/otio_interchange.hpp"
#include "ave/asset_relinker.hpp"
#include "ave/command.hpp"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cmath>
#include <stdexcept>
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

    // Clean up temporary files
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::current_path() / "test_output", ec);

    std::cout << "All Project Persistence & OTIO Interchange Tests PASSED!" << std::endl;
    return 0;
}
