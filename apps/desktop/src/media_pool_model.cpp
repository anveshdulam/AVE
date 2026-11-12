#include "ave/media_pool_model.hpp"

#include "ave/media_asset.hpp"
#include "ave/media_cache.hpp"
#include "ave/media_extractor.hpp"
#include "ave/media_import_queue.hpp"
#include "ave/media_pool.hpp"
#include "ave/project.hpp"
#include "ave/timecode.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>

#include <QBuffer>
#include <QByteArray>
#include <QIODevice>
#include <QImage>
#include <QMetaObject>
#include <QPointer>

namespace ave::desktop {
namespace {

QString encode_thumbnail_data_url(const ave::ThumbnailData* thumb) {
    if (!thumb || !thumb->is_valid()) {
        return {};
    }

    QImage img(thumb->rgba_pixels.data(),
               thumb->width,
               thumb->height,
               thumb->width * 4,
               QImage::Format_RGBA8888);
    if (img.isNull()) {
        return {};
    }

    QByteArray png_bytes;
    QBuffer buffer(&png_bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !img.save(&buffer, "PNG")) {
        return {};
    }

    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png_bytes.toBase64());
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

MediaPoolModel::MediaPoolModel(ave::Project& project,
                               std::shared_ptr<ave::MediaImportQueue> import_queue,
                               QObject* parent,
                               std::shared_ptr<ave::MediaCache> media_cache)
    : QAbstractListModel(parent),
      project_(project),
      import_queue_(std::move(import_queue)),
      media_cache_(std::move(media_cache)),
      model_alive_(import_queue_ ? import_queue_->model_alive_flag()
                                 : std::make_shared<std::atomic<bool>>(true)) {
    model_alive_->store(true, std::memory_order_release);

    if (import_queue_) {
        QPointer<MediaPoolModel> self(this);
        std::shared_ptr<std::atomic<bool>> alive = model_alive_;

        import_queue_->set_callbacks(
            [self, alive](ave::ImportJobResult result) {
                if (!alive || !alive->load(std::memory_order_acquire)) {
                    return;
                }
                MediaPoolModel* target = self.data();
                if (!target) {
                    return;
                }
                QMetaObject::invokeMethod(
                    target,
                    [self, alive, res = std::move(result)]() mutable {
                        if (!alive || !alive->load(std::memory_order_acquire) || self.isNull()) {
                            return;
                        }
                        self->handle_import_completed(std::move(res));
                    },
                    Qt::QueuedConnection);
            },
            [self, alive](ave::ImportQueueCounts counts) {
                if (!alive || !alive->load(std::memory_order_acquire)) {
                    return;
                }
                MediaPoolModel* target = self.data();
                if (!target) {
                    return;
                }
                QMetaObject::invokeMethod(
                    target,
                    [self, alive, counts]() {
                        if (!alive || !alive->load(std::memory_order_acquire) || self.isNull()) {
                            return;
                        }
                        self->apply_import_counts(counts, true);
                    },
                    Qt::QueuedConnection);
            });

        const auto initial_counts = import_queue_->counts();
        active_import_count_ = static_cast<int>(initial_counts.active_count);
        pending_import_count_ = static_cast<int>(initial_counts.pending_count);
        last_counts_seq_ = initial_counts.seq;
    }

    if (import_queue_ && media_cache_ && !import_queue_->media_cache()) {
        import_queue_->set_media_cache(media_cache_);
    }

    if (media_cache_) {
        hydration_thread_ = std::thread([this]() { hydration_worker_loop(); });
    }

    reloadFromProject();
}

MediaPoolModel::~MediaPoolModel() {
    in_destructor_ = true;
    shutdown();
}

int MediaPoolModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(rows_.size());
}

QVariant MediaPoolModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size())) {
        return {};
    }

    const AssetRow& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case Qt::DisplayRole:
        case NameRole:
            return row.name;
        case AssetIdRole:
            return row.asset_id;
        case FilePathRole:
            return row.file_path;
        case DurationTextRole:
            return row.duration_text;
        case DurationSecondsRole:
            return row.duration_seconds;
        case ResolutionTextRole:
            return row.resolution_text;
        case FpsTextRole:
            return row.fps_text;
        case CodecTextRole:
            return row.codec_text;
        case MediaTypeRole:
            return row.media_type;
        case IsOnlineRole:
            return row.is_online;
        case ThumbnailDataUrlRole:
            return row.thumbnail_data_url;
        default:
            return {};
    }
}

QHash<int, QByteArray> MediaPoolModel::roleNames() const {
    return {
        { AssetIdRole, QByteArrayLiteral("assetId") },
        { NameRole, QByteArrayLiteral("name") },
        { FilePathRole, QByteArrayLiteral("filePath") },
        { DurationTextRole, QByteArrayLiteral("durationText") },
        { DurationSecondsRole, QByteArrayLiteral("durationSeconds") },
        { ResolutionTextRole, QByteArrayLiteral("resolutionText") },
        { FpsTextRole, QByteArrayLiteral("fpsText") },
        { CodecTextRole, QByteArrayLiteral("codecText") },
        { MediaTypeRole, QByteArrayLiteral("mediaType") },
        { IsOnlineRole, QByteArrayLiteral("isOnline") },
        { ThumbnailDataUrlRole, QByteArrayLiteral("thumbnailDataUrl") }
    };
}

int MediaPoolModel::assetCount() const noexcept {
    return static_cast<int>(rows_.size());
}

bool MediaPoolModel::isImporting() const noexcept {
    return (active_import_count_ + pending_import_count_) > 0;
}

int MediaPoolModel::activeImportCount() const noexcept {
    return active_import_count_;
}

int MediaPoolModel::pendingImportCount() const noexcept {
    return pending_import_count_;
}

QString MediaPoolModel::lastImportError() const {
    return last_import_error_;
}

QString MediaPoolModel::importMediaFile(const QString& localPath) {
    if (shutting_down_.load(std::memory_order_acquire) || !model_alive_->load(std::memory_order_acquire)) {
        set_last_import_error(QStringLiteral("Import rejected: media pool model is shut down"));
        return {};
    }
    if (!import_queue_) {
        set_last_import_error(QStringLiteral("Import rejected: import queue is unavailable"));
        return {};
    }

    const std::filesystem::path path = local_path_from_qstring(localPath);
    if (path.empty()) {
        set_last_import_error(QStringLiteral("Import rejected: empty file path"));
        return {};
    }

    std::string reject_reason;
    const std::string job_id = import_queue_->enqueue(path, &reject_reason);
    if (job_id.empty()) {
        set_last_import_error(reject_reason.empty()
                                  ? QStringLiteral("Import rejected")
                                  : QString::fromUtf8(reject_reason.data(), static_cast<qsizetype>(reject_reason.size())));
        return {};
    }

    sync_counts_immediately();
    return QString::fromStdString(job_id);
}

void MediaPoolModel::importMediaFiles(const QList<QUrl>& urls) {
    for (const QUrl& url : urls) {
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        const QString job_id = importMediaFile(path);
        if (job_id.isEmpty() && import_queue_) {
            const auto counts = import_queue_->counts();
            if ((counts.active_count + counts.pending_count) >= ave::MediaImportQueue::kMaxTotalAccepted) {
                break;
            }
        }
    }
}

void MediaPoolModel::cancelImport(const QString& jobId) {
    if (!import_queue_ || jobId.isEmpty()) {
        return;
    }
    import_queue_->cancel(jobId.toStdString());
    sync_counts_immediately();
}

void MediaPoolModel::cancelAllImports() {
    if (!import_queue_) {
        return;
    }
    import_queue_->cancel_all();
    sync_counts_immediately();
}

void MediaPoolModel::reloadFromProject() {
    cancel_hydration();

    const int old_count = assetCount();
    const auto& asset_map = project_.media_pool().assets();

    std::vector<const ave::MediaAsset*> sorted_assets;
    sorted_assets.reserve(asset_map.size());
    for (const auto& [id, asset] : asset_map) {
        (void)id;
        sorted_assets.push_back(&asset);
    }
    std::sort(sorted_assets.begin(), sorted_assets.end(),
        [](const ave::MediaAsset* a, const ave::MediaAsset* b) {
            if (a->display_name() != b->display_name()) {
                return a->display_name() < b->display_name();
            }
            return a->id() < b->id();
        });

    std::vector<ave::MediaAsset> to_hydrate;

    beginResetModel();
    rows_.clear();
    rows_.reserve(sorted_assets.size());
    for (const ave::MediaAsset* asset : sorted_assets) {
        std::optional<ave::ThumbnailData> cached_thumb;
        if (media_cache_) {
            cached_thumb = media_cache_->get_thumbnail_memory_only(
                *asset, ave::Rational{0, 1}, 160, 90);
            if (!cached_thumb.has_value()) {
                std::error_code ec;
                const bool file_exists = !asset->file_path().empty() &&
                                         std::filesystem::exists(asset->file_path(), ec) && !ec;
                if (!file_exists) {
                    cached_thumb = media_cache_->get_thumbnail(
                        asset->id(), ave::Rational{0, 1}, 160, 90);
                }
            }
        }

        AssetRow row = build_row(*asset, cached_thumb ? &*cached_thumb : nullptr);
        if (media_cache_ && !cached_thumb.has_value() && row.is_online &&
            asset->primary_video_stream() != nullptr) {
            to_hydrate.push_back(*asset);
        }
        rows_.push_back(std::move(row));
    }
    endResetModel();

    if (assetCount() != old_count) {
        emit assetCountChanged();
    }

    if (!to_hydrate.empty() && media_cache_ && !shutting_down_.load(std::memory_order_acquire)) {
        schedule_thumbnail_hydration(std::move(to_hydrate));
    }
}

void MediaPoolModel::shutdown() {
    model_alive_->store(false, std::memory_order_release);
    if (shutting_down_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(hydration_mutex_);
        hydration_stop_ = true;
        pending_hydration_assets_.clear();
        ++hydration_generation_;
        if (hydration_cancel_) {
            hydration_cancel_->store(true, std::memory_order_release);
        }
    }
    hydration_cv_.notify_all();
    if (hydration_thread_.joinable()) {
        hydration_thread_.join();
    }

    if (import_queue_) {
        import_queue_->clear_callbacks();
        import_queue_->shutdown();
    }

    apply_import_counts(ave::ImportQueueCounts{0, 0, last_counts_seq_ + 1}, !in_destructor_);
}

bool MediaPoolModel::is_hydrating() const noexcept {
    std::lock_guard<std::mutex> lock(hydration_mutex_);
    return hydration_active_ || !pending_hydration_assets_.empty();
}

uint64_t MediaPoolModel::hydration_decode_count_for_test() const noexcept {
    return hydration_decode_count_.load(std::memory_order_relaxed);
}

void MediaPoolModel::cancel_hydration() {
    std::lock_guard<std::mutex> lock(hydration_mutex_);
    ++hydration_generation_;
    pending_hydration_assets_.clear();
    if (hydration_cancel_) {
        hydration_cancel_->store(true, std::memory_order_release);
    }
    hydration_cancel_ = std::make_shared<std::atomic<bool>>(false);
}

void MediaPoolModel::schedule_thumbnail_hydration(std::vector<ave::MediaAsset> assets) {
    if (assets.empty() || !media_cache_ || shutting_down_.load(std::memory_order_acquire)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(hydration_mutex_);
        if (hydration_stop_) {
            return;
        }
        ++hydration_generation_;
        pending_hydration_assets_ = std::move(assets);
        if (hydration_cancel_) {
            hydration_cancel_->store(true, std::memory_order_release);
        }
        hydration_cancel_ = std::make_shared<std::atomic<bool>>(false);
    }
    hydration_cv_.notify_one();
}

void MediaPoolModel::hydration_worker_loop() {
    for (;;) {
        std::vector<ave::MediaAsset> batch;
        uint64_t gen = 0;
        std::shared_ptr<std::atomic<bool>> cancel;

        {
            std::unique_lock<std::mutex> lock(hydration_mutex_);
            hydration_active_ = false;
            hydration_cv_.wait(lock, [this]() {
                return hydration_stop_ || !pending_hydration_assets_.empty();
            });
            if (hydration_stop_) {
                return;
            }
            batch = std::move(pending_hydration_assets_);
            pending_hydration_assets_.clear();
            gen = hydration_generation_;
            cancel = hydration_cancel_;
            hydration_active_ = true;
        }

        const std::shared_ptr<ave::MediaCache> cache = media_cache_;
        const std::shared_ptr<std::atomic<bool>> alive = model_alive_;
        QPointer<MediaPoolModel> self(this);

        for (const ave::MediaAsset& asset : batch) {
            if (shutting_down_.load(std::memory_order_acquire) ||
                !alive || !alive->load(std::memory_order_acquire) ||
                (cancel && cancel->load(std::memory_order_acquire))) {
                break;
            }
            if (!cache) {
                break;
            }

            std::optional<ave::ThumbnailData> thumb = cache->get_or_extract_thumbnail(
                asset,
                ave::Rational{0, 1},
                160,
                90,
                [this, &asset](const std::shared_ptr<std::atomic<bool>>& cs) {
                    hydration_decode_count_.fetch_add(1, std::memory_order_relaxed);
                    return ave::ThumbnailExtractor::extract(
                        asset.file_path(), ave::Rational{0, 1}, 160, 90, cs);
                },
                cancel);

            if (!thumb.has_value() || !thumb->is_valid() ||
                (cancel && cancel->load(std::memory_order_acquire)) ||
                shutting_down_.load(std::memory_order_acquire)) {
                continue;
            }

            const QString data_url = encode_thumbnail_data_url(&*thumb);
            if (data_url.isEmpty()) {
                continue;
            }
            const QString asset_id_q = QString::fromStdString(asset.id());
            QMetaObject::invokeMethod(
                this,
                [self, alive, gen, asset_id_q, data_url]() {
                    if (!alive || !alive->load(std::memory_order_acquire) || self.isNull()) {
                        return;
                    }
                    self->apply_hydrated_thumbnail(gen, asset_id_q, data_url);
                },
                Qt::QueuedConnection);
        }
    }
}

void MediaPoolModel::apply_hydrated_thumbnail(uint64_t generation,
                                              const QString& asset_id,
                                              const QString& data_url) {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(hydration_mutex_);
        if (generation != hydration_generation_) {
            return;
        }
    }
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].asset_id == asset_id) {
            if (rows_[i].thumbnail_data_url == data_url) {
                return;
            }
            rows_[i].thumbnail_data_url = data_url;
            const QModelIndex idx = index(static_cast<int>(i), 0);
            emit dataChanged(idx, idx, {ThumbnailDataUrlRole});
            return;
        }
    }
}

MediaPoolModel::AssetRow MediaPoolModel::build_row(const ave::MediaAsset& asset,
                                                   const ave::ThumbnailData* thumbnail) const {
    AssetRow row;
    row.asset_id = QString::fromStdString(asset.id());
    row.name = QString::fromStdString(asset.display_name());
    row.file_path = QString::fromStdWString(asset.file_path().wstring());
    row.duration_seconds = asset.duration().to_seconds();

    const ave::SourceStream* video = asset.primary_video_stream();
    const ave::SourceStream* audio = asset.primary_audio_stream();

    ave::Rational tc_fps = project_.timeline().frame_rate();
    if (video && video->video_info().has_value() &&
        video->video_info()->frame_rate.denominator() > 0 &&
        video->video_info()->frame_rate.numerator() > 0) {
        tc_fps = video->video_info()->frame_rate;
    }

    if (asset.duration().numerator() >= 0 && asset.duration().denominator() > 0 && tc_fps.numerator() > 0) {
        try {
            row.duration_text = QString::fromStdString(ave::Timecode::from_rational(asset.duration(), tc_fps).to_string());
        } catch (...) {
            row.duration_text = QStringLiteral("00:00:00:00");
        }
    } else {
        row.duration_text = QStringLiteral("00:00:00:00");
    }

    if (video) {
        row.media_type = QStringLiteral("Video");
        row.codec_text = QString::fromStdString(video->codec_name());
        if (video->video_info().has_value()) {
            const auto& vi = *video->video_info();
            if (vi.width > 0 && vi.height > 0) {
                row.resolution_text = QStringLiteral("%1x%2").arg(vi.width).arg(vi.height);
            }
            if (vi.frame_rate.denominator() > 0 && vi.frame_rate.numerator() > 0) {
                row.fps_text = QStringLiteral("%1 fps").arg(vi.frame_rate.to_seconds(), 0, 'f', 2);
            }
        }
    } else if (audio) {
        row.media_type = QStringLiteral("Audio");
        row.codec_text = QString::fromStdString(audio->codec_name());
        if (audio->audio_info().has_value()) {
            const auto& ai = *audio->audio_info();
            row.resolution_text = QStringLiteral("%1 kHz").arg(static_cast<double>(ai.sample_rate) / 1000.0, 0, 'f', 1);
            if (ai.channels == 1) {
                row.fps_text = QStringLiteral("Mono");
            } else if (ai.channels == 2) {
                row.fps_text = QStringLiteral("Stereo");
            } else {
                row.fps_text = QStringLiteral("%1 ch").arg(ai.channels);
            }
        }
    } else {
        row.media_type = QStringLiteral("Unknown");
        row.codec_text = QString::fromStdString(asset.container_format());
    }

    std::error_code ec;
    row.is_online = !asset.file_path().empty() && std::filesystem::exists(asset.file_path(), ec);
    row.thumbnail_data_url = encode_thumbnail_data_url(thumbnail);
    return row;
}

void MediaPoolModel::upsert_asset_row(const ave::MediaAsset& asset, const ave::ThumbnailData* thumbnail) {
    AssetRow new_row = build_row(asset, thumbnail);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].asset_id == new_row.asset_id) {
            rows_[i] = std::move(new_row);
            const QModelIndex idx = index(static_cast<int>(i), 0);
            emit dataChanged(idx, idx);
            return;
        }
    }

    const int insert_row = static_cast<int>(rows_.size());
    beginInsertRows(QModelIndex(), insert_row, insert_row);
    rows_.push_back(std::move(new_row));
    endInsertRows();
    emit assetCountChanged();
}

void MediaPoolModel::apply_import_counts(const ave::ImportQueueCounts& counts, bool emit_signals) {
    if (counts.seq != 0 && counts.seq < last_counts_seq_) {
        return;
    }
    if (counts.seq != 0) {
        last_counts_seq_ = counts.seq;
    }

    const int new_active = static_cast<int>(counts.active_count);
    const int new_pending = static_cast<int>(counts.pending_count);
    const bool old_importing = isImporting();
    const bool counts_changed = (new_active != active_import_count_) || (new_pending != pending_import_count_);

    active_import_count_ = new_active;
    pending_import_count_ = new_pending;
    const bool new_importing = isImporting();

    if (!emit_signals) {
        return;
    }

    if (counts_changed) {
        emit importCountsChanged();
    }
    if (new_importing != old_importing) {
        emit isImportingChanged();
    }
}

void MediaPoolModel::handle_import_completed(ave::ImportJobResult result) {
    if (shutting_down_.load(std::memory_order_acquire) || !model_alive_->load(std::memory_order_acquire)) {
        return;
    }

    if (result.status == ave::ImportJobStatus::Succeeded && result.asset.has_value()) {
        const ave::ThumbnailData* thumb_ptr = result.thumbnail ? &*result.thumbnail : nullptr;
        const ave::MediaAsset& stored = project_.media_pool_mut().register_asset(*result.asset);

        if (media_cache_) {
            if (result.thumbnail && result.thumbnail->is_valid() &&
                !media_cache_->get_thumbnail_memory_only(
                    stored, ave::Rational{0, 1}, result.thumbnail->width, result.thumbnail->height).has_value()) {
                media_cache_->store_thumbnail(stored, ave::Rational{0, 1},
                                              result.thumbnail->width, result.thumbnail->height,
                                              *result.thumbnail);
            }
            if (result.waveform && result.waveform->is_valid() &&
                !media_cache_->get_waveform_memory_only(
                    stored, result.waveform->peaks_per_second > 0 ? result.waveform->peaks_per_second : 100).has_value()) {
                media_cache_->store_waveform(stored, *result.waveform);
            }
        }

        upsert_asset_row(stored, thumb_ptr);
        emit projectMediaMutated();
    } else if (result.status == ave::ImportJobStatus::Failed) {
        set_last_import_error(result.error_message.empty()
                                  ? QStringLiteral("Media import failed")
                                  : QString::fromUtf8(result.error_message.data(),
                                                      static_cast<qsizetype>(result.error_message.size())));
    }
    // ImportJobStatus::Cancelled: discard payload without modifying project media pool or lastImportError.

    if (import_queue_) {
        sync_counts_immediately();
    }
}

void MediaPoolModel::set_last_import_error(const QString& message) {
    if (last_import_error_ == message) {
        return;
    }
    last_import_error_ = message;
    emit lastImportErrorChanged();
}

void MediaPoolModel::sync_counts_immediately() {
    if (!import_queue_ || shutting_down_.load(std::memory_order_acquire)) {
        return;
    }
    apply_import_counts(import_queue_->counts(), !in_destructor_);
}

} // namespace ave::desktop
