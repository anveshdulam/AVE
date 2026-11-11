#include "ave/ai_studio_service.hpp"
#include "ave/command.hpp"
#include "ave/edit_proposal.hpp"
#include "ave/ffmpeg_probe.hpp"
#include "ave/media_asset.hpp"
#include "ave/media_pool.hpp"
#include "ave/perception_report.hpp"
#include "ave/playback_snapshot.hpp"
#include "ave/project.hpp"
#include "ave/proposal_bridge.hpp"
#include "ave/reference_fingerprint.hpp"
#include "ave/stream_decoder.hpp"
#include "ave/timeline.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QMetaType>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QUrl>

#ifdef _WIN32
#include <process.h>
#endif

namespace ave::desktop {

class AiStudioServiceTestAccessor {
public:
    static void seed_reference(AiStudioService& service,
                               const ave::ReferenceFingerprint& fp,
                               const ave::AudioRhythmReport& rhythm,
                               bool emit_signals = true) {
        const bool was_ref = service.has_reference_;
        service.has_reference_ = true;
        service.reference_fingerprint_ = fp;
        service.reference_rhythm_ = rhythm;
        service.recompute_similarity_from_snapshot(service.obtain_snapshot(), emit_signals);
        if (emit_signals && !was_ref) {
            emit service.hasReferenceChanged();
        }
    }

    static void seed_proposal(AiStudioService& service,
                              ave::EditProposalEnvelope envelope,
                              QStringList formatted_operations) {
        service.active_proposal_ = std::move(envelope);
        service.proposed_operations_ = std::move(formatted_operations);
        service.has_valid_proposal_ = true;
        emit service.proposalChanged();
    }
};

} // namespace ave::desktop

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
        path = std::filesystem::temp_directory_path() /
               (prefix + "_" + std::to_string(pid) + "_" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

template <typename Pred>
bool pump_events_until(Pred&& pred,
                       std::chrono::milliseconds timeout = std::chrono::milliseconds(15000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            return pred();
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

ave::MediaAsset make_synthetic_video_asset(const std::string& id,
                                           const std::string& name,
                                           const std::filesystem::path& file_path,
                                           ave::Rational duration) {
    std::vector<ave::SourceStream> streams;
    ave::SourceStream vs(0, ave::StreamType::Video, "h264", ave::Rational{1, 24},
                         duration.to_frames(ave::Rational{24, 1}));
    ave::VideoStreamInfo vi;
    vi.width = 1280;
    vi.height = 720;
    vi.frame_rate = ave::Rational{24, 1};
    vs.set_video_info(vi);
    streams.push_back(std::move(vs));
    return ave::MediaAsset(id, file_path, name, 4096, "mp4", duration, std::move(streams));
}

ave::EditProposalEnvelope make_valid_envelope(const std::string& track_id,
                                              const std::string& asset_id) {
    ave::EditProposalEnvelope env;
    env.schema_version = 1;
    env.proposal_id = "prop-test-1";
    env.source = "ai";

    ave::EditOperation op;
    op.operation_type = "insert_clip";
    op.target_ids = {track_id};
    op.parameters.clip_id = "cand-clip-1";
    op.parameters.asset_id = asset_id;
    op.parameters.clip_name = "sample.mp4";
    op.parameters.timeline_in = ave::Rational{0, 1};
    op.parameters.duration = ave::Rational{1, 1};
    op.parameters.source_in = ave::Rational{0, 1};
    op.parameters.speed = ave::Rational{1, 1};
    op.parameters.opacity = 1.0f;
    op.parameters.blend_mode = "Over";
    op.evidence.justified_by = "beat_grid";
    op.evidence.timestamp = ave::Rational{0, 1};
    op.evidence.confidence = 0.9;
    env.operations.push_back(std::move(op));
    return env;
}

} // namespace

TEST(Phase10AiStudioTest, MetaObjectContractAndInitialState) {
    const QMetaObject& mo = ave::desktop::AiStudioService::staticMetaObject;

    int custom_props = 0;
    for (int i = mo.propertyOffset(); i < mo.propertyCount(); ++i) {
        ++custom_props;
    }
    EXPECT_EQ(custom_props, 6);

    struct ExpectedProp {
        const char* name;
        int meta_type;
    };
    const ExpectedProp expected[] = {
        { "isBusy",                        QMetaType::Bool },
        { "hasReference",                  QMetaType::Bool },
        { "structuralReferenceSimilarity", QMetaType::QVariant },
        { "statusMessage",                 QMetaType::QString },
        { "proposedOperations",            QMetaType::QStringList },
        { "hasValidProposal",              QMetaType::Bool }
    };
    for (const auto& item : expected) {
        const int idx = mo.indexOfProperty(item.name);
        ASSERT_GE(idx, 0) << item.name;
        const QMetaProperty prop = mo.property(idx);
        EXPECT_EQ(prop.typeId(), item.meta_type) << item.name;
        EXPECT_FALSE(prop.isWritable()) << item.name;
        EXPECT_FALSE(prop.isConstant()) << item.name;
        ASSERT_TRUE(prop.hasNotifySignal()) << item.name;
        EXPECT_EQ(prop.notifySignal().parameterCount(), 0) << item.name;
    }

    // Zero fake setter methods exposed to QML/public API
    EXPECT_LT(mo.indexOfMethod("setStructuralReferenceSimilarity(QVariant)"), 0);
    EXPECT_LT(mo.indexOfMethod("setProposedOperations(QStringList)"), 0);
    EXPECT_LT(mo.indexOfMethod("setHasValidProposal(bool)"), 0);
    EXPECT_LT(mo.indexOfMethod("setHasReference(bool)"), 0);

    // Exact Q_INVOKABLE slots present
    EXPECT_GE(mo.indexOfMethod("analyzeReferenceMedia(QString)"), 0);
    EXPECT_GE(mo.indexOfMethod("clearReference()"), 0);
    EXPECT_GE(mo.indexOfMethod("generateProposal(QString)"), 0);
    EXPECT_GE(mo.indexOfMethod("applyProposal()"), 0);
    EXPECT_GE(mo.indexOfMethod("discardProposal()"), 0);
    EXPECT_GE(mo.indexOfMethod("shutdown()"), 0);

    ave::Project project("Meta Initial Check");
    ave::desktop::AiStudioService service(project);
    EXPECT_FALSE(service.isBusy());
    EXPECT_FALSE(service.hasReference());
    EXPECT_FALSE(service.structuralReferenceSimilarity().isValid());
    EXPECT_EQ(service.statusMessage(), QStringLiteral("Ready for natural language prompt."));
    EXPECT_TRUE(service.proposedOperations().isEmpty());
    EXPECT_FALSE(service.hasValidProposal());
    service.shutdown();
}

TEST(Phase10AiStudioTest, FingerprintEngineNonFiniteSafeguards) {
    const double qnan = std::numeric_limits<double>::quiet_NaN();
    const double pinf = std::numeric_limits<double>::infinity();
    const double ninf = -std::numeric_limits<double>::infinity();

    ave::ReferenceFingerprint valid_a;
    ave::ReferenceFingerprint valid_b;
    const ave::FingerprintDistanceBreakdown valid_bd =
        ave::FingerprintEngine::compute_distance_detailed(valid_a, valid_b);
    EXPECT_TRUE(valid_bd.valid);
    EXPECT_DOUBLE_EQ(valid_bd.total_distance, 0.0);

    // Every single NaN / Inf field across both operands must fail BEFORE min/max clamping
    const auto expect_invalid_bd = [](const ave::ReferenceFingerprint& x,
                                      const ave::ReferenceFingerprint& y,
                                      ave::FingerprintDistanceWeights w = {}) {
        const ave::FingerprintDistanceBreakdown bd =
            ave::FingerprintEngine::compute_distance_detailed(x, y, w);
        EXPECT_FALSE(bd.valid);
        EXPECT_TRUE(std::isnan(bd.total_distance));
        EXPECT_TRUE(std::isnan(ave::FingerprintEngine::compute_distance(x, y, w)));
    };

    {
        ave::ReferenceFingerprint bad = valid_a;
        bad.pacing.median_sec = qnan;
        expect_invalid_bd(bad, valid_b);
        expect_invalid_bd(valid_b, bad);
    }
    {
        ave::ReferenceFingerprint bad = valid_a;
        bad.pacing.burstiness = pinf;
        expect_invalid_bd(bad, valid_b);
    }
    {
        ave::ReferenceFingerprint bad = valid_a;
        bad.rhythm.mean_offset_ms = pinf;
        expect_invalid_bd(bad, valid_b);
    }
    {
        ave::ReferenceFingerprint bad = valid_a;
        bad.transitions.hard_cut_ratio = qnan;
        expect_invalid_bd(bad, valid_b);
    }
    {
        ave::ReferenceFingerprint bad = valid_a;
        bad.motion.mean_motion_energy = ninf;
        expect_invalid_bd(bad, valid_b);
    }
    {
        ave::ReferenceFingerprint bad = valid_a;
        bad.color.lab_l = qnan;
        expect_invalid_bd(bad, valid_b);
    }
    {
        ave::ReferenceFingerprint bad = valid_a;
        bad.integrated_loudness_lufs = ninf;
        bad.tempo_bpm = qnan;
        bad.speech_density = pinf;
        expect_invalid_bd(bad, valid_b);
    }

    // Weight validation
    {
        ave::FingerprintDistanceWeights w;
        w.pacing_weight = qnan;
        expect_invalid_bd(valid_a, valid_b, w);
    }
    {
        ave::FingerprintDistanceWeights w;
        w.color_weight = -0.1;
        expect_invalid_bd(valid_a, valid_b, w);
    }
    {
        ave::FingerprintDistanceWeights zero_w{0.0, 0.0, 0.0, 0.0, 0.0};
        expect_invalid_bd(valid_a, valid_b, zero_w);
    }

    // Unsorted / NaN shot duration in PerceptionReport must not slip through std::sort
    ave::PerceptionReport corrupt_report;
    corrupt_report.duration = ave::Rational{3, 1};
    corrupt_report.shots.push_back(ave::ShotBoundary{ave::Rational{0, 1}, ave::Rational{1, 1}});
    corrupt_report.shots.push_back(ave::ShotBoundary{ave::Rational{1, 1}, ave::Rational{2, 1}});
    corrupt_report.visual.average_motion_energy = qnan;
    const ave::ReferenceFingerprint corrupt_fp =
        ave::FingerprintEngine::from_perception_report(corrupt_report);
    expect_invalid_bd(corrupt_fp, valid_b);

    // Seed invalid NaN fingerprint via friend test accessor:
    // hasReference == true while structuralReferenceSimilarity remains null invalid QVariant{}
    ave::Project project("Invalid Similarity Service Check");
    ave::desktop::AiStudioService service(project);
    EXPECT_FALSE(service.structuralReferenceSimilarity().isValid());

    ave::desktop::AiStudioServiceTestAccessor::seed_reference(
        service, corrupt_fp, ave::AudioRhythmReport{}, true);
    EXPECT_TRUE(service.hasReference());
    EXPECT_FALSE(service.structuralReferenceSimilarity().isValid());

    ave::desktop::AiStudioServiceTestAccessor::seed_reference(
        service, valid_a, ave::AudioRhythmReport{}, true);
    EXPECT_TRUE(service.hasReference());
    ASSERT_TRUE(service.structuralReferenceSimilarity().isValid());
    const double valid_sim = service.structuralReferenceSimilarity().toDouble();
    EXPECT_TRUE(std::isfinite(valid_sim));
    EXPECT_GE(valid_sim, 0.0);
    EXPECT_LE(valid_sim, 1.0);

    service.shutdown();
}

TEST(Phase10AiStudioTest, AnalyzeReferenceMediaRealFixtureAndClear) {
    const std::filesystem::path fixture = fixture_file("sample_test.mp4");
    ASSERT_TRUE(std::filesystem::exists(fixture));

    const ave::ProbeResult probed = ave::FFmpegProbe::probe(fixture);
    ASSERT_TRUE(probed.success && probed.asset.has_value());

    ave::Project project("Analyze Reference Test");
    const ave::MediaAsset& stored = project.media_pool_mut().register_asset(*probed.asset);
    const std::string v1_id = project.timeline().video_tracks()[0].id();
    const ave::Rational clip_dur =
        (stored.duration() > ave::Rational{0, 1}) ? stored.duration() : ave::Rational{1, 1};
    project.execute_command(std::make_unique<ave::InsertClipCommand>(
        v1_id,
        ave::TimelineItem("ref-tl-clip", "mc-ref", stored.id(), stored.display_name(),
                          ave::Rational{0, 1}, clip_dur, ave::Rational{0, 1})));

    auto store = std::make_shared<ave::PlaybackSnapshotStore>(
        project.timeline(), project.media_pool());
    ave::ProjectSnapshotCoordinator coordinator(project, store);
    ave::desktop::AiStudioService service(project, nullptr, store, &coordinator);

    QSignalSpy busy_spy(&service, &ave::desktop::AiStudioService::isBusyChanged);
    QSignalSpy ref_spy(&service, &ave::desktop::AiStudioService::hasReferenceChanged);
    QSignalSpy sim_spy(&service, &ave::desktop::AiStudioService::structuralReferenceSimilarityChanged);

    service.analyzeReferenceMedia(QString::fromStdWString(fixture.wstring()));
    EXPECT_TRUE(service.isBusy());

    ASSERT_TRUE(pump_events_until([&]() {
        return !service.isBusy() && service.hasReference();
    }));

    EXPECT_FALSE(service.isBusy());
    EXPECT_TRUE(service.hasReference());
    ASSERT_TRUE(service.structuralReferenceSimilarity().isValid());
    EXPECT_EQ(service.structuralReferenceSimilarity().typeId(), QMetaType::Double);
    const double sim = service.structuralReferenceSimilarity().toDouble();
    EXPECT_TRUE(std::isfinite(sim));
    EXPECT_GE(sim, 0.0);
    EXPECT_LE(sim, 1.0);
    EXPECT_EQ(service.statusMessage(), QStringLiteral("Reference fingerprint analyzed."));
    EXPECT_GE(busy_spy.count(), 2);
    EXPECT_GE(ref_spy.count(), 1);
    EXPECT_GE(sim_spy.count(), 1);

    // Clear resets reference & similarity back to null QVariant{}
    service.clearReference();
    EXPECT_FALSE(service.hasReference());
    EXPECT_FALSE(service.structuralReferenceSimilarity().isValid());

    // file:/// URL variant works identically
    const QString file_url =
        QUrl::fromLocalFile(QString::fromStdWString(fixture.wstring())).toString();
    service.analyzeReferenceMedia(file_url);
    ASSERT_TRUE(pump_events_until([&]() {
        return !service.isBusy() && service.hasReference();
    }));
    EXPECT_TRUE(service.structuralReferenceSimilarity().isValid());

    // Corrupt / missing file clears reference & reports failure cleanly without hanging busy
    ScopedTempDir tmp("ave_g07_corrupt");
    const std::filesystem::path bad_file = tmp.path / "not_media.mp4";
    {
        std::ofstream ofs(bad_file, std::ios::binary | std::ios::trunc);
        ofs << "corrupt-bytes-not-an-mp4";
    }
    service.analyzeReferenceMedia(QString::fromStdWString(bad_file.wstring()));
    ASSERT_TRUE(pump_events_until([&]() { return !service.isBusy(); }));
    EXPECT_FALSE(service.hasReference());
    EXPECT_FALSE(service.structuralReferenceSimilarity().isValid());
    EXPECT_FALSE(service.statusMessage().isEmpty());

    // Empty path rejected synchronously without sticking busy
    service.analyzeReferenceMedia(QStringLiteral("   "));
    EXPECT_FALSE(service.isBusy());
    EXPECT_FALSE(service.hasReference());
    EXPECT_FALSE(service.structuralReferenceSimilarity().isValid());

    coordinator.unbind();
    service.shutdown();
}

TEST(Phase10AiStudioTest, ProposalGenerationAndValidationSafeguards) {
    const std::filesystem::path fixture = fixture_file("sample_test.mp4");
    ASSERT_TRUE(std::filesystem::exists(fixture));
    const ave::ProbeResult probed = ave::FFmpegProbe::probe(fixture);
    ASSERT_TRUE(probed.success && probed.asset.has_value());

    ave::Project project("Proposal Generation Test");
    const std::string v1_id = project.timeline().video_tracks()[0].id();
    auto store = std::make_shared<ave::PlaybackSnapshotStore>(
        project.timeline(), project.media_pool());
    ave::ProjectSnapshotCoordinator coordinator(project, store);
    ave::desktop::AiStudioService service(project, nullptr, store, &coordinator);

    // 1. Empty prompt rejected cleanly
    service.generateProposal(QStringLiteral("   \t\n "));
    EXPECT_FALSE(service.isBusy());
    EXPECT_FALSE(service.hasValidProposal());

    // 2. Empty MediaPool rejected cleanly
    service.generateProposal(QStringLiteral("fast 4s montage"));
    ASSERT_TRUE(pump_events_until([&]() { return !service.isBusy(); }));
    EXPECT_FALSE(service.hasValidProposal());
    EXPECT_TRUE(service.proposedOperations().isEmpty());

    // 3. Populate pool with real fixture + second clip asset and republish snapshot
    const ave::MediaAsset& real_asset = project.media_pool_mut().register_asset(*probed.asset);
    const ave::MediaAsset extra_asset = make_synthetic_video_asset(
        "asset-synth-b", "B_Roll_Aerial.mp4", "b_roll.mp4", ave::Rational{3, 1});
    project.media_pool_mut().register_asset(extra_asset);
    coordinator.notify_media_mutated();

    QSignalSpy prop_spy(&service, &ave::desktop::AiStudioService::proposalChanged);
    service.generateProposal(QStringLiteral("fast energetic 4s montage on beats"));
    EXPECT_TRUE(service.isBusy());

    ASSERT_TRUE(pump_events_until([&]() {
        return !service.isBusy() && service.hasValidProposal();
    }));

    EXPECT_TRUE(service.hasValidProposal());
    EXPECT_GE(prop_spy.count(), 1);
    EXPECT_EQ(service.statusMessage(),
              QStringLiteral("Proposal generated via Deterministic Planner (ADR-0018)."));

    const QStringList ops = service.proposedOperations();
    ASSERT_FALSE(ops.isEmpty());
    const QRegularExpression line_rx(
        QStringLiteral(R"(^[+\-~] V\d+: .+ \[\d{2}:\d{2}:\d{2}:\d{2} -> \d{2}:\d{2}:\d{2}:\d{2}\] \(.+\)$)"));
    for (const QString& line : ops) {
        EXPECT_TRUE(line_rx.match(line).hasMatch()) << line.toStdString();
    }

    // 4. discardProposal resets proposedOperations and hasValidProposal
    service.discardProposal();
    EXPECT_FALSE(service.hasValidProposal());
    EXPECT_TRUE(service.proposedOperations().isEmpty());

    // 5. ProposalBridge::validate hardening checks
    const ave::EditProposalEnvelope base = make_valid_envelope(v1_id, real_asset.id());
    std::vector<std::string> errors;
    EXPECT_TRUE(ave::ProposalBridge::validate(base, project.timeline(), project.media_pool(), errors));
    EXPECT_TRUE(errors.empty());

    auto expect_validate_fail = [&](ave::EditProposalEnvelope bad) {
        std::vector<std::string> errs;
        EXPECT_FALSE(ave::ProposalBridge::validate(bad, project.timeline(), project.media_pool(), errs));
        EXPECT_FALSE(errs.empty());
    };

    { auto b = base; b.schema_version = 2; expect_validate_fail(b); }
    { auto b = base; b.proposal_id.clear(); expect_validate_fail(b); }
    { auto b = base; b.source = "untrusted_llm"; expect_validate_fail(b); }
    { auto b = base; b.operations.clear(); expect_validate_fail(b); }
    { auto b = base; b.operations[0].operation_type = "delete_clip"; expect_validate_fail(b); }
    { auto b = base; b.operations[0].target_ids = {"missing-track"}; expect_validate_fail(b); }
    { auto b = base; b.operations[0].parameters.timeline_in = ave::Rational{-1, 1}; expect_validate_fail(b); }
    { auto b = base; b.operations[0].parameters.duration = ave::Rational{0, 1}; expect_validate_fail(b); }
    { auto b = base; b.operations[0].parameters.source_in = ave::Rational{-1, 2}; expect_validate_fail(b); }
    { auto b = base; b.operations[0].parameters.speed = ave::Rational{0, 1}; expect_validate_fail(b); }
    { auto b = base; b.operations[0].parameters.opacity = std::numeric_limits<float>::quiet_NaN(); expect_validate_fail(b); }
    { auto b = base; b.operations[0].parameters.opacity = 1.25f; expect_validate_fail(b); }
    { auto b = base; b.operations[0].evidence.confidence = -0.2; expect_validate_fail(b); }
    { auto b = base; b.operations[0].parameters.blend_mode = "Burn"; expect_validate_fail(b); }
    { auto b = base; b.operations[0].parameters.asset_id = "missing-asset"; expect_validate_fail(b); }
    {
        auto b = base;
        b.operations[0].parameters.duration = ave::Rational{60, 1}; // exceeds fixture duration
        expect_validate_fail(b);
    }

    coordinator.unbind();
    service.shutdown();
}

TEST(Phase10AiStudioTest, ApplyProposalCompositeUndoSnapshotAndPartialRollback) {
    const std::filesystem::path fixture = fixture_file("sample_test.mp4");
    ASSERT_TRUE(std::filesystem::exists(fixture));
    const ave::ProbeResult probed = ave::FFmpegProbe::probe(fixture);
    ASSERT_TRUE(probed.success && probed.asset.has_value());

    ave::Project project("Apply Proposal Transaction Test");
    const std::string v1_id = project.timeline().video_tracks()[0].id();
    const ave::MediaAsset& real_asset = project.media_pool_mut().register_asset(*probed.asset);
    project.media_pool_mut().register_asset(
        make_synthetic_video_asset("asset-2", "Cutaway.mp4", "cutaway.mp4", ave::Rational{3, 1}));

    auto store = std::make_shared<ave::PlaybackSnapshotStore>(
        project.timeline(), project.media_pool());
    ave::ProjectSnapshotCoordinator coordinator(project, store);
    ave::desktop::AiStudioService service(project, nullptr, store, &coordinator);

    // First analyze reference so similarity updates dynamically when proposal is applied & undone
    service.analyzeReferenceMedia(QString::fromStdWString(fixture.wstring()));
    ASSERT_TRUE(pump_events_until([&]() {
        return !service.isBusy() && service.hasReference();
    }));

    service.generateProposal(QStringLiteral("fast dynamic 4s montage"));
    ASSERT_TRUE(pump_events_until([&]() {
        return !service.isBusy() && service.hasValidProposal();
    }));

    const int planned_count = service.proposedOperations().size();
    ASSERT_GE(planned_count, 2);

    const uint64_t snap_before = store->current_version();
    const std::size_t undo_before = project.command_history().undo_count();
    QSignalSpy applied_spy(&service, &ave::desktop::AiStudioService::proposalApplied);

    ASSERT_TRUE(service.applyProposal());
    EXPECT_EQ(applied_spy.count(), 1);
    EXPECT_FALSE(service.hasValidProposal());
    EXPECT_TRUE(service.proposedOperations().isEmpty());
    EXPECT_GT(store->current_version(), snap_before);
    EXPECT_EQ(project.command_history().undo_count(), undo_before + 1u);
    EXPECT_EQ(static_cast<int>(project.timeline().video_tracks()[0].items().size()), planned_count);
    ASSERT_TRUE(service.structuralReferenceSimilarity().isValid());

    // Single-step Undo reverts ALL AI clips atomically
    ASSERT_TRUE(project.undo().is_success());
    EXPECT_TRUE(project.timeline().video_tracks()[0].items().empty());

    // Single-step Redo restores ALL AI clips atomically
    ASSERT_TRUE(project.redo().is_success());
    EXPECT_EQ(static_cast<int>(project.timeline().video_tracks()[0].items().size()), planned_count);

    // Generating another proposal on populated V1 appends cleanly without overlapping existing clips
    const ave::Rational end_before_append = project.timeline().video_tracks()[0].duration();
    service.generateProposal(QStringLiteral("fast 2s outro"));
    ASSERT_TRUE(pump_events_until([&]() {
        return !service.isBusy() && service.hasValidProposal();
    }));
    const int second_count = service.proposedOperations().size();
    ASSERT_GE(second_count, 1);
    ASSERT_TRUE(service.applyProposal());
    EXPECT_EQ(static_cast<int>(project.timeline().video_tracks()[0].items().size()),
              planned_count + second_count);
    EXPECT_GT(project.timeline().video_tracks()[0].duration(), end_before_append);

    // Undo second & first proposal so V1 returns empty cleanly
    ASSERT_TRUE(project.undo().is_success());
    ASSERT_TRUE(project.undo().is_success());
    EXPECT_TRUE(project.timeline().video_tracks()[0].items().empty());

    // Stale conflict test via friend test accessor
    const ave::EditProposalEnvelope seeded = make_valid_envelope(v1_id, real_asset.id());
    ave::desktop::AiStudioServiceTestAccessor::seed_proposal(
        service, seeded, QStringList{QStringLiteral("+ V1: sample.mp4 [00:00:00:00 -> 00:00:01:00] (beat_grid)")});
    // Mutate timeline to occupy [0s, 1s] before applying
    ASSERT_TRUE(project.execute_command(std::make_unique<ave::InsertClipCommand>(
        v1_id,
        ave::TimelineItem("manual-occupy", "mc-man", real_asset.id(), "manual.mp4",
                          ave::Rational{0, 1}, ave::Rational{1, 1}, ave::Rational{0, 1}))).is_success());
    EXPECT_FALSE(service.applyProposal());
    EXPECT_FALSE(service.hasValidProposal());
    EXPECT_EQ(project.timeline().video_tracks()[0].items().size(), 1u);
    ASSERT_TRUE(project.undo().is_success());
    EXPECT_TRUE(project.timeline().video_tracks()[0].items().empty());

    // Partial-failure CompositeCommand reverse rollback test:
    // Child 0 inserts at [0, 1s] (valid initially), Child 1 also tries to insert at [0, 1s]
    // (passes initial validate() against empty timeline, then fails during apply() after Child 0 ran).
    // CompositeCommand must revert Child 0 cleanly and preserve history & empty track!
    const std::size_t undo_before_rb = project.command_history().undo_count();
    std::vector<std::unique_ptr<ave::Command>> partial_fail_cmds;
    partial_fail_cmds.push_back(std::make_unique<ave::InsertClipCommand>(
        v1_id,
        ave::TimelineItem("rb-clip-1", "mc-rb-1", real_asset.id(), "rb1.mp4",
                          ave::Rational{0, 1}, ave::Rational{1, 1}, ave::Rational{0, 1})));
    partial_fail_cmds.push_back(std::make_unique<ave::InsertClipCommand>(
        v1_id,
        ave::TimelineItem("rb-clip-2", "mc-rb-2", real_asset.id(), "rb2.mp4",
                          ave::Rational{0, 1}, ave::Rational{1, 1}, ave::Rational{0, 1})));
    auto rollback_tx = std::make_unique<ave::CompositeCommand>(
        "Partial Fail Rollback Check", std::move(partial_fail_cmds), ave::CommandOrigin::AI);
    const ave::CommandResult rb_res = project.execute_command(std::move(rollback_tx));
    EXPECT_FALSE(rb_res.is_success());
    EXPECT_TRUE(project.timeline().video_tracks()[0].items().empty());
    EXPECT_EQ(project.command_history().undo_count(), undo_before_rb);

    coordinator.unbind();
    service.shutdown();
}

TEST(Phase10AiStudioTest, CancellationLifetimeAndSnapshotIsolation) {
    const std::filesystem::path fixture = fixture_file("sample_test.mp4");
    ASSERT_TRUE(std::filesystem::exists(fixture));

    // 1. Direct AVIOInterruptCB cooperative abort on FFmpegProbe & StreamDecoder
    {
        auto cancel = std::make_shared<std::atomic<bool>>(true);
        const ave::ProbeResult aborted_probe = ave::FFmpegProbe::probe(fixture, cancel);
        EXPECT_FALSE(aborted_probe.success);
        EXPECT_TRUE(aborted_probe.cancelled);

        ave::StreamDecoder dec;
        EXPECT_FALSE(dec.open(fixture, cancel));
        EXPECT_FALSE(dec.is_open());

        auto mid_cancel = std::make_shared<std::atomic<bool>>(false);
        ASSERT_TRUE(dec.open(fixture, mid_cancel));
        dec.request_cancel();
        EXPECT_TRUE(dec.is_cancelled());
        EXPECT_FALSE(dec.decode_next_frame(0, 160, 90).has_value());
        dec.close();
        EXPECT_FALSE(dec.is_open());
    }

    const ave::ProbeResult probed = ave::FFmpegProbe::probe(fixture);
    ASSERT_TRUE(probed.success && probed.asset.has_value());

    // 2. Rapid supersession + cancellation + mid-flight object destruction
    {
        ave::Project project("Destruction Mid Worker");
        project.media_pool_mut().register_asset(*probed.asset);
        auto store = std::make_shared<ave::PlaybackSnapshotStore>(
            project.timeline(), project.media_pool());
        ave::ProjectSnapshotCoordinator coordinator(project, store);

        auto* raw_svc = new ave::desktop::AiStudioService(project, nullptr, store, &coordinator);
        raw_svc->analyzeReferenceMedia(QString::fromStdWString(fixture.wstring()));
        raw_svc->clearReference();
        raw_svc->generateProposal(QStringLiteral("fast 4s edit"));
        raw_svc->discardProposal();
        raw_svc->analyzeReferenceMedia(QString::fromStdWString(fixture.wstring()));
        raw_svc->analyzeReferenceMedia(QString::fromStdWString(fixture.wstring()));
        delete raw_svc; // Must cleanly abort, join worker, and drop queued GUI callbacks safely

        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        coordinator.unbind();
    }

    // 3. Snapshot isolation while GUI mutates Timeline/MediaPool concurrently with AI worker
    {
        ave::Project project("Concurrent Snapshot Isolation");
        const ave::MediaAsset& a = project.media_pool_mut().register_asset(*probed.asset);
        const std::string v1_id = project.timeline().video_tracks()[0].id();
        auto store = std::make_shared<ave::PlaybackSnapshotStore>(
            project.timeline(), project.media_pool());
        ave::ProjectSnapshotCoordinator coordinator(project, store);
        ave::desktop::AiStudioService service(project, nullptr, store, &coordinator);

        service.generateProposal(QStringLiteral("fast 2s edit"));
        for (int i = 0; i < 6; ++i) {
            ave::TimelineItem item("live-mut-" + std::to_string(i),
                                   "mc-live",
                                   a.id(),
                                   a.display_name(),
                                   ave::Rational{i * 2, 1},
                                   ave::Rational{1, 1},
                                   ave::Rational{0, 1});
            ASSERT_TRUE(project.execute_command(
                std::make_unique<ave::InsertClipCommand>(v1_id, std::move(item))).is_success());
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }

        ASSERT_TRUE(pump_events_until([&]() { return !service.isBusy(); }));
        EXPECT_TRUE(service.hasValidProposal());

        coordinator.unbind();
        service.shutdown();
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

