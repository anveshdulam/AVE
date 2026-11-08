#include "ave/project.hpp"
#include "ave/otio_interchange.hpp"
#include "ave/asset_relinker.hpp"
#include "ave/command.hpp"

#include <iostream>
#include <filesystem>
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
    ASSERT_EQ(project.schema_version(), "1.0.0");
    ASSERT_EQ(project.name(), "Persistence Master");
    ASSERT_FALSE(project.is_dirty());

    // Register asset in MediaPool
    std::string asset_id = "asset-p1";
    ave::MediaAsset asset(asset_id, fixture_path, "sample_test.mp4", 48796, "mp4", ave::Rational{1, 1}, {});
    project.media_pool_mut().register_asset(std::move(asset));

    // Add V2 track
    project.timeline_mut().add_video_track("V2");
    std::string v1_id = project.timeline().video_tracks()[0].id();

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
    ASSERT_EQ(loaded_proj.name(), "Persistence Master");
    ASSERT_EQ(loaded_proj.schema_version(), "1.0.0");
    ASSERT_FALSE(loaded_proj.is_dirty());
    ASSERT_EQ(loaded_proj.timeline().video_tracks().size(), 2);
    ASSERT_EQ(loaded_proj.media_pool().asset_count(), 1);

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

    // Clean up temporary files
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::current_path() / "test_output", ec);

    std::cout << "All Project Persistence & OTIO Interchange Tests PASSED!" << std::endl;
    return 0;
}
