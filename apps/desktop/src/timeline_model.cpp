#include "ave/timeline_model.hpp"

#include "ave/asset_relinker.hpp"
#include "ave/media_asset.hpp"
#include "ave/media_cache.hpp"
#include "ave/media_pool.hpp"
#include "ave/otio_interchange.hpp"
#include "ave/playback_snapshot.hpp"
#include "ave/project.hpp"
#include "ave/time.hpp"
#include "ave/timeline.hpp"
#include "ave/uuid.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>
#include <QUrl>

namespace ave::desktop {
namespace {

constexpr std::size_t kMaxClipWaveformBars = 128;

ave::Rational safe_timeline_fps(const ave::Timeline& tl) noexcept {
    const ave::Rational fps = tl.frame_rate();
    return (fps.numerator() > 0 && fps.denominator() > 0) ? fps : ave::Rational{24, 1};
}

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

} // namespace

// --- TimelineTrackListModel ---

TimelineTrackListModel::TimelineTrackListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int TimelineTrackListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(rows_.size());
}

QVariant TimelineTrackListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size())) {
        return {};
    }

    const TrackRow& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case Qt::DisplayRole:
        case TrackNameRole:
            return row.track_name;
        case TrackIdRole:
            return row.track_id;
        case TrackTypeRole:
            return row.track_type;
        case TrackIndexRole:
            return row.track_index;
        case MutedRole:
            return row.muted;
        case SoloRole:
            return row.solo;
        default:
            return {};
    }
}

QHash<int, QByteArray> TimelineTrackListModel::roleNames() const {
    return {
        { TrackIdRole, QByteArrayLiteral("trackId") },
        { TrackNameRole, QByteArrayLiteral("trackName") },
        { TrackTypeRole, QByteArrayLiteral("trackType") },
        { TrackIndexRole, QByteArrayLiteral("trackIndex") },
        { MutedRole, QByteArrayLiteral("muted") },
        { SoloRole, QByteArrayLiteral("solo") }
    };
}

void TimelineTrackListModel::set_rows(std::vector<TrackRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

// --- TimelineClipListModel ---

TimelineClipListModel::TimelineClipListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int TimelineClipListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(rows_.size());
}

QVariant TimelineClipListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size())) {
        return {};
    }

    const ClipRow& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case Qt::DisplayRole:
        case ClipNameRole:
            return row.clip_name;
        case ClipIdRole:
            return row.clip_id;
        case TrackIdRole:
            return row.track_id;
        case AssetIdRole:
            return row.asset_id;
        case TimelineInSecRole:
            return row.timeline_in_sec;
        case DurationSecRole:
            return row.duration_sec;
        case SourceInSecRole:
            return row.source_in_sec;
        case TrackRowRole:
            return row.track_row;
        case IsSelectedRole:
            return row.is_selected;
        case HasWaveformRole:
            return row.has_waveform;
        case WaveformPeaksRole:
            return row.waveform_peaks;
        default:
            return {};
    }
}

QHash<int, QByteArray> TimelineClipListModel::roleNames() const {
    return {
        { ClipIdRole, QByteArrayLiteral("clipId") },
        { TrackIdRole, QByteArrayLiteral("trackId") },
        { AssetIdRole, QByteArrayLiteral("assetId") },
        { ClipNameRole, QByteArrayLiteral("clipName") },
        { TimelineInSecRole, QByteArrayLiteral("timelineInSec") },
        { DurationSecRole, QByteArrayLiteral("durationSec") },
        { SourceInSecRole, QByteArrayLiteral("sourceInSec") },
        { TrackRowRole, QByteArrayLiteral("trackRow") },
        { IsSelectedRole, QByteArrayLiteral("isSelected") },
        { HasWaveformRole, QByteArrayLiteral("hasWaveform") },
        { WaveformPeaksRole, QByteArrayLiteral("waveformPeaks") }
    };
}

void TimelineClipListModel::set_rows(std::vector<ClipRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

void TimelineClipListModel::update_selection(const QString& selected_clip_id) {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const bool next_selected = !selected_clip_id.isEmpty() && (rows_[i].clip_id == selected_clip_id);
        if (rows_[i].is_selected != next_selected) {
            rows_[i].is_selected = next_selected;
            const QModelIndex idx = index(static_cast<int>(i), 0);
            emit dataChanged(idx, idx, { IsSelectedRole });
        }
    }
}

// --- TimelineModel ---

TimelineModel::TimelineModel(ave::Project& project,
                             QObject* parent,
                             ave::ProjectSnapshotCoordinator* snapshot_coordinator,
                             std::shared_ptr<ave::MediaCache> media_cache)
    : QObject(parent),
      project_(project),
      snapshot_coordinator_(snapshot_coordinator),
      media_cache_(std::move(media_cache)),
      tracks_model_(new TimelineTrackListModel(this)),
      clips_model_(new TimelineClipListModel(this)) {
    viewport_.set_pixels_per_second(zoom_factor_ * 100.0);
    snapping_.set_enabled(snapping_enabled_);

    track_count_ = static_cast<int>(
        project_.timeline().video_tracks().size() + project_.timeline().audio_tracks().size());
    duration_seconds_ = project_.timeline().duration().to_seconds();
    can_undo_ = project_.command_history().can_undo();
    can_redo_ = project_.command_history().can_redo();
    is_dirty_ = project_.is_dirty();
    project_name_ = QString::fromStdString(project_.name());
    project_file_path_ = QString::fromStdWString(project_.file_path().wstring());

    rebuild_models();
    bind_history_listener();
}

TimelineModel::~TimelineModel() {
    unbind_history_listener();
}

int TimelineModel::trackCount() const noexcept {
    return track_count_;
}

double TimelineModel::durationSeconds() const noexcept {
    return duration_seconds_;
}

int TimelineModel::activeTool() const noexcept {
    return active_tool_;
}

void TimelineModel::setActiveTool(int tool) {
    if (tool < static_cast<int>(Tool::Select) || tool > static_cast<int>(Tool::Slip)) {
        return;
    }
    if (active_tool_ == tool) {
        return;
    }
    active_tool_ = tool;
    emit activeToolChanged();
}

bool TimelineModel::snappingEnabled() const noexcept {
    return snapping_enabled_;
}

void TimelineModel::setSnappingEnabled(bool enabled) {
    if (snapping_enabled_ == enabled) {
        return;
    }
    snapping_enabled_ = enabled;
    snapping_.set_enabled(enabled);
    emit snappingEnabledChanged();
}

double TimelineModel::zoomFactor() const noexcept {
    return zoom_factor_;
}

void TimelineModel::setZoomFactor(double zoom) {
    if (!std::isfinite(zoom)) {
        return;
    }
    const double clamped = std::clamp(zoom, 0.1, 20.0);
    if (std::abs(clamped - zoom_factor_) < 1e-6) {
        return;
    }
    zoom_factor_ = clamped;
    viewport_.set_pixels_per_second(zoom_factor_ * 100.0);
    emit zoomFactorChanged();
}

QString TimelineModel::selectedClipId() const {
    return selected_clip_id_;
}

void TimelineModel::setSelectedClipId(const QString& clipId) {
    const QString trimmed = clipId.trimmed();
    QString normalized;
    if (!trimmed.isEmpty()) {
        const std::string raw = trimmed.toStdString();
        if (project_.timeline().find_item(raw) == nullptr) {
            return;
        }
        normalized = trimmed;
    }

    if (selected_clip_id_ == normalized) {
        return;
    }
    selected_clip_id_ = normalized;
    if (clips_model_) {
        clips_model_->update_selection(selected_clip_id_);
    }
    emit selectedClipIdChanged();
}

bool TimelineModel::canUndo() const noexcept {
    return can_undo_;
}

bool TimelineModel::canRedo() const noexcept {
    return can_redo_;
}

bool TimelineModel::isDirty() const noexcept {
    return is_dirty_;
}

QString TimelineModel::projectName() const {
    return project_name_;
}

QString TimelineModel::projectFilePath() const {
    return project_file_path_;
}

QAbstractListModel* TimelineModel::tracksModel() const noexcept {
    return tracks_model_;
}

QAbstractListModel* TimelineModel::clipsModel() const noexcept {
    return clips_model_;
}

bool TimelineModel::insertClipFromAsset(const QString& assetId,
                                        const QString& trackId,
                                        double timelineInSeconds) {
    const std::string aid = assetId.trimmed().toStdString();
    if (aid.empty()) {
        return false;
    }

    const ave::MediaAsset* asset = project_.media_pool().find_asset(aid);
    if (!asset) {
        return false;
    }

    const ave::Track* target_track = nullptr;
    const std::string requested_track = trackId.trimmed().toStdString();
    if (!requested_track.empty()) {
        target_track = project_.timeline().find_track(requested_track);
        if (!target_track) {
            return false;
        }
    } else {
        const bool prefer_video = (asset->primary_video_stream() != nullptr) ||
                                  (asset->primary_audio_stream() == nullptr);
        if (prefer_video) {
            if (!project_.timeline().video_tracks().empty()) {
                target_track = &project_.timeline().video_tracks().front();
            } else if (!project_.timeline().audio_tracks().empty()) {
                target_track = &project_.timeline().audio_tracks().front();
            }
        } else {
            if (!project_.timeline().audio_tracks().empty()) {
                target_track = &project_.timeline().audio_tracks().front();
            } else if (!project_.timeline().video_tracks().empty()) {
                target_track = &project_.timeline().video_tracks().front();
            }
        }
        if (!target_track) {
            return false;
        }
    }

    ave::Rational clip_duration = asset->duration();
    if (clip_duration.numerator() <= 0 || clip_duration.denominator() <= 0) {
        clip_duration = ave::Rational{5, 1};
    }

    ave::Rational timeline_in{0, 1};
    if (timelineInSeconds == -1.0) {
        timeline_in = target_track->duration();
    } else {
        if (!std::isfinite(timelineInSeconds) || timelineInSeconds < 0.0) {
            return false;
        }
        const ave::TimeConversionResult conv =
            ave::ui_seconds_to_rational_pts_safe(timelineInSeconds, safe_timeline_fps(project_.timeline()));
        if (!conv.is_valid()) {
            return false;
        }
        timeline_in = conv.pts;
    }

    std::string clip_name = asset->display_name();
    if (clip_name.empty()) {
        clip_name = aid;
    }

    ave::TimelineItem item(
        "clip-" + ave::UUID::generate(),
        "mc-" + ave::UUID::generate(),
        aid,
        std::move(clip_name),
        timeline_in,
        clip_duration,
        ave::Rational{0, 1});

    const ave::CommandResult res = project_.execute_command(
        std::make_unique<ave::InsertClipCommand>(target_track->id(), std::move(item)));
    return res.is_success();
}

bool TimelineModel::splitAtPlayhead(double playheadSeconds) {
    if (!std::isfinite(playheadSeconds) || playheadSeconds < 0.0) {
        return false;
    }

    const ave::TimeConversionResult conv =
        ave::ui_seconds_to_rational_pts_safe(playheadSeconds, safe_timeline_fps(project_.timeline()));
    if (!conv.is_valid()) {
        return false;
    }
    const ave::Rational split_time = conv.pts;

    std::string target_clip_id = selected_clip_id_.toStdString();
    std::string target_track_id;

    if (!target_clip_id.empty()) {
        const ave::Track* trk = project_.timeline().find_track_for_item(target_clip_id);
        if (!trk) {
            return false;
        }
        target_track_id = trk->id();
    } else {
        auto find_in_tracks = [&](const std::vector<ave::Track>& tracks) {
            for (const auto& trk : tracks) {
                const ave::TimelineItem* it = trk.item_at(split_time);
                if (it && split_time > it->timeline_in() && split_time < it->timeline_out()) {
                    target_clip_id = it->id();
                    target_track_id = trk.id();
                    return true;
                }
            }
            return false;
        };

        if (!find_in_tracks(project_.timeline().video_tracks())) {
            (void)find_in_tracks(project_.timeline().audio_tracks());
        }
    }

    if (target_clip_id.empty() || target_track_id.empty()) {
        return false;
    }

    const ave::CommandResult res = project_.execute_command(
        std::make_unique<ave::SplitClipCommand>(target_track_id, target_clip_id, split_time));
    return res.is_success();
}

bool TimelineModel::deleteSelected() {
    if (selected_clip_id_.isEmpty()) {
        return false;
    }
    const std::string item_id = selected_clip_id_.toStdString();
    const ave::Track* trk = project_.timeline().find_track_for_item(item_id);
    if (!trk) {
        return false;
    }

    const ave::CommandResult res = project_.execute_command(
        std::make_unique<ave::DeleteClipCommand>(trk->id(), item_id));
    return res.is_success();
}

bool TimelineModel::moveSelectedClip(const QString& dstTrackId, double newInSeconds) {
    if (selected_clip_id_.isEmpty() || !std::isfinite(newInSeconds) || newInSeconds < 0.0) {
        return false;
    }

    const std::string item_id = selected_clip_id_.toStdString();
    const ave::Track* src_trk = project_.timeline().find_track_for_item(item_id);
    if (!src_trk) {
        return false;
    }

    const std::string target_track_id = dstTrackId.trimmed().isEmpty()
        ? src_trk->id()
        : dstTrackId.trimmed().toStdString();
    if (!project_.timeline().find_track(target_track_id)) {
        return false;
    }

    const ave::TimeConversionResult conv =
        ave::ui_seconds_to_rational_pts_safe(newInSeconds, safe_timeline_fps(project_.timeline()));
    if (!conv.is_valid()) {
        return false;
    }

    ave::Rational target_in = conv.pts;
    if (snapping_enabled_) {
        const ave::SnapResult snap = snapping_.find_snap(
            target_in, project_.timeline(), viewport_, ave::Rational{0, 1}, item_id);
        if (snap.snapped) {
            target_in = snap.snapped_time;
        }
    }

    const ave::CommandResult res = project_.execute_command(
        std::make_unique<ave::MoveClipCommand>(src_trk->id(), item_id, target_track_id, target_in));
    return res.is_success();
}

bool TimelineModel::toggleTrackMute(const QString& trackId) {
    const std::string tid = trackId.trimmed().toStdString();
    if (tid.empty()) {
        return false;
    }
    const ave::Track* trk = project_.timeline().find_track(tid);
    if (!trk) {
        return false;
    }

    const ave::CommandResult res = project_.execute_command(
        std::make_unique<ave::SetTrackMuteCommand>(tid, !trk->muted()));
    return res.is_success();
}

bool TimelineModel::undo() {
    return project_.undo().is_success();
}

bool TimelineModel::redo() {
    return project_.redo().is_success();
}

bool TimelineModel::newProject(const QString& name) {
    const QString trimmed = name.trimmed();
    const std::string project_title = trimmed.isEmpty() ? std::string("Untitled Project")
                                                        : trimmed.toStdString();
    ave::Project next_project(project_title);
    return replace_active_project(std::move(next_project));
}

bool TimelineModel::openProject(const QString& filePath) {
    const std::filesystem::path src = local_path_from_qstring(filePath);
    if (src.empty()) {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(src, ec) || !std::filesystem::is_regular_file(src, ec) || ec) {
        return false;
    }

    {
        QFile file(QString::fromStdWString(src.wstring()));
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        const QByteArray payload = file.readAll();
        QJsonParseError parse_err{};
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &parse_err);
        if (parse_err.error != QJsonParseError::NoError || !doc.isObject()) {
            return false;
        }
        const QJsonObject obj = doc.object();
        if (!obj.contains(QStringLiteral("schema_version")) ||
            !obj.contains(QStringLiteral("timeline"))) {
            return false;
        }
    }

    try {
        ave::Project loaded = ave::Project::load(src);
        return replace_active_project(std::move(loaded));
    } catch (const std::exception&) {
        return false;
    }
}

bool TimelineModel::saveProject(const QString& filePath) {
    std::filesystem::path dst;
    if (filePath.trimmed().isEmpty()) {
        dst = project_.file_path();
    } else {
        dst = local_path_from_qstring(filePath);
    }

    if (dst.empty()) {
        return false;
    }

    bool ok = false;
    try {
        ok = project_.save(dst);
    } catch (const std::exception&) {
        return false;
    }

    if (ok) {
        sync_from_project(false);
    }
    return ok;
}

bool TimelineModel::exportOtio(const QString& filePath) {
    const std::filesystem::path dst = local_path_from_qstring(filePath);
    if (dst.empty()) {
        return false;
    }

    std::error_code ec;
    if (dst.has_parent_path()) {
        std::filesystem::create_directories(dst.parent_path(), ec);
        if (ec) {
            return false;
        }
    }

    ave::OtioLossReport report;
    const std::string otio_json =
        ave::OtioInterchange::export_to_json(project_.timeline(), project_.media_pool(), report);

    std::filesystem::path tmp = dst;
    tmp += ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            return false;
        }
        ofs.write(otio_json.data(), static_cast<std::streamsize>(otio_json.size()));
        if (!ofs.good()) {
            return false;
        }
    }

    std::filesystem::rename(tmp, dst, ec);
    if (ec) {
        std::filesystem::remove(dst, ec);
        ec.clear();
        std::filesystem::rename(tmp, dst, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }

    return true;
}

int TimelineModel::relinkOfflineMedia(const QString& searchDirectory) {
    const std::filesystem::path dir = local_path_from_qstring(searchDirectory);
    if (dir.empty()) {
        return 0;
    }

    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec) || ec) {
        return 0;
    }

    const ave::RelinkReport report = snapshot_coordinator_
        ? snapshot_coordinator_->auto_relink_directory(dir, true)
        : ave::AssetRelinker::auto_relink_directory(project_.media_pool_mut(), dir, true);

    if (report.relinked_count > 0) {
        rebuild_models();
        emit projectReloaded();
    }

    return static_cast<int>(report.relinked_count);
}

void TimelineModel::set_media_cache(std::shared_ptr<ave::MediaCache> cache) {
    media_cache_ = std::move(cache);
    rebuild_models();
}

void TimelineModel::bind_history_listener() {
    unbind_history_listener();
    QPointer<TimelineModel> self(this);
    history_listener_id_ = project_.command_history_mut().add_change_listener_with_id([self]() {
        if (self.isNull()) {
            return;
        }
        self->sync_from_project(false);
    });
}

void TimelineModel::unbind_history_listener() {
    if (history_listener_id_ != 0) {
        project_.command_history_mut().remove_change_listener(history_listener_id_);
        history_listener_id_ = 0;
    }
}

void TimelineModel::sync_from_project(bool emit_reload_signal) {
    const ave::Timeline& tl = project_.timeline();
    const int next_track_count = static_cast<int>(tl.video_tracks().size() + tl.audio_tracks().size());
    const double next_dur = tl.duration().to_seconds();
    const bool next_can_undo = project_.command_history().can_undo();
    const bool next_can_redo = project_.command_history().can_redo();
    const bool next_dirty = project_.is_dirty();
    const QString next_name = QString::fromStdString(project_.name());
    const QString next_path = QString::fromStdWString(project_.file_path().wstring());

    bool selection_cleared = false;
    if (!selected_clip_id_.isEmpty() && tl.find_item(selected_clip_id_.toStdString()) == nullptr) {
        selected_clip_id_.clear();
        selection_cleared = true;
    }

    const bool tracks_changed = (next_track_count != track_count_);
    const bool dur_changed = (std::abs(next_dur - duration_seconds_) > 1e-9);
    const bool history_changed = (next_can_undo != can_undo_) || (next_can_redo != can_redo_);
    const bool state_changed = (next_dirty != is_dirty_) ||
                               (next_name != project_name_) ||
                               (next_path != project_file_path_);

    track_count_ = next_track_count;
    duration_seconds_ = next_dur;
    can_undo_ = next_can_undo;
    can_redo_ = next_can_redo;
    is_dirty_ = next_dirty;
    project_name_ = next_name;
    project_file_path_ = next_path;

    rebuild_models();

    if (tracks_changed) {
        emit trackCountChanged();
    }
    if (dur_changed) {
        emit durationSecondsChanged();
    }
    if (selection_cleared) {
        emit selectedClipIdChanged();
    }
    if (history_changed) {
        emit historyChanged();
    }
    if (state_changed) {
        emit projectStateChanged();
    }
    if (emit_reload_signal) {
        emit projectReloaded();
    }
}

void TimelineModel::rebuild_models() {
    const ave::Timeline& tl = project_.timeline();
    std::vector<TimelineTrackListModel::TrackRow> track_rows;
    std::vector<TimelineClipListModel::ClipRow> clip_rows;

    const std::size_t total_tracks = tl.video_tracks().size() + tl.audio_tracks().size();
    track_rows.reserve(total_tracks);

    int track_row_index = 0;
    auto append_track_group = [&](const std::vector<ave::Track>& tracks, const QString& type_str) {
        for (const ave::Track& trk : tracks) {
            TimelineTrackListModel::TrackRow trow;
            trow.track_id = QString::fromStdString(trk.id());
            trow.track_name = QString::fromStdString(trk.name());
            trow.track_type = type_str;
            trow.track_index = trk.index();
            trow.muted = trk.muted();
            trow.solo = trk.soloed();
            track_rows.push_back(trow);

            for (const ave::TimelineItem& item : trk.items()) {
                TimelineClipListModel::ClipRow crow;
                crow.clip_id = QString::fromStdString(item.id());
                crow.track_id = trow.track_id;
                crow.asset_id = QString::fromStdString(item.asset_id());
                crow.clip_name = QString::fromStdString(item.name());
                crow.timeline_in_sec = item.timeline_in().to_seconds();
                crow.duration_sec = item.duration().to_seconds();
                crow.source_in_sec = item.source_in().to_seconds();
                crow.track_row = track_row_index;
                crow.is_selected = (!selected_clip_id_.isEmpty() && crow.clip_id == selected_clip_id_);
                const double source_span_sec = (item.source_out() - item.source_in()).to_seconds();
                crow.waveform_peaks = build_waveform_peaks(item.asset_id(), crow.source_in_sec, source_span_sec);
                crow.has_waveform = !crow.waveform_peaks.isEmpty();
                clip_rows.push_back(std::move(crow));
            }

            ++track_row_index;
        }
    };

    append_track_group(tl.video_tracks(), QStringLiteral("video"));
    append_track_group(tl.audio_tracks(), QStringLiteral("audio"));

    if (tracks_model_) {
        tracks_model_->set_rows(std::move(track_rows));
    }
    if (clips_model_) {
        clips_model_->set_rows(std::move(clip_rows));
    }
}

QVariantList TimelineModel::build_waveform_peaks(const std::string& asset_id,
                                                 double source_in_sec,
                                                 double source_span_sec) const {
    if (!media_cache_ || asset_id.empty()) {
        return {};
    }

    const std::optional<ave::WaveformData> wf = media_cache_->get_waveform(asset_id);
    if (!wf.has_value() || !wf->is_valid()) {
        return {};
    }

    std::size_t total_samples = 0;
    for (const ave::WaveformChannel& ch : wf->channel_peaks) {
        total_samples = (std::max)(total_samples, ch.min_peaks.size());
        total_samples = (std::max)(total_samples, ch.max_peaks.size());
    }
    if (total_samples == 0) {
        return {};
    }

    const int pps = (wf->peaks_per_second > 0) ? wf->peaks_per_second : 100;
    std::size_t start_idx = 0;
    std::size_t end_idx = total_samples;

    if (std::isfinite(source_in_sec) && source_in_sec > 0.0) {
        const double raw_start = std::floor(source_in_sec * static_cast<double>(pps));
        if (raw_start >= static_cast<double>(total_samples)) {
            return {};
        }
        start_idx = static_cast<std::size_t>(raw_start);
    }

    if (std::isfinite(source_span_sec) && source_span_sec > 0.0) {
        const double raw_end = std::ceil((source_in_sec + source_span_sec) * static_cast<double>(pps));
        if (raw_end > static_cast<double>(start_idx)) {
            const std::size_t candidate_end = (raw_end >= static_cast<double>(total_samples))
                ? total_samples
                : static_cast<std::size_t>(raw_end);
            end_idx = (std::max)(start_idx + 1, candidate_end);
        }
    }

    if (end_idx <= start_idx) {
        return {};
    }

    const std::size_t slice_len = end_idx - start_idx;
    const std::size_t bucket_count = (std::min)(slice_len, kMaxClipWaveformBars);

    QVariantList result;
    result.reserve(static_cast<int>(bucket_count));

    for (std::size_t b = 0; b < bucket_count; ++b) {
        const std::size_t b_start = start_idx + (b * slice_len) / bucket_count;
        const std::size_t b_end = (std::max)(b_start + 1, start_idx + ((b + 1) * slice_len) / bucket_count);
        double peak = 0.0;
        for (std::size_t idx = b_start; idx < b_end && idx < total_samples; ++idx) {
            for (const ave::WaveformChannel& ch : wf->channel_peaks) {
                if (idx < ch.min_peaks.size()) {
                    const float v = ch.min_peaks[idx];
                    if (std::isfinite(v)) {
                        peak = (std::max)(peak, static_cast<double>(std::abs(v)));
                    }
                }
                if (idx < ch.max_peaks.size()) {
                    const float v = ch.max_peaks[idx];
                    if (std::isfinite(v)) {
                        peak = (std::max)(peak, static_cast<double>(std::abs(v)));
                    }
                }
            }
        }
        result.append(std::clamp(peak, 0.0, 1.0));
    }

    return result;
}

bool TimelineModel::replace_active_project(ave::Project next_project) {
    unbind_history_listener();
    const bool had_selection = !selected_clip_id_.isEmpty();
    selected_clip_id_.clear();

    if (snapshot_coordinator_) {
        snapshot_coordinator_->replace_project(std::move(next_project));
    } else {
        project_ = std::move(next_project);
    }

    bind_history_listener();
    sync_from_project(false);

    if (had_selection) {
        emit selectedClipIdChanged();
    }
    emit projectReloaded();
    return true;
}

} // namespace ave::desktop
