#include "ave/ai_studio_service.hpp"

#include "ave/audio_buffer.hpp"
#include "ave/audio_perception.hpp"
#include "ave/deterministic_planner.hpp"
#include "ave/ffmpeg_probe.hpp"
#include "ave/language_sidecar.hpp"
#include "ave/media_asset.hpp"
#include "ave/media_pool.hpp"
#include "ave/project.hpp"
#include "ave/prompt_parser.hpp"
#include "ave/proposal_bridge.hpp"
#include "ave/proposal_inspector.hpp"
#include "ave/shot_detector.hpp"
#include "ave/stream_audio_decoder.hpp"
#include "ave/stream_decoder.hpp"
#include "ave/time.hpp"
#include "ave/timecode.hpp"
#include "ave/timeline.hpp"
#include "ave/video_frame.hpp"
#include "ave/visual_perception.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QUrl>

namespace ave::desktop {
namespace {

constexpr std::size_t kMaxReferenceVideoFrames = 96;
constexpr int kAnalysisMaxWidth = 320;
constexpr int kAnalysisMaxHeight = 240;
constexpr std::uint32_t kAnalysisSampleRate = 22050;
constexpr std::size_t kMaxReferenceAudioSamples = kAnalysisSampleRate * 30u; // Up to 30s mono PCM

std::filesystem::path local_path_from_qstring(const QString& input) {
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    if (trimmed.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)) {
        const QUrl url(trimmed);
        if (url.isLocalFile()) {
            return std::filesystem::path(url.toLocalFile().toStdWString());
        }
    }
    return std::filesystem::path(trimmed.toStdWString());
}

ave::Rational safe_timeline_fps(const ave::Timeline& timeline) noexcept {
    const ave::Rational fps = timeline.frame_rate();
    return (fps.numerator() > 0 && fps.denominator() > 0) ? fps : ave::Rational{24, 1};
}

QString format_timecode_safe(ave::Rational pts, ave::Rational fps) {
    try {
        return QString::fromStdString(ave::Timecode::from_rational(pts, fps).to_string());
    } catch (const std::exception&) {
        return QStringLiteral("00:00:00:00");
    }
}

QString format_proposal_operation_line(const ave::EditOperation& op,
                                       const ave::Timeline& timeline,
                                       ave::Rational fps) {
    QString prefix = QStringLiteral("~");
    if (op.operation_type == "insert_clip") {
        prefix = QStringLiteral("+");
    } else if (op.operation_type == "delete_clip") {
        prefix = QStringLiteral("-");
    }

    QString target_label = QStringLiteral("Track");
    if (!op.target_ids.empty() && !op.target_ids[0].empty()) {
        if (const ave::Track* trk = timeline.find_track(op.target_ids[0])) {
            target_label = QString::fromStdString(trk->name());
        } else {
            target_label = QString::fromStdString(op.target_ids[0]);
        }
    }

    const std::string& raw_clip_name = !op.parameters.clip_name.empty()
                                           ? op.parameters.clip_name
                                           : op.parameters.asset_id;
    const QString clip_label = raw_clip_name.empty()
                                   ? QStringLiteral("Unnamed Clip")
                                   : QString::fromStdString(raw_clip_name);

    const ave::Rational tl_in = op.parameters.timeline_in;
    ave::Rational tl_out = tl_in;
    try {
        tl_out = tl_in + op.parameters.duration;
    } catch (const std::exception&) {
        tl_out = tl_in;
    }

    const QString in_tc = format_timecode_safe(tl_in, fps);
    const QString out_tc = format_timecode_safe(tl_out, fps);
    const QString reason = op.evidence.justified_by.empty()
                               ? QStringLiteral("planner")
                               : QString::fromStdString(op.evidence.justified_by);

    return QStringLiteral("%1 %2: %3 [%4 -> %5] (%6)")
        .arg(prefix, target_label, clip_label, in_tc, out_tc, reason);
}

int16_t float_pcm_to_int16(float sample) noexcept {
    if (!std::isfinite(sample)) {
        return 0;
    }
    const float clamped = (std::max)(-1.0f, (std::min)(1.0f, sample));
    const long long scaled = std::llround(static_cast<double>(clamped) * 32767.0);
    if (scaled > 32767LL) {
        return 32767;
    }
    if (scaled < -32768LL) {
        return -32768;
    }
    return static_cast<int16_t>(scaled);
}

std::optional<double> compute_timeline_similarity(
    const ave::ReferenceFingerprint& ref_fp,
    const ave::AudioRhythmReport& ref_rhythm,
    const ave::PlaybackSnapshotPtr& snapshot) noexcept {
    if (!snapshot) {
        return std::nullopt;
    }
    const ave::ReferenceFingerprint self_fp = ref_fp;
    const ave::FingerprintDistanceBreakdown self_check =
        ave::FingerprintEngine::compute_distance_detailed(self_fp, self_fp);
    if (!self_check.valid || !std::isfinite(self_check.total_distance)) {
        return std::nullopt;
    }

    const ave::ReferenceFingerprint timeline_fp =
        ave::FingerprintEngine::from_timeline(snapshot->timeline, ref_rhythm);
    const ave::FingerprintDistanceBreakdown bd =
        ave::FingerprintEngine::compute_distance_detailed(self_fp, timeline_fp);
    if (!bd.valid || !std::isfinite(bd.total_distance)) {
        return std::nullopt;
    }

    return std::clamp(1.0 - bd.total_distance, 0.0, 1.0);
}

} // namespace

AiStudioService::AiStudioService(ave::Project& project,
                                 QObject* parent,
                                 std::shared_ptr<ave::PlaybackSnapshotStore> snapshot_store,
                                 ave::ProjectSnapshotCoordinator* snapshot_coordinator)
    : QObject(parent),
      project_(project),
      snapshot_store_(std::move(snapshot_store)),
      snapshot_coordinator_(snapshot_coordinator),
      alive_flag_(std::make_shared<std::atomic<bool>>(true)) {
    bind_listeners();
    worker_thread_ = std::thread(&AiStudioService::worker_loop, this);
}

AiStudioService::~AiStudioService() {
    in_destructor_ = true;
    shutdown();
}

bool AiStudioService::isBusy() const noexcept {
    return is_busy_;
}

bool AiStudioService::hasReference() const noexcept {
    return has_reference_;
}

QVariant AiStudioService::structuralReferenceSimilarity() const {
    if (!has_reference_ || !similarity_.has_value() || !std::isfinite(*similarity_)) {
        return {};
    }
    return QVariant::fromValue(*similarity_);
}

QString AiStudioService::statusMessage() const {
    return status_message_;
}

QStringList AiStudioService::proposedOperations() const {
    return proposed_operations_;
}

bool AiStudioService::hasValidProposal() const noexcept {
    return has_valid_proposal_;
}

void AiStudioService::analyzeReferenceMedia(const QString& filePath) {
    if (shutting_down_.load(std::memory_order_acquire) || !alive_flag_->load(std::memory_order_acquire)) {
        return;
    }

    const std::filesystem::path ref_path = local_path_from_qstring(filePath);
    if (ref_path.empty()) {
        cancel_active_worker();
        ++reference_generation_;
        clear_reference_fields(!in_destructor_);
        set_busy(false, !in_destructor_);
        set_status_message(QStringLiteral("Reference analysis rejected: empty file path."), !in_destructor_);
        return;
    }

    cancel_active_worker(std::nullopt, false);
    ++reference_generation_;

    const uint64_t epoch = ++next_epoch_;
    active_busy_epoch_ = epoch;
    active_job_kind_ = JobKind::AnalyzeReference;
    set_busy(true, true);
    set_status_message(QStringLiteral("Analyzing reference media..."), true);

    WorkerJob job;
    job.kind = JobKind::AnalyzeReference;
    job.busy_epoch = epoch;
    job.domain_generation = reference_generation_;
    job.cancel_state = std::make_shared<std::atomic<bool>>(false);
    job.snapshot = obtain_snapshot();
    job.reference_path = ref_path;
    enqueue_job(std::move(job));
}

void AiStudioService::clearReference() {
    ++reference_generation_;
    cancel_active_worker(JobKind::AnalyzeReference);
    clear_reference_fields(!in_destructor_);
    if (!is_busy_) {
        set_status_message(QStringLiteral("Reference cleared."), !in_destructor_);
    }
}

void AiStudioService::generateProposal(const QString& prompt) {
    if (shutting_down_.load(std::memory_order_acquire) || !alive_flag_->load(std::memory_order_acquire)) {
        return;
    }

    const QString trimmed = prompt.trimmed();
    if (trimmed.isEmpty()) {
        ++proposal_generation_;
        cancel_active_worker(JobKind::GenerateProposal);
        clear_proposal_fields(!in_destructor_);
        set_status_message(QStringLiteral("Enter a non-empty natural language prompt."), !in_destructor_);
        return;
    }

    cancel_active_worker(std::nullopt, false);
    ++proposal_generation_;
    clear_proposal_fields(true);

    const uint64_t epoch = ++next_epoch_;
    active_busy_epoch_ = epoch;
    active_job_kind_ = JobKind::GenerateProposal;
    set_busy(true, true);
    set_status_message(QStringLiteral("Generating edit proposal..."), true);

    WorkerJob job;
    job.kind = JobKind::GenerateProposal;
    job.busy_epoch = epoch;
    job.domain_generation = proposal_generation_;
    job.cancel_state = std::make_shared<std::atomic<bool>>(false);
    job.snapshot = obtain_snapshot();
    job.prompt = trimmed.toStdString();
    if (has_reference_ && reference_fingerprint_.has_value()) {
        job.reference_fingerprint = reference_fingerprint_;
        job.reference_rhythm = reference_rhythm_;
    }
    enqueue_job(std::move(job));
}

bool AiStudioService::applyProposal() {
    if (shutting_down_.load(std::memory_order_acquire) || !alive_flag_->load(std::memory_order_acquire)) {
        return false;
    }
    if (!has_valid_proposal_ || !active_proposal_.has_value()) {
        set_status_message(QStringLiteral("No valid AI proposal available to apply."), true);
        return false;
    }

    const uint64_t snap_ver_before = snapshot_store_ ? snapshot_store_->current_version() : 0;
    const ave::CommandResult res = ave::ProposalBridge::commit(
        *active_proposal_,
        project_.timeline_mut(),
        project_.media_pool_mut(),
        project_.command_history_mut());

    if (!res.is_success()) {
        ++proposal_generation_;
        clear_proposal_fields(true);
        const QString reason = res.message.empty()
                                   ? QStringLiteral("unknown conflict")
                                   : QString::fromStdString(res.message);
        set_status_message(QStringLiteral("Proposal commit failed: %1").arg(reason), true);
        return false;
    }

    ++proposal_generation_;
    clear_proposal_fields(true);

    if (snapshot_store_ && snapshot_store_->current_version() == snap_ver_before) {
        if (snapshot_coordinator_) {
            snapshot_coordinator_->publish_now();
        } else {
            snapshot_store_->publish(project_.timeline(), project_.media_pool());
        }
    } else if (!snapshot_store_) {
        recompute_similarity_from_snapshot(obtain_snapshot(), true);
    }

    set_status_message(QStringLiteral("Applied AI proposal (1-step undo available)."), true);
    emit proposalApplied();
    return true;
}

void AiStudioService::discardProposal() {
    ++proposal_generation_;
    cancel_active_worker(JobKind::GenerateProposal);
    clear_proposal_fields(!in_destructor_);
    if (!is_busy_) {
        set_status_message(QStringLiteral("Proposal discarded."), !in_destructor_);
    }
}

void AiStudioService::shutdown() {
    alive_flag_->store(false, std::memory_order_release);
    shutting_down_.store(true, std::memory_order_release);

    unbind_listeners();

    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (active_cancel_state_) {
            active_cancel_state_->store(true, std::memory_order_release);
        }
        if (pending_job_ && pending_job_->cancel_state) {
            pending_job_->cancel_state->store(true, std::memory_order_release);
        }
        pending_job_.reset();
        worker_stop_ = true;
    }
    worker_cv_.notify_all();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    active_busy_epoch_ = 0;
    active_job_kind_.reset();
    if (in_destructor_) {
        is_busy_ = false;
    } else {
        set_busy(false, true);
    }
}

void AiStudioService::handle_project_reloaded() {
    if (shutting_down_.load(std::memory_order_acquire) || in_destructor_) {
        return;
    }

    ++proposal_generation_;
    cancel_active_worker(JobKind::GenerateProposal);
    clear_proposal_fields(true);

    if (!snapshot_store_) {
        bind_listeners();
    }
    recompute_similarity_from_snapshot(obtain_snapshot(), true);
}

void AiStudioService::worker_loop() {
    for (;;) {
        std::optional<WorkerJob> job;
        {
            std::unique_lock<std::mutex> lock(worker_mutex_);
            worker_cv_.wait(lock, [this]() {
                return worker_stop_ || pending_job_.has_value();
            });
            if (worker_stop_) {
                active_cancel_state_.reset();
                return;
            }
            job = std::move(pending_job_);
            pending_job_.reset();
            active_cancel_state_ = job->cancel_state;
        }

        if (job->kind == JobKind::AnalyzeReference) {
            post_analyze_outcome(execute_analyze_job(*job));
        } else {
            post_proposal_outcome(execute_proposal_job(*job));
        }

        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            active_cancel_state_.reset();
        }
    }
}

void AiStudioService::enqueue_job(WorkerJob job) {
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (worker_stop_ || shutting_down_.load(std::memory_order_acquire)) {
            return;
        }
        if (pending_job_ && pending_job_->cancel_state) {
            pending_job_->cancel_state->store(true, std::memory_order_release);
        }
        if (active_cancel_state_) {
            active_cancel_state_->store(true, std::memory_order_release);
        }
        pending_job_ = std::move(job);
    }
    worker_cv_.notify_one();
}

void AiStudioService::cancel_active_worker(std::optional<JobKind> only_if_kind,
                                           bool clear_busy_state) {
    bool cleared_gui_active = false;
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (only_if_kind.has_value()) {
            if (pending_job_ && pending_job_->kind == *only_if_kind) {
                if (pending_job_->cancel_state) {
                    pending_job_->cancel_state->store(true, std::memory_order_release);
                }
                pending_job_.reset();
            }
            if (active_job_kind_ == *only_if_kind) {
                if (active_cancel_state_) {
                    active_cancel_state_->store(true, std::memory_order_release);
                }
                cleared_gui_active = true;
            }
        } else {
            if (pending_job_) {
                if (pending_job_->cancel_state) {
                    pending_job_->cancel_state->store(true, std::memory_order_release);
                }
                pending_job_.reset();
            }
            if (active_cancel_state_) {
                active_cancel_state_->store(true, std::memory_order_release);
            }
            cleared_gui_active = active_busy_epoch_ != 0;
        }
    }

    if (cleared_gui_active && clear_busy_state) {
        active_busy_epoch_ = 0;
        active_job_kind_.reset();
        if (in_destructor_) {
            is_busy_ = false;
        } else {
            set_busy(false, true);
        }
    }
}

bool AiStudioService::is_job_aborted(const WorkerJob& job) const noexcept {
    return shutting_down_.load(std::memory_order_acquire) ||
           !alive_flag_->load(std::memory_order_acquire) ||
           (job.cancel_state && job.cancel_state->load(std::memory_order_acquire));
}

ave::PlaybackSnapshotPtr AiStudioService::obtain_snapshot() const {
    if (snapshot_store_) {
        if (ave::PlaybackSnapshotPtr snap = snapshot_store_->load()) {
            return snap;
        }
    }
    auto fallback = std::make_shared<ave::PlaybackSnapshot>();
    fallback->version = snapshot_store_ ? snapshot_store_->current_version() : 1;
    fallback->timeline = project_.timeline();
    fallback->assets_by_id = project_.media_pool().assets();
    return fallback;
}

AiStudioService::AnalyzeOutcome AiStudioService::execute_analyze_job(const WorkerJob& job) const {
    AnalyzeOutcome out;
    out.busy_epoch = job.busy_epoch;
    out.reference_generation = job.domain_generation;
    out.snapshot = job.snapshot;

    if (is_job_aborted(job)) {
        out.status = JobOutcomeStatus::Cancelled;
        return out;
    }

    const ave::ProbeResult probe = ave::FFmpegProbe::probe(job.reference_path, job.cancel_state);
    if (probe.cancelled || is_job_aborted(job)) {
        out.status = JobOutcomeStatus::Cancelled;
        return out;
    }
    if (!probe.success || !probe.asset.has_value()) {
        out.status = JobOutcomeStatus::Failed;
        out.error_message = probe.error_message.empty()
                                ? "Unable to probe reference media file."
                                : probe.error_message;
        return out;
    }

    const ave::MediaAsset& asset = *probe.asset;
    ave::Rational video_fps{24, 1};
    int sample_w = 0;
    int sample_h = 0;
    std::vector<ave::VideoFrame> decoded_frames;

    if (asset.primary_video_stream() != nullptr) {
        ave::StreamDecoder vdec;
        if (vdec.open(job.reference_path, job.cancel_state)) {
            if (vdec.frame_rate().numerator() > 0 && vdec.frame_rate().denominator() > 0) {
                video_fps = vdec.frame_rate();
            }

            const int src_w = (std::max)(2, vdec.width());
            const int src_h = (std::max)(2, vdec.height());
            const double scale = (std::min)({
                1.0,
                static_cast<double>(kAnalysisMaxWidth) / static_cast<double>(src_w),
                static_cast<double>(kAnalysisMaxHeight) / static_cast<double>(src_h)
            });
            sample_w = (std::max)(2, (static_cast<int>(src_w * scale) / 2) * 2);
            sample_h = (std::max)(2, (static_cast<int>(src_h * scale) / 2) * 2);

            const ave::Rational v_dur =
                (vdec.duration().numerator() > 0 && vdec.duration().denominator() > 0)
                    ? vdec.duration()
                    : asset.duration();
            const double dur_sec = v_dur.to_seconds();

            if (dur_sec <= 4.0) {
                decoded_frames.reserve(kMaxReferenceVideoFrames);
                for (std::size_t i = 0; i < kMaxReferenceVideoFrames; ++i) {
                    if (is_job_aborted(job) || vdec.is_cancelled()) {
                        vdec.close();
                        out.status = JobOutcomeStatus::Cancelled;
                        return out;
                    }
                    auto frame = vdec.decode_next_frame(0, sample_w, sample_h);
                    if (!frame.has_value()) {
                        break;
                    }
                    decoded_frames.push_back(std::move(*frame));
                }
            } else {
                const std::size_t sample_count = (std::min)(kMaxReferenceVideoFrames, std::size_t{64});
                decoded_frames.reserve(sample_count);
                for (std::size_t i = 0; i < sample_count; ++i) {
                    if (is_job_aborted(job) || vdec.is_cancelled()) {
                        vdec.close();
                        out.status = JobOutcomeStatus::Cancelled;
                        return out;
                    }
                    const double target_sec =
                        (dur_sec * static_cast<double>(i)) / static_cast<double>(sample_count);
                    const ave::Rational target_pts(
                        static_cast<int64_t>(std::llround(target_sec * 1000.0)), 1000);
                    if (i > 0) {
                        (void)vdec.seek_pts(target_pts);
                    }
                    if (is_job_aborted(job) || vdec.is_cancelled()) {
                        vdec.close();
                        out.status = JobOutcomeStatus::Cancelled;
                        return out;
                    }
                    auto frame = vdec.decode_next_frame(0, sample_w, sample_h);
                    if (!frame.has_value()) {
                        if (i == 0) {
                            continue;
                        }
                        break;
                    }
                    decoded_frames.push_back(std::move(*frame));
                }
            }

            const bool cancelled_mid_decode = vdec.is_cancelled();
            vdec.close();
            if (cancelled_mid_decode || is_job_aborted(job)) {
                out.status = JobOutcomeStatus::Cancelled;
                return out;
            }
        } else if (is_job_aborted(job)) {
            out.status = JobOutcomeStatus::Cancelled;
            return out;
        }
    }

    std::vector<int16_t> pcm_samples;
    if (asset.primary_audio_stream() != nullptr) {
        ave::StreamAudioDecoder adec;
        if (adec.open(job.reference_path, kAnalysisSampleRate, 1, job.cancel_state)) {
            while (pcm_samples.size() < kMaxReferenceAudioSamples) {
                if (is_job_aborted(job) || adec.is_cancelled()) {
                    adec.close();
                    out.status = JobOutcomeStatus::Cancelled;
                    return out;
                }
                auto buf = adec.decode_next_audio();
                if (!buf.has_value() || buf->sample_count() == 0) {
                    break;
                }
                const std::size_t remaining = kMaxReferenceAudioSamples - pcm_samples.size();
                const std::size_t take = (std::min)(remaining, buf->sample_count());
                const float* data = buf->data();
                pcm_samples.reserve(pcm_samples.size() + take);
                for (std::size_t s = 0; s < take; ++s) {
                    pcm_samples.push_back(float_pcm_to_int16(data[s]));
                }
            }

            const bool cancelled_mid_audio = adec.is_cancelled();
            adec.close();
            if (cancelled_mid_audio || is_job_aborted(job)) {
                out.status = JobOutcomeStatus::Cancelled;
                return out;
            }
        } else if (is_job_aborted(job)) {
            out.status = JobOutcomeStatus::Cancelled;
            return out;
        }
    }

    if (decoded_frames.empty() && pcm_samples.empty()) {
        out.status = JobOutcomeStatus::Failed;
        out.error_message = "Reference media produced no decodable video or audio frames.";
        return out;
    }

    ave::PerceptionReport report;
    report.asset_id = asset.id();
    report.duration = asset.duration();

    if (!decoded_frames.empty()) {
        ave::ShotDetector shot_detector(nullptr, ave::ShotDetectorMode::ClassicalOnly);
        (void)shot_detector.process(decoded_frames, video_fps, &report);
        if (is_job_aborted(job)) {
            out.status = JobOutcomeStatus::Cancelled;
            return out;
        }

        std::vector<std::vector<uint8_t>> frame_rgbas;
        std::vector<ave::Rational> frame_pts;
        frame_rgbas.reserve(decoded_frames.size());
        frame_pts.reserve(decoded_frames.size());
        for (const auto& vf : decoded_frames) {
            if (vf.width == sample_w && vf.height == sample_h &&
                vf.rgba_data.size() == static_cast<std::size_t>(sample_w) * static_cast<std::size_t>(sample_h) * 4u) {
                frame_rgbas.push_back(vf.rgba_data);
                frame_pts.push_back(vf.pts);
            }
        }
        if (!frame_rgbas.empty()) {
            ave::VisualPerceptionPipeline visual_pipeline(nullptr);
            (void)visual_pipeline.analyze(frame_rgbas, sample_w, sample_h, frame_pts, report, nullptr);
        }
        if (is_job_aborted(job)) {
            out.status = JobOutcomeStatus::Cancelled;
            return out;
        }
    }

    if (!pcm_samples.empty()) {
        ave::AudioPerceptionPipeline audio_pipeline(nullptr, nullptr);
        (void)audio_pipeline.analyze(pcm_samples, report);
        if (is_job_aborted(job)) {
            out.status = JobOutcomeStatus::Cancelled;
            return out;
        }
    }

    const ave::ReferenceFingerprint fp = ave::FingerprintEngine::from_perception_report(report);
    const ave::FingerprintDistanceBreakdown self_check =
        ave::FingerprintEngine::compute_distance_detailed(fp, fp);
    if (!self_check.valid || !std::isfinite(self_check.total_distance)) {
        out.status = JobOutcomeStatus::Failed;
        out.error_message = "Reference analysis produced non-finite fingerprint metrics.";
        return out;
    }

    out.status = JobOutcomeStatus::Succeeded;
    out.fingerprint = fp;
    out.rhythm = std::move(report.rhythm);
    return out;
}

AiStudioService::ProposalOutcome AiStudioService::execute_proposal_job(const WorkerJob& job) const {
    ProposalOutcome out;
    out.busy_epoch = job.busy_epoch;
    out.proposal_generation = job.domain_generation;

    if (is_job_aborted(job)) {
        out.status = JobOutcomeStatus::Cancelled;
        return out;
    }

    if (!job.snapshot || job.snapshot->timeline.video_tracks().empty()) {
        out.status = JobOutcomeStatus::Failed;
        out.error_message = "Cannot generate proposal: timeline has no video track.";
        return out;
    }

    const ave::Timeline& tl = job.snapshot->timeline;
    const ave::Track& target_track = tl.video_tracks().front();

    std::vector<const ave::MediaAsset*> sorted_assets;
    sorted_assets.reserve(job.snapshot->assets_by_id.size());
    ave::MediaPool pool_snapshot;
    for (const auto& [id, asset] : job.snapshot->assets_by_id) {
        (void)id;
        pool_snapshot.register_asset(asset);
        sorted_assets.push_back(&asset);
    }

    std::sort(sorted_assets.begin(), sorted_assets.end(),
        [](const ave::MediaAsset* a, const ave::MediaAsset* b) {
            if (a->display_name() != b->display_name()) {
                return a->display_name() < b->display_name();
            }
            return a->id() < b->id();
        });

    std::vector<ave::SourceClipCandidate> video_candidates;
    std::vector<ave::SourceClipCandidate> fallback_candidates;
    for (const ave::MediaAsset* asset : sorted_assets) {
        if (!asset || asset->duration().denominator() <= 0 || asset->duration().numerator() <= 0) {
            continue;
        }
        ave::SourceClipCandidate cand;
        cand.clip_id = "cand-" + asset->id();
        cand.asset_id = asset->id();
        cand.name = asset->display_name().empty() ? asset->id() : asset->display_name();
        cand.duration = asset->duration();
        cand.source_in = ave::Rational{0, 1};
        cand.motion_energy = 0.35;
        if (asset->primary_video_stream() != nullptr) {
            video_candidates.push_back(std::move(cand));
        } else {
            fallback_candidates.push_back(std::move(cand));
        }
    }

    std::vector<ave::SourceClipCandidate> candidates =
        !video_candidates.empty() ? std::move(video_candidates) : std::move(fallback_candidates);
    if (candidates.empty()) {
        out.status = JobOutcomeStatus::Failed;
        out.error_message = "Import media into the Media Pool before generating an AI proposal.";
        return out;
    }

    ave::Rational max_cand_dur{0, 1};
    ave::Rational total_cand_dur{0, 1};
    for (const auto& cand : candidates) {
        if (cand.duration > max_cand_dur) {
            max_cand_dur = cand.duration;
        }
        try {
            total_cand_dur = total_cand_dur + cand.duration;
        } catch (const std::overflow_error&) {
            total_cand_dur = ave::Rational{15, 1};
        }
    }

    auto sidecar = std::make_shared<ave::LanguageSidecar>();
    ave::PromptParser parser(sidecar);
    const ave::ParsedPromptConstraints constraints = parser.parse(job.prompt);

    if (is_job_aborted(job)) {
        out.status = JobOutcomeStatus::Cancelled;
        return out;
    }

    const ave::ReferenceFingerprint base_ref =
        job.reference_fingerprint.value_or(ave::ReferenceFingerprint{});
    const ave::ReferenceFingerprint target_ref =
        constraints.to_reference_fingerprint(base_ref);
    ave::PlannerConfig cfg = constraints.to_planner_config();

    const double max_cand_sec = max_cand_dur.to_seconds();
    if (max_cand_sec > 0.0 && max_cand_sec < cfg.max_shot_duration_sec * 1.4) {
        cfg.max_shot_duration_sec = (std::max)(0.1, max_cand_sec * 0.55);
    }
    cfg.min_shot_duration_sec = std::clamp(cfg.min_shot_duration_sec, 0.05, cfg.max_shot_duration_sec * 0.8);

    ave::Rational target_duration{0, 1};
    if (constraints.target_duration.has_value() &&
        constraints.target_duration->denominator() > 0 &&
        constraints.target_duration->numerator() > 0) {
        target_duration = *constraints.target_duration;
        if (target_duration < ave::Rational{1, 10}) {
            target_duration = ave::Rational{1, 10};
        } else if (target_duration > ave::Rational{300, 1}) {
            target_duration = ave::Rational{300, 1};
        }
    } else {
        target_duration = (total_cand_dur > max_cand_dur) ? total_cand_dur : max_cand_dur;
        if (target_duration > ave::Rational{15, 1}) {
            target_duration = ave::Rational{15, 1};
        }
    }

    const ave::AudioRhythmReport rhythm =
        job.reference_rhythm.value_or(ave::AudioRhythmReport{});

    ave::DeterministicPlanner planner(cfg);
    ave::EditProposalEnvelope envelope = planner.plan(
        tl.id(),
        target_track.id(),
        candidates,
        target_ref,
        rhythm,
        target_duration);

    if (is_job_aborted(job)) {
        out.status = JobOutcomeStatus::Cancelled;
        return out;
    }

    // Guarantee every proposed operation stays strictly within its source asset duration,
    // splitting oversized fallback slots across valid source sub-spans if necessary.
    std::vector<ave::EditOperation> normalized_ops;
    normalized_ops.reserve(envelope.operations.size());
    for (const auto& op : envelope.operations) {
        const ave::MediaAsset* asset = pool_snapshot.find_asset(op.parameters.asset_id);
        if (!asset || asset->duration().numerator() <= 0 || asset->duration().denominator() <= 0) {
            continue;
        }
        const ave::Rational avail = asset->duration() - op.parameters.source_in;
        if (avail.numerator() <= 0) {
            continue;
        }
        if (op.parameters.duration <= avail) {
            normalized_ops.push_back(op);
        } else {
            ave::Rational cursor_tl = op.parameters.timeline_in;
            ave::Rational remaining = op.parameters.duration;
            while (remaining.numerator() > 0) {
                const ave::Rational chunk_dur = (remaining <= avail) ? remaining : avail;
                if (chunk_dur.numerator() <= 0) {
                    break;
                }
                ave::EditOperation slice = op;
                slice.parameters.timeline_in = cursor_tl;
                slice.parameters.duration = chunk_dur;
                slice.evidence.timestamp = cursor_tl;
                normalized_ops.push_back(std::move(slice));
                cursor_tl = cursor_tl + chunk_dur;
                remaining = remaining - chunk_dur;
            }
        }
    }
    envelope.operations = std::move(normalized_ops);

    // Append cleanly after any existing clips on the target track.
    const ave::Rational append_offset = target_track.duration();
    if (append_offset > ave::Rational{0, 1}) {
        for (auto& op : envelope.operations) {
            op.parameters.timeline_in = op.parameters.timeline_in + append_offset;
            op.evidence.timestamp = op.evidence.timestamp + append_offset;
        }
    }

    const ave::ReferenceFingerprint* ref_ptr =
        job.reference_fingerprint.has_value() ? &(*job.reference_fingerprint) : nullptr;
    const ave::DryRunResult dry_run = ave::ProposalBridge::dry_run(
        envelope,
        tl,
        pool_snapshot,
        rhythm,
        ref_ptr);
    const ave::ProposalInspectionViewModel inspection =
        ave::ProposalInspector::inspect(envelope, dry_run);

    if (!dry_run.is_valid || !inspection.is_valid || envelope.operations.empty()) {
        out.status = JobOutcomeStatus::Failed;
        out.error_message = dry_run.error_message.empty()
                                ? "Generated proposal failed validation."
                                : dry_run.error_message;
        return out;
    }

    const ave::Rational safe_fps = safe_timeline_fps(tl);
    QStringList formatted;
    formatted.reserve(static_cast<qsizetype>(envelope.operations.size()));
    for (const auto& op : envelope.operations) {
        formatted.push_back(format_proposal_operation_line(op, tl, safe_fps));
    }

    if (is_job_aborted(job)) {
        out.status = JobOutcomeStatus::Cancelled;
        return out;
    }

    out.status = JobOutcomeStatus::Succeeded;
    out.proposal = std::move(envelope);
    out.formatted_operations = std::move(formatted);
    return out;
}

void AiStudioService::post_analyze_outcome(AnalyzeOutcome outcome) {
    if (shutting_down_.load(std::memory_order_acquire) || !alive_flag_->load(std::memory_order_acquire)) {
        return;
    }
    QPointer<AiStudioService> self(this);
    std::shared_ptr<std::atomic<bool>> alive = alive_flag_;
    QMetaObject::invokeMethod(
        this,
        [self, alive, result = std::move(outcome)]() mutable {
            if (!alive || !alive->load(std::memory_order_acquire) || self.isNull()) {
                return;
            }
            self->handle_analyze_completed(std::move(result));
        },
        Qt::QueuedConnection);
}

void AiStudioService::post_proposal_outcome(ProposalOutcome outcome) {
    if (shutting_down_.load(std::memory_order_acquire) || !alive_flag_->load(std::memory_order_acquire)) {
        return;
    }
    QPointer<AiStudioService> self(this);
    std::shared_ptr<std::atomic<bool>> alive = alive_flag_;
    QMetaObject::invokeMethod(
        this,
        [self, alive, result = std::move(outcome)]() mutable {
            if (!alive || !alive->load(std::memory_order_acquire) || self.isNull()) {
                return;
            }
            self->handle_proposal_completed(std::move(result));
        },
        Qt::QueuedConnection);
}

void AiStudioService::handle_analyze_completed(AnalyzeOutcome outcome) {
    if (shutting_down_.load(std::memory_order_acquire) || !alive_flag_->load(std::memory_order_acquire)) {
        return;
    }

    if (outcome.busy_epoch != 0 && outcome.busy_epoch == active_busy_epoch_) {
        active_busy_epoch_ = 0;
        active_job_kind_.reset();
        set_busy(false, true);
    }

    if (outcome.reference_generation != reference_generation_ ||
        outcome.status == JobOutcomeStatus::Cancelled) {
        return;
    }

    if (outcome.status == JobOutcomeStatus::Failed) {
        clear_reference_fields(true);
        const QString err = outcome.error_message.empty()
                                ? QStringLiteral("Reference media analysis failed.")
                                : QString::fromStdString(outcome.error_message);
        set_status_message(err, true);
        return;
    }

    const ave::PlaybackSnapshotPtr current_snap = obtain_snapshot();
    const std::optional<double> sim =
        compute_timeline_similarity(outcome.fingerprint, outcome.rhythm, current_snap);

    const bool ref_changed = !has_reference_;
    const bool sim_changed = (similarity_ != sim);

    has_reference_ = true;
    reference_fingerprint_ = std::move(outcome.fingerprint);
    reference_rhythm_ = std::move(outcome.rhythm);
    similarity_ = sim;

    if (ref_changed) {
        emit hasReferenceChanged();
    }
    if (sim_changed) {
        emit structuralReferenceSimilarityChanged();
    }
    set_status_message(QStringLiteral("Reference fingerprint analyzed."), true);
}

void AiStudioService::handle_proposal_completed(ProposalOutcome outcome) {
    if (shutting_down_.load(std::memory_order_acquire) || !alive_flag_->load(std::memory_order_acquire)) {
        return;
    }

    if (outcome.busy_epoch != 0 && outcome.busy_epoch == active_busy_epoch_) {
        active_busy_epoch_ = 0;
        active_job_kind_.reset();
        set_busy(false, true);
    }

    if (outcome.proposal_generation != proposal_generation_ ||
        outcome.status == JobOutcomeStatus::Cancelled) {
        return;
    }

    if (outcome.status == JobOutcomeStatus::Failed) {
        clear_proposal_fields(true);
        const QString err = outcome.error_message.empty()
                                ? QStringLiteral("Proposal generation failed.")
                                : QString::fromStdString(outcome.error_message);
        set_status_message(err, true);
        return;
    }

    const bool prop_changed =
        !has_valid_proposal_ || (proposed_operations_ != outcome.formatted_operations);

    active_proposal_ = std::move(outcome.proposal);
    proposed_operations_ = std::move(outcome.formatted_operations);
    has_valid_proposal_ = true;

    if (prop_changed) {
        emit proposalChanged();
    }
    set_status_message(QStringLiteral("Proposal generated via Deterministic Planner (ADR-0018)."), true);
}

void AiStudioService::bind_listeners() {
    unbind_listeners();

    QPointer<AiStudioService> self(this);
    std::shared_ptr<std::atomic<bool>> alive = alive_flag_;

    if (snapshot_store_) {
        snapshot_listener_id_ = snapshot_store_->add_listener(
            [self, alive](const ave::PlaybackSnapshotPtr& snap) {
                if (!alive || !alive->load(std::memory_order_acquire)) {
                    return;
                }
                AiStudioService* target = self.data();
                if (!target) {
                    return;
                }
                if (QThread::currentThread() == target->thread()) {
                    target->recompute_similarity_from_snapshot(snap, true);
                    return;
                }
                QMetaObject::invokeMethod(
                    target,
                    [self, alive, snap]() {
                        if (!alive || !alive->load(std::memory_order_acquire) || self.isNull()) {
                            return;
                        }
                        self->recompute_similarity_from_snapshot(snap, true);
                    },
                    Qt::QueuedConnection);
            });
        return;
    }

    last_history_revision_ = project_.command_history().mutation_revision();
    history_listener_id_ = project_.command_history_mut().add_change_listener_with_id(
        [self, alive]() {
            if (!alive || !alive->load(std::memory_order_acquire) || self.isNull()) {
                return;
            }
            if (self->shutting_down_.load(std::memory_order_acquire) || self->in_destructor_) {
                return;
            }
            const uint64_t rev = self->project_.command_history().mutation_revision();
            if (rev == self->last_history_revision_) {
                return;
            }
            self->last_history_revision_ = rev;
            self->recompute_similarity_from_snapshot(self->obtain_snapshot(), true);
        });
}

void AiStudioService::unbind_listeners() {
    if (snapshot_store_ && snapshot_listener_id_ != 0) {
        snapshot_store_->remove_listener(snapshot_listener_id_);
        snapshot_listener_id_ = 0;
    }
    if (history_listener_id_ != 0) {
        project_.command_history_mut().remove_change_listener(history_listener_id_);
        history_listener_id_ = 0;
    }
}

void AiStudioService::recompute_similarity_from_snapshot(const ave::PlaybackSnapshotPtr& snapshot,
                                                        bool emit_signal) {
    if (shutting_down_.load(std::memory_order_acquire) || in_destructor_) {
        return;
    }

    std::optional<double> next_similarity;
    if (has_reference_ && reference_fingerprint_.has_value()) {
        const ave::AudioRhythmReport rhythm =
            reference_rhythm_.value_or(ave::AudioRhythmReport{});
        next_similarity = compute_timeline_similarity(*reference_fingerprint_, rhythm, snapshot);
    }

    const bool changed = (next_similarity.has_value() != similarity_.has_value()) ||
                         (next_similarity.has_value() && similarity_.has_value() &&
                          std::abs(*next_similarity - *similarity_) >= 1e-9);
    similarity_ = next_similarity;
    if (changed && emit_signal && !in_destructor_) {
        emit structuralReferenceSimilarityChanged();
    }
}

void AiStudioService::clear_proposal_fields(bool emit_signal) {
    const bool changed = has_valid_proposal_ || !proposed_operations_.isEmpty();
    active_proposal_.reset();
    proposed_operations_.clear();
    has_valid_proposal_ = false;
    if (changed && emit_signal && !in_destructor_) {
        emit proposalChanged();
    }
}

void AiStudioService::clear_reference_fields(bool emit_signal) {
    const bool ref_changed = has_reference_;
    const bool sim_changed = similarity_.has_value();

    has_reference_ = false;
    reference_fingerprint_.reset();
    reference_rhythm_.reset();
    similarity_.reset();

    if (!emit_signal || in_destructor_) {
        return;
    }
    if (ref_changed) {
        emit hasReferenceChanged();
    }
    if (sim_changed) {
        emit structuralReferenceSimilarityChanged();
    }
}

void AiStudioService::set_busy(bool busy, bool emit_signal) {
    if (is_busy_ == busy) {
        return;
    }
    is_busy_ = busy;
    if (emit_signal && !in_destructor_) {
        emit isBusyChanged();
    }
}

void AiStudioService::set_status_message(const QString& message, bool emit_signal) {
    if (status_message_ == message) {
        return;
    }
    status_message_ = message;
    if (emit_signal && !in_destructor_) {
        emit statusMessageChanged();
    }
}

} // namespace ave::desktop
