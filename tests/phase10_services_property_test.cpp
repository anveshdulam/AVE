#include "ave/ai_studio_service.hpp"
#include "ave/asset_relinker.hpp"
#include "ave/command.hpp"
#include "ave/frame_presentation_hub.hpp"
#include "ave/inspector_service.hpp"
#include "ave/media_asset.hpp"
#include "ave/media_cache.hpp"
#include "ave/media_import_queue.hpp"
#include "ave/media_pool.hpp"
#include "ave/media_pool_model.hpp"
#include "ave/playback_service.hpp"
#include "ave/playback_session.hpp"
#include "ave/playback_snapshot.hpp"
#include "ave/project.hpp"
#include "ave/timeline.hpp"
#include "ave/timeline_model.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <QAbstractListModel>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaEnum>
#include <QMetaObject>
#include <QMetaProperty>
#include <QSignalSpy>
#include <QUrl>
#include <QVariantList>

#ifdef _WIN32
#include <process.h>
#endif

namespace {

std::filesystem::path fixture_file(const char* name) {
#ifdef AVE_TEST_FIXTURES_DIR
    return std::filesystem::path(AVE_TEST_FIXTURES_DIR) / name;
#else
    return std::filesystem::path("tests/fixtures") / name;
#endif
}

struct ScopedTempDir {
    std::filesystem::path path;
    explicit ScopedTempDir(const std::string& prefix) {
#ifdef _WIN32
        const int pid = _getpid();
#else
        const int pid = 0;
#endif
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / (prefix + "_" + std::to_string(pid) + "_" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

struct PropExpectation {
    const char* name;
    bool writable;
    bool constant;
};

template <std::size_t N>
void verify_meta_properties(const QMetaObject& mo,
                            const PropExpectation (&expected)[N],
                            const char* class_label) {
    int custom_count = 0;
    for (int i = mo.propertyOffset(); i < mo.propertyCount(); ++i) {
        ++custom_count;
    }
    EXPECT_EQ(custom_count, static_cast<int>(N)) << class_label;

    for (const auto& exp : expected) {
        const int idx = mo.indexOfProperty(exp.name);
        ASSERT_GE(idx, 0) << class_label << " missing Q_PROPERTY: " << exp.name;
        const QMetaProperty prop = mo.property(idx);
        EXPECT_EQ(prop.isWritable(), exp.writable) << class_label << "::" << exp.name;
        EXPECT_EQ(prop.isConstant(), exp.constant) << class_label << "::" << exp.name;
        if (!exp.constant) {
            ASSERT_TRUE(prop.hasNotifySignal()) << class_label << "::" << exp.name;
            EXPECT_EQ(prop.notifySignal().parameterCount(), 0) << class_label << "::" << exp.name;
        }
    }
}

ave::MediaAsset make_synthetic_av_asset(const std::string& id,
                                        const std::string& name,
                                        const std::filesystem::path& file_path,
                                        ave::Rational duration,
                                        uint64_t size_bytes = 4096) {
    std::vector<ave::SourceStream> streams;
    ave::SourceStream vs(0, ave::StreamType::Video, "h264", ave::Rational{1, 24},
                         duration.to_frames(ave::Rational{24, 1}));
    ave::VideoStreamInfo vi;
    vi.width = 1280;
    vi.height = 720;
    vi.frame_rate = ave::Rational{24, 1};
    vs.set_video_info(vi);
    streams.push_back(std::move(vs));

    ave::SourceStream as(1, ave::StreamType::Audio, "aac", ave::Rational{1, 48000},
                         duration.to_frames(ave::Rational{48000, 1}));
    ave::AudioStreamInfo ai;
    ai.sample_rate = 48000;
    ai.channels = 2;
    as.set_audio_info(ai);
    streams.push_back(std::move(as));

    return ave::MediaAsset(id, file_path, name, size_bytes, "mp4", duration, std::move(streams));
}

ave::WaveformData make_synthetic_waveform(ave::Rational duration, int peaks_per_second = 100) {
    ave::WaveformData wf;
    wf.channels = 2;
    wf.sample_rate = 48000;
    wf.peaks_per_second = peaks_per_second;
    wf.duration = duration;
    const std::size_t count = static_cast<std::size_t>(
        std::max(1.0, std::round(duration.to_seconds() * static_cast<double>(peaks_per_second))));
    wf.channel_peaks.resize(2);
    for (int ch = 0; ch < 2; ++ch) {
        wf.channel_peaks[static_cast<std::size_t>(ch)].min_peaks.resize(count);
        wf.channel_peaks[static_cast<std::size_t>(ch)].max_peaks.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            const float mag = 0.15f + 0.65f * static_cast<float>(i % 10) / 9.0f;
            wf.channel_peaks[static_cast<std::size_t>(ch)].min_peaks[i] = -mag;
            wf.channel_peaks[static_cast<std::size_t>(ch)].max_peaks[i] = mag;
        }
    }
    return wf;
}

int find_clip_row_by_id(const QAbstractListModel* clips_model, const QString& clip_id) {
    if (!clips_model) {
        return -1;
    }
    for (int row = 0; row < clips_model->rowCount(); ++row) {
        const QModelIndex idx = clips_model->index(row, 0);
        if (clips_model->data(idx, ave::desktop::TimelineClipListModel::ClipIdRole).toString() == clip_id) {
            return row;
        }
    }
    return -1;
}

} // namespace

TEST(Phase10ServicesPropertyTest, MetaObjectPropertiesSignalsAndEnums) {
    const PropExpectation playback_props[] = {
        { "isPlaying",          false, false },
        { "playheadPosition",   false, false },
        { "durationSeconds",    false, false },
        { "timecode",           false, false },
        { "fps",                false, false },
        { "currentFrameSeq",    false, false },
        { "compositingEnabled", true,  false },
        { "hasGpuAcceleration", false, true  },
        { "gpuDeviceName",      false, true  },
        { "audioOutputStatus",  false, true  },
        { "leftLevelDb",        false, false },
        { "rightLevelDb",       false, false }
    };
    verify_meta_properties(ave::desktop::PlaybackService::staticMetaObject, playback_props, "PlaybackService");

    const PropExpectation media_pool_props[] = {
        { "assetCount",         false, false },
        { "isImporting",        false, false },
        { "activeImportCount",  false, false },
        { "pendingImportCount", false, false },
        { "lastImportError",    false, false }
    };
    verify_meta_properties(ave::desktop::MediaPoolModel::staticMetaObject, media_pool_props, "MediaPoolModel");
    EXPECT_LT(ave::desktop::MediaPoolModel::staticMetaObject.indexOfProperty("rowCount"), 0);

    const PropExpectation timeline_props[] = {
        { "trackCount",      false, false },
        { "durationSeconds", false, false },
        { "activeTool",      true,  false },
        { "snappingEnabled", true,  false },
        { "zoomFactor",      true,  false },
        { "selectedClipId",  true,  false },
        { "canUndo",         false, false },
        { "canRedo",         false, false },
        { "isDirty",         false, false },
        { "projectName",     false, false },
        { "projectFilePath", false, false },
        { "tracksModel",     false, true  },
        { "clipsModel",      false, true  }
    };
    verify_meta_properties(ave::desktop::TimelineModel::staticMetaObject, timeline_props, "TimelineModel");

    const PropExpectation inspector_props[] = {
        { "hasSelection",     false, false },
        { "clipName",         false, false },
        { "trackName",        false, false },
        { "durationTimecode", false, false },
        { "posX",             true,  false },
        { "posY",             true,  false },
        { "scaleX",           true,  false },
        { "scaleY",           true,  false },
        { "opacity",          true,  false },
        { "blendMode",        true,  false }
    };
    verify_meta_properties(ave::desktop::InspectorService::staticMetaObject, inspector_props, "InspectorService");

    const PropExpectation ai_studio_props[] = {
        { "isBusy",                        false, false },
        { "hasReference",                  false, false },
        { "structuralReferenceSimilarity", false, false },
        { "statusMessage",                 false, false },
        { "proposedOperations",            false, false },
        { "hasValidProposal",              false, false }
    };
    verify_meta_properties(ave::desktop::AiStudioService::staticMetaObject, ai_studio_props, "AiStudioService");

    // Verify TimelineModel::Tool Q_ENUM exact integer mapping
    const QMetaObject& tm_mo = ave::desktop::TimelineModel::staticMetaObject;
    const int enum_idx = tm_mo.indexOfEnumerator("Tool");
    ASSERT_GE(enum_idx, 0);
    const QMetaEnum tool_enum = tm_mo.enumerator(enum_idx);
    EXPECT_EQ(tool_enum.keyCount(), 4);
    EXPECT_EQ(tool_enum.keyToValue("Select"), 0);
    EXPECT_EQ(tool_enum.keyToValue("Razor"), 1);
    EXPECT_EQ(tool_enum.keyToValue("Trim"), 2);
    EXPECT_EQ(tool_enum.keyToValue("Slip"), 3);

    // Verify PlaybackService status strings and read-only playhead invariant
    ave::Project project("MetaObject Check");
    const std::string v1 = project.timeline().video_tracks()[0].id();
    project.media_pool_mut().register_asset(
        make_synthetic_av_asset("asset-meta", "meta.mp4", "meta.mp4", ave::Rational{2, 1}));
    project.timeline_mut().find_track_mut(v1)->add_item(
        ave::TimelineItem("clip-meta", "mc-meta", "asset-meta", "meta.mp4",
                          ave::Rational{0, 1}, ave::Rational{2, 1}, ave::Rational{0, 1}));

    auto store = std::make_shared<ave::PlaybackSnapshotStore>(project.timeline(), project.media_pool());
    auto hub = std::make_shared<ave::FramePresentationHub>();
    ave::PlaybackSession session(store, ave::PlaybackSessionConfig{}, hub);
    ave::desktop::PlaybackService playback(session, hub, store);
    EXPECT_FALSE(playback.gpuDeviceName().isEmpty());
    EXPECT_EQ(playback.audioOutputStatus(),
              QStringLiteral("NullAudioDevice (Simulated; Physical WASAPI Deferred)"));
    playback.seekSeconds(1.25);
    EXPECT_NEAR(playback.playheadPosition(), 1.25, 1e-6);
    playback.seekSeconds(-9.0);
    EXPECT_DOUBLE_EQ(playback.playheadPosition(), 0.0);
    playback.shutdown();
    session.shutdown();
    hub->shutdown();

    // Verify AiStudioService initial read-only/null-similarity contract
    ave::desktop::AiStudioService ai_studio(project, nullptr, store, nullptr);
    EXPECT_FALSE(ai_studio.isBusy());
    EXPECT_FALSE(ai_studio.hasReference());
    EXPECT_FALSE(ai_studio.structuralReferenceSimilarity().isValid());
    EXPECT_FALSE(ai_studio.hasValidProposal());
    EXPECT_EQ(ai_studio.statusMessage(), QStringLiteral("Ready for natural language prompt."));
    ai_studio.shutdown();

    // Verify sub-model roleNames
    ave::desktop::TimelineModel timeline_model(project);
    const QHash<int, QByteArray> track_roles = timeline_model.tracksModel()->roleNames();
    EXPECT_EQ(track_roles.size(), 6);
    EXPECT_EQ(track_roles.value(ave::desktop::TimelineTrackListModel::TrackIdRole), QByteArrayLiteral("trackId"));
    EXPECT_EQ(track_roles.value(ave::desktop::TimelineTrackListModel::TrackNameRole), QByteArrayLiteral("trackName"));
    EXPECT_EQ(track_roles.value(ave::desktop::TimelineTrackListModel::TrackTypeRole), QByteArrayLiteral("trackType"));
    EXPECT_EQ(track_roles.value(ave::desktop::TimelineTrackListModel::TrackIndexRole), QByteArrayLiteral("trackIndex"));
    EXPECT_EQ(track_roles.value(ave::desktop::TimelineTrackListModel::MutedRole), QByteArrayLiteral("muted"));
    EXPECT_EQ(track_roles.value(ave::desktop::TimelineTrackListModel::SoloRole), QByteArrayLiteral("solo"));

    const QHash<int, QByteArray> clip_roles = timeline_model.clipsModel()->roleNames();
    EXPECT_EQ(clip_roles.size(), 11);
    EXPECT_EQ(clip_roles.value(ave::desktop::TimelineClipListModel::ClipIdRole), QByteArrayLiteral("clipId"));
    EXPECT_EQ(clip_roles.value(ave::desktop::TimelineClipListModel::TrackIdRole), QByteArrayLiteral("trackId"));
    EXPECT_EQ(clip_roles.value(ave::desktop::TimelineClipListModel::AssetIdRole), QByteArrayLiteral("assetId"));
    EXPECT_EQ(clip_roles.value(ave::desktop::TimelineClipListModel::ClipNameRole), QByteArrayLiteral("clipName"));
    EXPECT_EQ(clip_roles.value(ave::desktop::TimelineClipListModel::TimelineInSecRole), QByteArrayLiteral("timelineInSec"));
    EXPECT_EQ(clip_roles.value(ave::desktop::TimelineClipListModel::DurationSecRole), QByteArrayLiteral("durationSec"));
    EXPECT_EQ(clip_roles.value(ave::desktop::TimelineClipListModel::SourceInSecRole), QByteArrayLiteral("sourceInSec"));
    EXPECT_EQ(clip_roles.value(ave::desktop::TimelineClipListModel::TrackRowRole), QByteArrayLiteral("trackRow"));
    EXPECT_EQ(clip_roles.value(ave::desktop::TimelineClipListModel::IsSelectedRole), QByteArrayLiteral("isSelected"));
    EXPECT_EQ(clip_roles.value(ave::desktop::TimelineClipListModel::HasWaveformRole), QByteArrayLiteral("hasWaveform"));
    EXPECT_EQ(clip_roles.value(ave::desktop::TimelineClipListModel::WaveformPeaksRole), QByteArrayLiteral("waveformPeaks"));
}

TEST(Phase10ServicesPropertyTest, TimelineModelModelsSelectionSnappingAndCommands) {
    ScopedTempDir tmp("ave_g05_timeline");

    ave::Project project("Editorial Tests");
    const std::string v1_id = project.timeline().video_tracks()[0].id();
    const std::string a1_id = project.timeline().audio_tracks()[0].id();

    auto cache = std::make_shared<ave::MediaCache>(tmp.path / "cache");
    const ave::MediaAsset cached_asset = make_synthetic_av_asset(
        "asset-cached", "Scene_01.mp4", tmp.path / "scene_01.mp4", ave::Rational{4, 1});
    const ave::MediaAsset silent_asset = make_synthetic_av_asset(
        "asset-silent", "MOS_Plate.mp4", tmp.path / "mos_plate.mp4", ave::Rational{2, 1});
    project.media_pool_mut().register_asset(cached_asset);
    project.media_pool_mut().register_asset(silent_asset);
    // Setup fixture assets belong to the baseline state before testing dirty transitions.
    project.mark_clean();
    cache->store_waveform("asset-cached", make_synthetic_waveform(ave::Rational{4, 1}));

    ave::desktop::TimelineModel model(project, nullptr, nullptr, cache);

    // Real tracks only, zero hardcoded mock clips
    EXPECT_EQ(model.trackCount(), static_cast<int>(project.timeline().video_tracks().size() +
                                                   project.timeline().audio_tracks().size()));
    EXPECT_EQ(model.tracksModel()->rowCount(), model.trackCount());
    EXPECT_EQ(model.clipsModel()->rowCount(), 0);
    EXPECT_DOUBLE_EQ(model.durationSeconds(), 0.0);
    EXPECT_FALSE(model.canUndo());
    EXPECT_FALSE(model.canRedo());
    EXPECT_FALSE(model.isDirty());
    EXPECT_EQ(model.projectName(), QStringLiteral("Editorial Tests"));
    EXPECT_EQ(model.tracksModel()->data(model.tracksModel()->index(0, 0),
                                        ave::desktop::TimelineTrackListModel::TrackTypeRole).toString(),
              QStringLiteral("video"));
    EXPECT_EQ(model.tracksModel()->data(model.tracksModel()->index(1, 0),
                                        ave::desktop::TimelineTrackListModel::TrackTypeRole).toString(),
              QStringLiteral("audio"));

    // activeTool valid/invalid bounds
    QSignalSpy tool_spy(&model, &ave::desktop::TimelineModel::activeToolChanged);
    model.setActiveTool(static_cast<int>(ave::desktop::TimelineModel::Tool::Razor));
    EXPECT_EQ(model.activeTool(), 1);
    EXPECT_EQ(tool_spy.count(), 1);
    model.setActiveTool(-1);
    model.setActiveTool(4);
    EXPECT_EQ(model.activeTool(), 1);
    EXPECT_EQ(tool_spy.count(), 1);
    model.setActiveTool(static_cast<int>(ave::desktop::TimelineModel::Tool::Select));
    EXPECT_EQ(model.activeTool(), 0);

    // zoomFactor clamping and NaN guard
    model.setZoomFactor(500.0);
    EXPECT_DOUBLE_EQ(model.zoomFactor(), 20.0);
    model.setZoomFactor(0.01);
    EXPECT_DOUBLE_EQ(model.zoomFactor(), 0.1);
    model.setZoomFactor(std::numeric_limits<double>::quiet_NaN());
    EXPECT_DOUBLE_EQ(model.zoomFactor(), 0.1);
    model.setZoomFactor(1.0);
    EXPECT_DOUBLE_EQ(model.zoomFactor(), 1.0);

    // Reject invalid insert requests without mutating project
    EXPECT_FALSE(model.insertClipFromAsset(QString(), QString::fromStdString(v1_id), 0.0));
    EXPECT_FALSE(model.insertClipFromAsset(QStringLiteral("missing-asset"), QString::fromStdString(v1_id), 0.0));
    EXPECT_FALSE(model.insertClipFromAsset(QStringLiteral("asset-cached"), QString::fromStdString(v1_id), -5.0));
    EXPECT_FALSE(model.insertClipFromAsset(QStringLiteral("asset-cached"), QString::fromStdString(v1_id),
                                           std::numeric_limits<double>::quiet_NaN()));
    EXPECT_EQ(model.clipsModel()->rowCount(), 0);
    EXPECT_FALSE(model.canUndo());

    // Append (-1.0 sentinel) + explicit placement on V1
    ASSERT_TRUE(model.insertClipFromAsset(QStringLiteral("asset-cached"), QString::fromStdString(v1_id), -1.0));
    EXPECT_EQ(model.clipsModel()->rowCount(), 1);
    EXPECT_DOUBLE_EQ(model.durationSeconds(), 4.0);
    EXPECT_TRUE(model.canUndo());
    EXPECT_TRUE(model.isDirty());

    const QModelIndex c0_idx = model.clipsModel()->index(0, 0);
    const QString c0_id = model.clipsModel()->data(c0_idx, ave::desktop::TimelineClipListModel::ClipIdRole).toString();
    EXPECT_TRUE(model.clipsModel()->data(c0_idx, ave::desktop::TimelineClipListModel::HasWaveformRole).toBool());
    const QVariantList peaks =
        model.clipsModel()->data(c0_idx, ave::desktop::TimelineClipListModel::WaveformPeaksRole).toList();
    ASSERT_FALSE(peaks.isEmpty());
    EXPECT_LE(peaks.size(), 128);
    for (const QVariant& peak_var : peaks) {
        EXPECT_GE(peak_var.toDouble(), 0.0);
        EXPECT_LE(peak_var.toDouble(), 1.0);
    }

    // Insert uncached silent asset at 6.0s -> honest hasWaveform = false, empty peaks
    ASSERT_TRUE(model.insertClipFromAsset(QStringLiteral("asset-silent"), QString::fromStdString(v1_id), 6.0));
    EXPECT_EQ(model.clipsModel()->rowCount(), 2);
    EXPECT_DOUBLE_EQ(model.durationSeconds(), 8.0);
    const int c1_row = 1;
    const QModelIndex c1_idx = model.clipsModel()->index(c1_row, 0);
    const QString c1_id = model.clipsModel()->data(c1_idx, ave::desktop::TimelineClipListModel::ClipIdRole).toString();
    EXPECT_FALSE(model.clipsModel()->data(c1_idx, ave::desktop::TimelineClipListModel::HasWaveformRole).toBool());
    EXPECT_TRUE(model.clipsModel()->data(c1_idx, ave::desktop::TimelineClipListModel::WaveformPeaksRole).toList().isEmpty());

    // Selection ignores nonexistent IDs and updates IsSelectedRole via dataChanged
    model.setSelectedClipId(QStringLiteral("nonexistent-clip"));
    EXPECT_TRUE(model.selectedClipId().isEmpty());

    QSignalSpy clips_data_spy(model.clipsModel(), &QAbstractItemModel::dataChanged);
    model.setSelectedClipId(c1_id);
    EXPECT_EQ(model.selectedClipId(), c1_id);
    EXPECT_GE(clips_data_spy.count(), 1);
    EXPECT_TRUE(model.clipsModel()->data(c1_idx, ave::desktop::TimelineClipListModel::IsSelectedRole).toBool());

    // Magnetic snapping ON (default 10px = 0.1s at zoom 1.0): moving near 4.0s snaps to 4.0s
    model.setSnappingEnabled(true);
    ASSERT_TRUE(model.moveSelectedClip(QString::fromStdString(v1_id), 4.05));
    int moved_row = find_clip_row_by_id(model.clipsModel(), c1_id);
    ASSERT_GE(moved_row, 0);
    EXPECT_DOUBLE_EQ(
        model.clipsModel()->data(model.clipsModel()->index(moved_row, 0),
                                 ave::desktop::TimelineClipListModel::TimelineInSecRole).toDouble(),
        4.0);

    // Snapping OFF: near-boundary move does NOT snap to 4.0s (quantizes to 98/24 ~= 4.0833s)
    model.setSnappingEnabled(false);
    ASSERT_TRUE(model.moveSelectedClip(QString::fromStdString(v1_id), 4.0 + 2.0 / 24.0));
    moved_row = find_clip_row_by_id(model.clipsModel(), c1_id);
    ASSERT_GE(moved_row, 0);
    EXPECT_NEAR(
        model.clipsModel()->data(model.clipsModel()->index(moved_row, 0),
                                 ave::desktop::TimelineClipListModel::TimelineInSecRole).toDouble(),
        98.0 / 24.0, 1e-6);

    // Overlap collision rejected cleanly
    EXPECT_FALSE(model.moveSelectedClip(QString::fromStdString(v1_id), 1.0));

    // Split selected clip at 5.0s -> clip count 2 -> 3, selection clears (split replaces original ID with two new slice IDs)
    QSignalSpy sel_spy(&model, &ave::desktop::TimelineModel::selectedClipIdChanged);
    ASSERT_TRUE(model.splitAtPlayhead(5.0));
    EXPECT_EQ(model.clipsModel()->rowCount(), 3);
    EXPECT_TRUE(model.selectedClipId().isEmpty());
    EXPECT_EQ(sel_spy.count(), 1);

    // Undo split -> returns to 2 clips; Redo split -> 3 clips
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.clipsModel()->rowCount(), 2);
    ASSERT_TRUE(model.redo());
    EXPECT_EQ(model.clipsModel()->rowCount(), 3);

    // Select first clip and deleteSelected -> clears selectedClipId; Undo restores
    model.setSelectedClipId(c0_id);
    ASSERT_TRUE(model.deleteSelected());
    EXPECT_EQ(model.clipsModel()->rowCount(), 2);
    EXPECT_TRUE(model.selectedClipId().isEmpty());
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.clipsModel()->rowCount(), 3);

    // Toggle track mute via SetTrackMuteCommand & verify TracksModel MutedRole + undo/redo
    ASSERT_TRUE(model.toggleTrackMute(QString::fromStdString(a1_id)));
    EXPECT_TRUE(project.timeline().find_track(a1_id)->muted());
    EXPECT_TRUE(model.tracksModel()->data(model.tracksModel()->index(1, 0),
                                          ave::desktop::TimelineTrackListModel::MutedRole).toBool());
    ASSERT_TRUE(model.undo());
    EXPECT_FALSE(project.timeline().find_track(a1_id)->muted());

    // Verify fractional timebase 30000/1001 insert quantization compiles and executes cleanly
    ave::Project ntsc_proj("NTSC Project");
    ntsc_proj.timeline_mut() = ave::Timeline("tl-ntsc", "NTSC Sequence", ave::Rational{30000, 1001});
    ntsc_proj.media_pool_mut().register_asset(
        make_synthetic_av_asset("asset-ntsc", "ntsc.mp4", "ntsc.mp4", ave::Rational{3, 1}));
    ave::desktop::TimelineModel ntsc_model(ntsc_proj);
    EXPECT_TRUE(ntsc_model.insertClipFromAsset(QStringLiteral("asset-ntsc"), QString(), 1.0));
    EXPECT_EQ(ntsc_model.clipsModel()->rowCount(), 1);
}

TEST(Phase10ServicesPropertyTest, InspectorServiceTransformsLoopSuppressionAndLifetime) {
    ave::Project project("Inspector Test");
    const std::string v1_id = project.timeline().video_tracks()[0].id();
    project.media_pool_mut().register_asset(
        make_synthetic_av_asset("asset-insp", "Interview_A.mov", "interview.mov", ave::Rational{4, 1}));

    ave::desktop::TimelineModel timeline_model(project);
    ave::desktop::InspectorService inspector(project, timeline_model);

    // Empty selection state
    EXPECT_FALSE(inspector.hasSelection());
    EXPECT_TRUE(inspector.clipName().isEmpty());
    EXPECT_TRUE(inspector.trackName().isEmpty());
    EXPECT_EQ(inspector.durationTimecode(), QStringLiteral("00:00:00:00"));
    EXPECT_DOUBLE_EQ(inspector.posX(), 0.0);
    EXPECT_DOUBLE_EQ(inspector.posY(), 0.0);
    EXPECT_DOUBLE_EQ(inspector.scaleX(), 1.0);
    EXPECT_DOUBLE_EQ(inspector.scaleY(), 1.0);
    EXPECT_DOUBLE_EQ(inspector.opacity(), 1.0);
    EXPECT_EQ(inspector.blendMode(), QStringLiteral("Over"));

    // No-op when no selection exists
    inspector.setPosX(50.0);
    inspector.resetTransforms();
    EXPECT_EQ(project.command_history().undo_count(), 0u);

    // Insert clip and select it
    ASSERT_TRUE(timeline_model.insertClipFromAsset(QStringLiteral("asset-insp"), QString::fromStdString(v1_id), 0.0));
    const QString clip_id = timeline_model.clipsModel()->data(
        timeline_model.clipsModel()->index(0, 0),
        ave::desktop::TimelineClipListModel::ClipIdRole).toString();

    QSignalSpy sel_spy(&inspector, &ave::desktop::InspectorService::selectionChanged);
    timeline_model.setSelectedClipId(clip_id);
    EXPECT_TRUE(inspector.hasSelection());
    EXPECT_GE(sel_spy.count(), 1);
    EXPECT_EQ(inspector.clipName(), QStringLiteral("Interview_A.mov"));
    EXPECT_EQ(inspector.trackName(), QStringLiteral("V1"));
    EXPECT_EQ(inspector.durationTimecode().toStdString(), "00:00:04:00");

    // Transform edits route through SetTransformCommand
    QSignalSpy tf_spy(&inspector, &ave::desktop::InspectorService::transformChanged);
    inspector.setPosX(120.0);
    inspector.setPosY(-45.0);
    inspector.setScaleX(0.5);
    inspector.setScaleY(0.75);
    inspector.setOpacity(0.8);
    inspector.setBlendMode(QStringLiteral("Multiply"));

    EXPECT_NEAR(inspector.posX(), 120.0, 1e-4);
    EXPECT_NEAR(inspector.posY(), -45.0, 1e-4);
    EXPECT_NEAR(inspector.scaleX(), 0.5, 1e-4);
    EXPECT_NEAR(inspector.scaleY(), 0.75, 1e-4);
    EXPECT_NEAR(inspector.opacity(), 0.8, 1e-4);
    EXPECT_EQ(inspector.blendMode(), QStringLiteral("Multiply"));
    EXPECT_GE(tf_spy.count(), 6);

    const std::size_t undos_before_noop = project.command_history().undo_count();

    // Slider feedback-loop suppression (< 1e-4 delta) + invalid value rejection
    inspector.setPosX(120.0 + 5e-5);
    inspector.setPosY(-45.0 - 5e-5);
    inspector.setScaleX(0.5 + 5e-5);
    inspector.setScaleY(0.75 - 5e-5);
    inspector.setOpacity(0.8 + 5e-5);
    inspector.setBlendMode(QStringLiteral("Multiply"));
    EXPECT_EQ(project.command_history().undo_count(), undos_before_noop);

    inspector.setPosX(std::numeric_limits<double>::quiet_NaN());
    inspector.setPosY(std::numeric_limits<double>::infinity());
    inspector.setScaleX(0.0);
    inspector.setScaleY(-1.0);
    inspector.setOpacity(-0.1);
    inspector.setOpacity(1.5);
    inspector.setBlendMode(QStringLiteral("InvalidBlend"));
    EXPECT_EQ(project.command_history().undo_count(), undos_before_noop);
    EXPECT_EQ(inspector.blendMode(), QStringLiteral("Multiply"));

    // Undo / redo synchronizes inspector properties
    ASSERT_TRUE(timeline_model.undo()); // Reverts blendMode Multiply -> Over
    EXPECT_EQ(inspector.blendMode(), QStringLiteral("Over"));
    ASSERT_TRUE(timeline_model.redo());
    EXPECT_EQ(inspector.blendMode(), QStringLiteral("Multiply"));

    // resetTransforms executes as a single atomic command and skips if already default
    const std::size_t count_before_reset = project.command_history().undo_count();
    inspector.resetTransforms();
    EXPECT_EQ(project.command_history().undo_count(), count_before_reset + 1);
    EXPECT_DOUBLE_EQ(inspector.posX(), 0.0);
    EXPECT_DOUBLE_EQ(inspector.posY(), 0.0);
    EXPECT_DOUBLE_EQ(inspector.scaleX(), 1.0);
    EXPECT_DOUBLE_EQ(inspector.scaleY(), 1.0);
    EXPECT_DOUBLE_EQ(inspector.opacity(), 1.0);
    EXPECT_EQ(inspector.blendMode(), QStringLiteral("Over"));

    // Calling resetTransforms again while default does NOT push another command
    inspector.resetTransforms();
    EXPECT_EQ(project.command_history().undo_count(), count_before_reset + 1);

    // Undoing reset restores modified values in 1 step
    ASSERT_TRUE(timeline_model.undo());
    EXPECT_NEAR(inspector.posX(), 120.0, 1e-4);
    EXPECT_NEAR(inspector.opacity(), 0.8, 1e-4);
    EXPECT_EQ(inspector.blendMode(), QStringLiteral("Multiply"));

    // Listener unregistration safety when InspectorService is destroyed before Project
    {
        ave::desktop::InspectorService temp_inspector(project, timeline_model);
        EXPECT_TRUE(temp_inspector.hasSelection());
    }
    EXPECT_TRUE(timeline_model.undo());
    EXPECT_TRUE(timeline_model.redo());
}

TEST(Phase10ServicesPropertyTest, ProjectPersistenceOtioExportAndRelinkWorkflow) {
    const std::filesystem::path fixture = fixture_file("sample_test.mp4");
    ASSERT_TRUE(std::filesystem::exists(fixture));

    ScopedTempDir tmp("ave_g05_persist");
    const std::filesystem::path src_dir = tmp.path / "src_media";
    const std::filesystem::path moved_dir = tmp.path / "moved_media";
    std::filesystem::create_directories(src_dir);
    std::filesystem::create_directories(moved_dir);

    const std::filesystem::path copied_media = src_dir / "sample_test.mp4";
    std::filesystem::copy_file(fixture, copied_media, std::filesystem::copy_options::overwrite_existing);
    const uint64_t media_bytes = std::filesystem::file_size(copied_media);

    const std::filesystem::path proj_file = tmp.path / "saved_proj.aveproj";
    const std::filesystem::path otio_file = tmp.path / "exports" / "sequence.otio";

    ave::Project project("Persisted Edit");
    auto store = std::make_shared<ave::PlaybackSnapshotStore>(project.timeline(), project.media_pool());
    ave::ProjectSnapshotCoordinator coordinator(project, store);
    auto import_queue = std::make_shared<ave::MediaImportQueue>();
    ave::desktop::MediaPoolModel pool_model(project, import_queue);
    ave::desktop::TimelineModel timeline_model(project, nullptr, &coordinator);
    ave::desktop::InspectorService inspector(project, timeline_model);

    QObject::connect(&timeline_model, &ave::desktop::TimelineModel::projectReloaded,
                     &pool_model, &ave::desktop::MediaPoolModel::reloadFromProject);

    project.media_pool_mut().register_asset(
        make_synthetic_av_asset("asset-persist", "sample_test.mp4", copied_media, ave::Rational{2, 1}, media_bytes));
    pool_model.reloadFromProject();
    ASSERT_EQ(pool_model.assetCount(), 1);
    EXPECT_TRUE(pool_model.data(pool_model.index(0, 0), ave::desktop::MediaPoolModel::IsOnlineRole).toBool());
    const std::string v1_id = project.timeline().video_tracks()[0].id();

    ASSERT_TRUE(timeline_model.insertClipFromAsset(QStringLiteral("asset-persist"), QString::fromStdString(v1_id), 0.0));
    const QString clip_id = timeline_model.clipsModel()->data(
        timeline_model.clipsModel()->index(0, 0),
        ave::desktop::TimelineClipListModel::ClipIdRole).toString();
    timeline_model.setSelectedClipId(clip_id);
    inspector.setPosX(42.0);
    inspector.setOpacity(0.65);
    ASSERT_TRUE(timeline_model.toggleTrackMute(QString::fromStdString(v1_id)));
    EXPECT_TRUE(timeline_model.isDirty());

    // Save project
    ASSERT_TRUE(timeline_model.saveProject(QString::fromStdWString(proj_file.wstring())));
    EXPECT_FALSE(timeline_model.isDirty());
    EXPECT_EQ(timeline_model.projectFilePath(), QString::fromStdWString(proj_file.wstring()));

    // Export OTIO and verify valid RFC 8259 JSON parseable by QJsonDocument (incl Windows backslashes)
    ASSERT_TRUE(timeline_model.exportOtio(QString::fromStdWString(otio_file.wstring())));
    std::ifstream otio_ifs(otio_file, std::ios::binary);
    ASSERT_TRUE(otio_ifs.is_open());
    const std::string otio_text((std::istreambuf_iterator<char>(otio_ifs)), std::istreambuf_iterator<char>());
    QJsonParseError otio_err{};
    const QJsonDocument otio_doc = QJsonDocument::fromJson(QByteArray::fromStdString(otio_text), &otio_err);
    ASSERT_EQ(otio_err.error, QJsonParseError::NoError) << otio_err.errorString().toStdString();
    ASSERT_TRUE(otio_doc.isObject());
    EXPECT_EQ(otio_doc.object().value(QStringLiteral("OTIO_SCHEMA")).toString(), QStringLiteral("Timeline.1"));

    // newProject clears timeline & marks clean
    QSignalSpy reload_spy(&timeline_model, &ave::desktop::TimelineModel::projectReloaded);
    ASSERT_TRUE(timeline_model.newProject(QStringLiteral("Fresh Workspace")));
    EXPECT_EQ(reload_spy.count(), 1);
    EXPECT_EQ(timeline_model.projectName(), QStringLiteral("Fresh Workspace"));
    EXPECT_EQ(timeline_model.clipsModel()->rowCount(), 0);
    EXPECT_FALSE(timeline_model.isDirty());
    EXPECT_TRUE(timeline_model.selectedClipId().isEmpty());

    // Corrupt / missing openProject preserves active project safely
    EXPECT_FALSE(timeline_model.openProject(QString::fromStdWString((tmp.path / "does_not_exist.aveproj").wstring())));
    const std::filesystem::path bad_file = tmp.path / "corrupt.aveproj";
    {
        std::ofstream bad_ofs(bad_file, std::ios::trunc);
        bad_ofs << "{\"unexpected\": true}";
    }
    EXPECT_FALSE(timeline_model.openProject(QString::fromStdWString(bad_file.wstring())));
    EXPECT_EQ(timeline_model.projectName(), QStringLiteral("Fresh Workspace"));

    // Move source media so opened project initially points to missing file
    const std::filesystem::path relocated_media = moved_dir / "sample_test.mp4";
    std::filesystem::rename(copied_media, relocated_media);
    EXPECT_FALSE(std::filesystem::exists(copied_media));

    // Reopen saved project via file:/// URL -> MediaPoolModel re-evaluates actual disk availability (offline!)
    const QString file_url = QUrl::fromLocalFile(QString::fromStdWString(proj_file.wstring())).toString();
    ASSERT_TRUE(timeline_model.openProject(file_url));
    EXPECT_EQ(reload_spy.count(), 2);
    EXPECT_EQ(pool_model.assetCount(), 1);
    EXPECT_FALSE(pool_model.data(pool_model.index(0, 0), ave::desktop::MediaPoolModel::IsOnlineRole).toBool());
    EXPECT_EQ(timeline_model.projectName(), QStringLiteral("Persisted Edit"));
    EXPECT_EQ(timeline_model.clipsModel()->rowCount(), 1);
    EXPECT_FALSE(timeline_model.isDirty());
    EXPECT_FALSE(timeline_model.canUndo());
    EXPECT_TRUE(project.timeline().video_tracks()[0].muted());
    EXPECT_TRUE(timeline_model.tracksModel()->data(
        timeline_model.tracksModel()->index(0, 0),
        ave::desktop::TimelineTrackListModel::MutedRole).toBool());

    const QString restored_clip_id = timeline_model.clipsModel()->data(
        timeline_model.clipsModel()->index(0, 0),
        ave::desktop::TimelineClipListModel::ClipIdRole).toString();
    timeline_model.setSelectedClipId(restored_clip_id);
    EXPECT_NEAR(inspector.posX(), 42.0, 1e-4);
    EXPECT_NEAR(inspector.opacity(), 0.65, 1e-4);

    // AssetRelinker recovery via relinkOfflineMedia -> republishes PlaybackSnapshot & updates IsOnlineRole
    const uint64_t version_before_relink = store->current_version();
    const int relinked = timeline_model.relinkOfflineMedia(QString::fromStdWString(moved_dir.wstring()));
    EXPECT_EQ(relinked, 1);
    EXPECT_EQ(reload_spy.count(), 3);
    EXPECT_GT(store->current_version(), version_before_relink);
    EXPECT_TRUE(pool_model.data(pool_model.index(0, 0), ave::desktop::MediaPoolModel::IsOnlineRole).toBool());

    const ave::PlaybackSnapshotPtr post_relink_snap = store->load();
    ASSERT_NE(post_relink_snap, nullptr);
    const ave::MediaAsset* snap_asset = post_relink_snap->find_asset("asset-persist");
    ASSERT_NE(snap_asset, nullptr);
    EXPECT_EQ(snap_asset->file_path(), relocated_media);

    coordinator.unbind();
    pool_model.shutdown();
}

TEST(Phase10ServicesPropertyTest, PlaybackSnapshotRepublishAndConcurrentReaderSafety) {
    ScopedTempDir tmp("ave_g05_mt");

    ave::Project project("Concurrent Snapshot Test");
    project.media_pool_mut().register_asset(
        make_synthetic_av_asset("asset-mt", "mt.mp4", "mt.mp4", ave::Rational{10, 1}));

    auto store = std::make_shared<ave::PlaybackSnapshotStore>(project.timeline(), project.media_pool());
    auto hub = std::make_shared<ave::FramePresentationHub>();
    ave::PlaybackSession session(store, ave::PlaybackSessionConfig{}, hub);
    ave::ProjectSnapshotCoordinator coordinator(project, store, &session);

    auto import_queue = std::make_shared<ave::MediaImportQueue>();
    ave::desktop::MediaPoolModel pool_model(project, import_queue);
    ave::desktop::TimelineModel timeline_model(project, nullptr, &coordinator);
    ave::desktop::InspectorService inspector(project, timeline_model);

    QObject::connect(&timeline_model, &ave::desktop::TimelineModel::projectReloaded,
                     &pool_model, &ave::desktop::MediaPoolModel::reloadFromProject);

    const uint64_t initial_version = store->current_version();
    std::atomic<bool> stop_reader{false};
    std::atomic<uint64_t> reads_completed{0};
    std::atomic<uint64_t> max_seen_version{initial_version};

    std::thread reader([&]() {
        uint64_t prev_version = 0;
        while (!stop_reader.load(std::memory_order_acquire)) {
            const ave::PlaybackSnapshotPtr snap = store->load();
            if (snap) {
                EXPECT_GE(snap->version, prev_version);
                prev_version = snap->version;
                uint64_t expected_max = max_seen_version.load(std::memory_order_relaxed);
                while (snap->version > expected_max &&
                       !max_seen_version.compare_exchange_weak(expected_max, snap->version)) {
                }
                for (const auto& trk : snap->timeline.video_tracks()) {
                    for (const auto& item : trk.items()) {
                        (void)snap->find_asset(item.asset_id());
                    }
                }
                reads_completed.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::yield();
        }
    });

    const std::string v1_id = project.timeline().video_tracks()[0].id();
    ASSERT_TRUE(timeline_model.insertClipFromAsset(QStringLiteral("asset-mt"), QString::fromStdString(v1_id), 0.0));
    const QString clip_id = timeline_model.clipsModel()->data(
        timeline_model.clipsModel()->index(0, 0),
        ave::desktop::TimelineClipListModel::ClipIdRole).toString();
    timeline_model.setSelectedClipId(clip_id);

    for (int i = 0; i < 15; ++i) {
        inspector.setPosX(static_cast<double>(i + 1) * 5.0);
        inspector.setOpacity(0.5 + 0.02 * static_cast<double>(i));
    }
    ASSERT_TRUE(timeline_model.moveSelectedClip(QString::fromStdString(v1_id), 2.0));
    ASSERT_TRUE(timeline_model.splitAtPlayhead(6.0));
    ASSERT_TRUE(timeline_model.toggleTrackMute(QString::fromStdString(v1_id)));
    ASSERT_TRUE(timeline_model.undo());
    ASSERT_TRUE(timeline_model.redo());

    const std::filesystem::path save_path = tmp.path / "mt_proj.aveproj";
    ASSERT_TRUE(timeline_model.saveProject(QString::fromStdWString(save_path.wstring())));
    ASSERT_TRUE(timeline_model.newProject(QStringLiteral("ResetMidRead")));
    EXPECT_EQ(pool_model.assetCount(), 0);
    ASSERT_TRUE(timeline_model.openProject(QString::fromStdWString(save_path.wstring())));
    EXPECT_EQ(pool_model.assetCount(), 1);
    ASSERT_EQ(timeline_model.clipsModel()->rowCount(), 2);

    // Verify listeners rebound cleanly after Coordinator::replace_project()
    const QString reloaded_clip = timeline_model.clipsModel()->data(
        timeline_model.clipsModel()->index(0, 0),
        ave::desktop::TimelineClipListModel::ClipIdRole).toString();
    timeline_model.setSelectedClipId(reloaded_clip);
    EXPECT_TRUE(inspector.hasSelection());
    inspector.setPosX(77.0);
    EXPECT_TRUE(timeline_model.canUndo());
    EXPECT_NEAR(inspector.posX(), 77.0, 1e-4);

    // Wait briefly so reader observes latest updates
    const auto wait_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (reads_completed.load(std::memory_order_relaxed) < 50 &&
           std::chrono::steady_clock::now() < wait_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    stop_reader.store(true, std::memory_order_release);
    reader.join();

    EXPECT_GT(reads_completed.load(), 0u);
    EXPECT_GT(store->current_version(), initial_version);

    coordinator.unbind();
    pool_model.shutdown();
    session.shutdown();
    hub->shutdown();
}

TEST(Phase10ServicesPropertyTest, M13AUnifiedDirtyTrackingDesktopIntegration) {
    ScopedTempDir tmp("ave_m13a_desktop");
    const std::filesystem::path sample = fixture_file("sample_test.mp4");
    ASSERT_TRUE(std::filesystem::exists(sample)) << "Missing fixture: " << sample;

    ave::Project project("Desktop Dirty Integration");
    auto store = std::make_shared<ave::PlaybackSnapshotStore>(project.timeline(), project.media_pool());
    auto hub = std::make_shared<ave::FramePresentationHub>();
    ave::PlaybackSession session(store, ave::PlaybackSessionConfig{}, hub);
    ave::ProjectSnapshotCoordinator coordinator(project, store, &session);

    auto cache = std::make_shared<ave::MediaCache>(tmp.path / "cache");
    auto queue = std::make_shared<ave::MediaImportQueue>();
    ave::desktop::MediaPoolModel pool_model(project, queue, nullptr, cache);
    ave::desktop::TimelineModel timeline_model(project, nullptr, &coordinator, cache);

    QObject::connect(&pool_model, &ave::desktop::MediaPoolModel::projectMediaMutated, &pool_model,
                     [&coordinator]() { coordinator.notify_media_mutated(); });
    QObject::connect(&timeline_model, &ave::desktop::TimelineModel::projectReloaded,
                     &pool_model, &ave::desktop::MediaPoolModel::reloadFromProject);

    QSignalSpy project_state_spy(&timeline_model, &ave::desktop::TimelineModel::projectStateChanged);

    // 1. Clean start & transient UI state changes do NOT mark dirty
    EXPECT_FALSE(project.is_dirty());
    EXPECT_FALSE(timeline_model.isDirty());
    timeline_model.setActiveTool(static_cast<int>(ave::desktop::TimelineModel::Tool::Razor));
    timeline_model.setSnappingEnabled(false);
    timeline_model.setZoomFactor(2.5);
    EXPECT_FALSE(project.is_dirty());
    EXPECT_FALSE(timeline_model.isDirty());
    EXPECT_EQ(project_state_spy.count(), 0);

    // 2. Direct PlaybackSnapshotStore::publish & notify_media_mutated without real edit do NOT dirty
    const uint64_t snap_v0 = store->current_version();
    (void)store->publish(project.timeline(), project.media_pool());
    coordinator.notify_media_mutated();
    EXPECT_GT(store->current_version(), snap_v0);
    EXPECT_FALSE(project.is_dirty());
    EXPECT_FALSE(timeline_model.isDirty());
    EXPECT_EQ(project_state_spy.count(), 0);

    // 3. Real import through MediaPoolModel marks BOTH Project and TimelineModel::isDirty + emits projectStateChanged
    const std::filesystem::path clip_v1 = tmp.path / "desk_clip_v1.mp4";
    std::filesystem::copy_file(sample, clip_v1, std::filesystem::copy_options::overwrite_existing);

    const QString import_job = pool_model.importMediaFile(QString::fromStdWString(clip_v1.wstring()));
    ASSERT_FALSE(import_job.isEmpty());
    const auto wait_start = std::chrono::steady_clock::now();
    while ((pool_model.assetCount() < 1 || pool_model.isImporting()) &&
           (std::chrono::steady_clock::now() - wait_start) < std::chrono::seconds(5)) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(pool_model.assetCount(), 1);
    EXPECT_TRUE(project.is_dirty());
    EXPECT_TRUE(timeline_model.isDirty());
    EXPECT_GE(project_state_spy.count(), 1);

    const QString imported_asset_id =
        pool_model.data(pool_model.index(0, 0), ave::desktop::MediaPoolModel::AssetIdRole).toString();
    ASSERT_FALSE(imported_asset_id.isEmpty());

    // 4. Failed saveProject preserves isDirty; successful saveProject clears BOTH Project::is_dirty & TimelineModel::isDirty
    const std::filesystem::path blocker = tmp.path / "file_blocker";
    {
        std::ofstream ofs(blocker, std::ios::out | std::ios::binary | std::ios::trunc);
        ofs << "block";
    }
    EXPECT_FALSE(timeline_model.saveProject(QString::fromStdWString((blocker / "bad.aveproj").wstring())));
    EXPECT_TRUE(project.is_dirty());
    EXPECT_TRUE(timeline_model.isDirty());

    const std::filesystem::path proj_path = tmp.path / "desktop_m13a.aveproj";
    ASSERT_TRUE(timeline_model.saveProject(QString::fromStdWString(proj_path.wstring())));
    EXPECT_FALSE(project.is_dirty());
    EXPECT_FALSE(timeline_model.isDirty());

    // 5. Rejected locked-track TimelineModel actions & invalid UI requests do NOT mark dirty
    const std::string v1_id = project.timeline().video_tracks()[0].id();
    project.timeline_mut().find_track_mut(v1_id)->set_locked(true);
    EXPECT_FALSE(timeline_model.insertClipFromAsset(imported_asset_id, QString::fromStdString(v1_id), 0.0));
    EXPECT_FALSE(project.is_dirty());
    EXPECT_FALSE(timeline_model.isDirty());
    project.timeline_mut().find_track_mut(v1_id)->set_locked(false);

    // 6. MediaPool bin/subclip edits & Project::set_name propagate to TimelineModel::isDirty
    const int dirty_sig_before_bin = project_state_spy.count();
    const std::string b_dailies = project.media_pool_mut().create_bin("Dailies");
    EXPECT_TRUE(timeline_model.isDirty());
    EXPECT_GT(project_state_spy.count(), dirty_sig_before_bin);
    ASSERT_TRUE(timeline_model.saveProject());
    EXPECT_FALSE(timeline_model.isDirty());

    EXPECT_TRUE(project.media_pool_mut().rename_bin(b_dailies, "Day 01"));
    EXPECT_TRUE(timeline_model.isDirty());
    ASSERT_TRUE(timeline_model.saveProject());
    EXPECT_FALSE(timeline_model.isDirty());

    project.set_name("Desktop Dirty Renamed");
    EXPECT_TRUE(timeline_model.isDirty());
    EXPECT_EQ(timeline_model.projectName(), QStringLiteral("Desktop Dirty Renamed"));
    ASSERT_TRUE(timeline_model.saveProject());
    EXPECT_FALSE(timeline_model.isDirty());

    // 7. Relink via ProjectSnapshotCoordinator: no-op directory & same-path stay clean; real path change marks dirty
    const ave::RelinkReport noop_relink = coordinator.auto_relink_directory(tmp.path, true);
    EXPECT_EQ(noop_relink.total_checked, 0u);
    EXPECT_FALSE(project.is_dirty());
    EXPECT_FALSE(timeline_model.isDirty());

    EXPECT_TRUE(coordinator.relink_asset(imported_asset_id.toStdString(), clip_v1));
    EXPECT_FALSE(project.is_dirty());
    EXPECT_FALSE(timeline_model.isDirty());

    const std::filesystem::path clip_v2 = tmp.path / "relocated" / "desk_clip_v1.mp4";
    std::filesystem::create_directories(clip_v2.parent_path());
    std::filesystem::rename(clip_v1, clip_v2);
    ASSERT_TRUE(coordinator.relink_asset(imported_asset_id.toStdString(), clip_v2));
    EXPECT_TRUE(project.is_dirty());
    EXPECT_TRUE(timeline_model.isDirty());
    ASSERT_TRUE(timeline_model.saveProject());
    EXPECT_FALSE(timeline_model.isDirty());

    // 8. Cache writes + reloadFromProject do NOT dirty clean project
    ave::ThumbnailData td;
    td.width = 4;
    td.height = 4;
    td.timestamp = ave::Rational{0, 1};
    td.rgba_pixels.assign(static_cast<std::size_t>(4 * 4 * 4), 200);
    cache->store_thumbnail(imported_asset_id.toStdString(), ave::Rational{0, 1}, 4, 4, std::move(td));
    EXPECT_GE(cache->thumbnail_count(), 1u);
    EXPECT_TRUE(cache->get_thumbnail(imported_asset_id.toStdString(), ave::Rational{0, 1}, 4, 4).has_value());
    pool_model.reloadFromProject();
    EXPECT_FALSE(project.is_dirty());
    EXPECT_FALSE(timeline_model.isDirty());

    // 9. Mixed MediaPool + Timeline command undo through TimelineModel keeps isDirty = true until saved
    const std::string sub_id = project.media_pool_mut().create_clip(
        imported_asset_id.toStdString(), b_dailies, "Hero Subclip");
    ASSERT_FALSE(sub_id.empty());
    EXPECT_TRUE(timeline_model.isDirty());

    ASSERT_TRUE(timeline_model.insertClipFromAsset(imported_asset_id, QString::fromStdString(v1_id), 0.0));
    EXPECT_TRUE(timeline_model.canUndo());
    EXPECT_TRUE(timeline_model.isDirty());
    ASSERT_TRUE(timeline_model.undo());
    EXPECT_FALSE(timeline_model.canUndo());
    EXPECT_TRUE(timeline_model.isDirty()); // Unsaved subclip remains dirty!

    ASSERT_TRUE(timeline_model.saveProject());
    EXPECT_FALSE(timeline_model.isDirty());

    // Pure command -> save -> undo -> redo symmetry on TimelineModel
    ASSERT_TRUE(timeline_model.insertClipFromAsset(imported_asset_id, QString::fromStdString(v1_id), 0.0));
    EXPECT_TRUE(timeline_model.isDirty());
    ASSERT_TRUE(timeline_model.undo());
    EXPECT_FALSE(timeline_model.isDirty());
    ASSERT_TRUE(timeline_model.redo());
    EXPECT_TRUE(timeline_model.isDirty());
    ASSERT_TRUE(timeline_model.saveProject());
    EXPECT_FALSE(timeline_model.isDirty());

    // 10. newProject & openProject start clean, and post-open edit marks dirty again
    ASSERT_TRUE(timeline_model.newProject(QStringLiteral("Fresh Clean")));
    EXPECT_FALSE(project.is_dirty());
    EXPECT_FALSE(timeline_model.isDirty());
    EXPECT_EQ(pool_model.assetCount(), 0);

    ASSERT_TRUE(timeline_model.openProject(QString::fromStdWString(proj_path.wstring())));
    EXPECT_FALSE(project.is_dirty());
    EXPECT_FALSE(timeline_model.isDirty());
    EXPECT_EQ(pool_model.assetCount(), 1);
    EXPECT_EQ(timeline_model.clipsModel()->rowCount(), 1);

    // Selecting clip after open does NOT dirty; mutating does
    const QString opened_clip_id = timeline_model.clipsModel()->data(
        timeline_model.clipsModel()->index(0, 0),
        ave::desktop::TimelineClipListModel::ClipIdRole).toString();
    timeline_model.setSelectedClipId(opened_clip_id);
    EXPECT_FALSE(timeline_model.isDirty());

    EXPECT_TRUE(project.media_pool_mut().set_clip_color_tag(sub_id, "Rose"));
    EXPECT_TRUE(project.is_dirty());
    EXPECT_TRUE(timeline_model.isDirty());

    coordinator.unbind();
    pool_model.shutdown();
    session.shutdown();
    hub->shutdown();
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
