#include "ave/autosave_service.hpp"

#include "ave/project.hpp"

#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace ave::desktop {
namespace {

QString path_key_for_dedup(const std::filesystem::path& p) {
    std::error_code ec;
    const std::filesystem::path norm = std::filesystem::weakly_canonical(p, ec);
    const QString s = QString::fromStdWString((ec ? p.lexically_normal() : norm).wstring());
#ifdef _WIN32
    return s.toLower();
#else
    return s;
#endif
}

std::unique_ptr<QSettings> make_recent_settings(const std::filesystem::path& ini_path,
                                                bool use_native_settings) {
    if (!ini_path.empty()) {
        return std::make_unique<QSettings>(
            QString::fromStdWString(ini_path.wstring()), QSettings::IniFormat);
    }
    if (use_native_settings) {
        return std::make_unique<QSettings>(
            QSettings::UserScope,
            QStringLiteral("AVE"),
            QStringLiteral("Autonomous Video Editor"));
    }
    return nullptr;
}

} // namespace

std::filesystem::path AutosaveService::default_app_recovery_dir() {
    const QString app_data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!app_data.trimmed().isEmpty()) {
        return std::filesystem::path(app_data.toStdWString()) / "recovery";
    }
    return std::filesystem::temp_directory_path() / "ave_recovery";
}

std::filesystem::path AutosaveService::local_path_from_qstring(const QString& input) {
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    const QUrl url(trimmed);
    if (url.isValid() && url.isLocalFile()) {
        return std::filesystem::path(url.toLocalFile().toStdWString());
    }
    return std::filesystem::path(trimmed.toStdWString());
}

QVariantMap AutosaveService::to_recovery_variant_map(const ave::RecoveryInfo& info) {
    QVariantMap item;
    item.insert(QStringLiteral("recoveryFilePath"),
                QString::fromStdWString(info.recovery_file_path.wstring()));
    item.insert(QStringLiteral("originalProjectPath"),
                QString::fromStdWString(info.original_project_path.wstring()));
    item.insert(QStringLiteral("projectId"),
                QString::fromStdString(info.project_id));
    item.insert(QStringLiteral("projectName"),
                info.project_name.empty() ? QStringLiteral("Untitled Project")
                                          : QString::fromStdString(info.project_name));
    item.insert(QStringLiteral("snapshotRevision"),
                static_cast<quint64>(info.snapshot_revision));
    item.insert(QStringLiteral("snapshotTimestampMs"),
                static_cast<qint64>(info.snapshot_timestamp_ms));
    const QString formatted_time = (info.snapshot_timestamp_ms > 0)
        ? QDateTime::fromMSecsSinceEpoch(info.snapshot_timestamp_ms)
              .toLocalTime()
              .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : QStringLiteral("Unknown time");
    item.insert(QStringLiteral("formattedTimestamp"), formatted_time);
    item.insert(QStringLiteral("isUntitled"), info.original_project_path.empty());
    bool orig_exists = false;
    if (!info.original_project_path.empty()) {
        std::error_code ec;
        orig_exists = std::filesystem::exists(info.original_project_path, ec) &&
                      std::filesystem::is_regular_file(info.original_project_path, ec) && !ec;
    }
    item.insert(QStringLiteral("originalExists"), orig_exists);

    QString status_str = QStringLiteral("missing");
    QString status_label = QStringLiteral("Missing");
    QString explanation;
    bool safe_to_restore = false;

    switch (info.status) {
    case ave::RecoveryStatus::Valid:
        status_str = QStringLiteral("valid");
        status_label = QStringLiteral("Recoverable");
        safe_to_restore = true;
        if (info.original_project_path.empty()) {
            explanation = tr("AVE preserved an unsaved editing session automatically. "
                             "Restoring recovers those changes in the editor without creating "
                             "a project file until you save.");
        } else {
            explanation = tr("AVE found an autosave snapshot with changes not saved to the "
                             "original project file. Restoring will leave the original file "
                             "untouched until you choose Save.");
        }
        break;
    case ave::RecoveryStatus::Stale:
        status_str = QStringLiteral("stale");
        status_label = QStringLiteral("Outdated");
        safe_to_restore = false;
        explanation = tr("The original project file was saved or modified after this autosave "
                         "was captured, so automatic restore is disabled. You can safely open "
                         "the original project or discard this outdated snapshot.");
        break;
    case ave::RecoveryStatus::Corrupt:
        status_str = QStringLiteral("corrupt");
        status_label = QStringLiteral("Damaged");
        safe_to_restore = false;
        if (info.detail_message.find("identity") != std::string::npos) {
            explanation = tr("This recovery file belongs to a different project and cannot be "
                             "restored here. You can discard the recovery file or open the "
                             "original project.");
        } else {
            explanation = tr("This recovery file failed integrity validation and appears damaged "
                             "or incomplete, so AVE will not load it. You can safely discard "
                             "the damaged recovery file.");
        }
        break;
    case ave::RecoveryStatus::Missing:
    default:
        break;
    }

    item.insert(QStringLiteral("status"), status_str);
    item.insert(QStringLiteral("statusLabel"), status_label);
    item.insert(QStringLiteral("isSafeToRestore"), safe_to_restore);
    item.insert(QStringLiteral("explanation"), explanation);
    return item;
}

AutosaveService::AutosaveService(ave::Project& project,
                                 std::filesystem::path recovery_dir,
                                 int interval_ms,
                                 QObject* parent)
    : QObject(parent),
      project_(project),
      alive_flag_(std::make_shared<std::atomic<bool>>(true)),
      interval_ms_((std::max)(10, interval_ms)),
      timer_(new QTimer(this)),
      settings_ini_path_(recovery_dir.empty() ? std::filesystem::path{} : (recovery_dir / ".recent_projects.ini")),
      use_native_settings_(recovery_dir.empty()),
      engine_(recovery_dir.empty() ? default_app_recovery_dir() : std::move(recovery_dir),
              std::chrono::milliseconds{interval_ms_}) {
    timer_->setTimerType(Qt::CoarseTimer);
    timer_->setInterval(interval_ms_);
    connect(timer_, &QTimer::timeout, this, &AutosaveService::on_timer_tick);

    QPointer<AutosaveService> self(this);
    std::shared_ptr<std::atomic<bool>> alive = alive_flag_;
    engine_.set_completion_callback([self, alive](const ave::AutosaveWriteResult& result) {
        if (!alive || !alive->load(std::memory_order_acquire)) {
            return;
        }
        AutosaveService* target = self.data();
        if (!target) {
            return;
        }
        QMetaObject::invokeMethod(
            target,
            [self, alive, result]() {
                if (!alive || !alive->load(std::memory_order_acquire) || self.isNull()) {
                    return;
                }
                self->handle_write_completed(result);
            },
            Qt::QueuedConnection);
    });
}

AutosaveService::~AutosaveService() {
    shutdown();
}

void AutosaveService::setIntervalMs(int interval_ms) {
    const int clamped = (std::max)(10, interval_ms);
    if (interval_ms_ == clamped) {
        return;
    }
    interval_ms_ = clamped;
    engine_.set_interval(std::chrono::milliseconds{clamped});
    if (timer_) {
        timer_->setInterval(clamped);
    }
}

void AutosaveService::setRecentProjectsIniPath(const std::filesystem::path& ini_path) {
    settings_ini_path_ = ini_path;
    use_native_settings_ = ini_path.empty();
}

bool AutosaveService::isRunning() const noexcept {
    return timer_ && timer_->isActive() && !shutting_down_.load(std::memory_order_acquire);
}

bool AutosaveService::isWriteInFlight() const noexcept {
    return engine_.is_write_in_flight();
}

bool AutosaveService::hasPendingReschedule() const noexcept {
    return engine_.has_pending_reschedule();
}

quint64 AutosaveService::completedCount() const noexcept {
    return static_cast<quint64>(engine_.completed_count());
}

quint64 AutosaveService::failedCount() const noexcept {
    return static_cast<quint64>(engine_.failed_count());
}

std::filesystem::path AutosaveService::recoveryDir() const {
    return engine_.recovery_dir();
}

std::filesystem::path AutosaveService::currentRecoveryPath() const {
    return ave::RecoveryInspector::recovery_path_for(
        engine_.recovery_dir(), project_.id(), project_.file_path());
}

ave::RecoveryInfo AutosaveService::inspectCurrentProjectRecovery() const {
    return ave::RecoveryInspector::inspect_for_project(project_, engine_.recovery_dir());
}

QStringList AutosaveService::recentProjectPaths() const {
    if (!recent_tracking_enabled_) {
        return {};
    }
    const auto settings = make_recent_settings(settings_ini_path_, use_native_settings_);
    if (!settings) {
        return {};
    }
    return settings->value(QStringLiteral("projects/recent")).toStringList();
}

void AutosaveService::save_recent_project_paths(const QStringList& paths) const {
    if (!recent_tracking_enabled_) {
        return;
    }
    const auto settings = make_recent_settings(settings_ini_path_, use_native_settings_);
    if (!settings) {
        return;
    }
    settings->setValue(QStringLiteral("projects/recent"), paths);
    settings->sync();
}

void AutosaveService::recordRecentProject(const QString& projectPathOrUrl) {
    if (!recent_tracking_enabled_) {
        return;
    }
    const std::filesystem::path proj_path = local_path_from_qstring(projectPathOrUrl);
    if (proj_path.empty()) {
        return;
    }
    const QString canonical_q = QString::fromStdWString(proj_path.lexically_normal().wstring());
    const QString dedup_key = path_key_for_dedup(proj_path);

    QStringList updated;
    updated.append(canonical_q);
    const QStringList existing = recentProjectPaths();
    for (const QString& entry : existing) {
        if (updated.size() >= kMaxRecentProjects) {
            break;
        }
        const std::filesystem::path p(entry.toStdWString());
        if (!p.empty() && path_key_for_dedup(p) != dedup_key) {
            updated.append(entry);
        }
    }
    save_recent_project_paths(updated);
}

QVariantList AutosaveService::discoverStartupRecovery() const {
    std::vector<ave::RecoveryInfo> discovered;
    std::unordered_set<std::string> seen_recovery_keys;

    auto append_unique = [&discovered, &seen_recovery_keys](ave::RecoveryInfo info) {
        if (info.status == ave::RecoveryStatus::Missing || info.recovery_file_path.empty()) {
            return;
        }
        const std::string key = path_key_for_dedup(info.recovery_file_path).toStdString();
        if (seen_recovery_keys.insert(key).second) {
            discovered.push_back(std::move(info));
        }
    };

    // 1. Inspect only AVE's application-owned recovery directory (untitled & local snapshots).
    for (auto& info : ave::RecoveryInspector::scan_recovery_directory(engine_.recovery_dir())) {
        append_unique(std::move(info));
    }

    // 2. Inspect exact <project>.autosave sidecars for explicitly known recent projects only.
    const QStringList recents = recentProjectPaths();
    for (const QString& entry : recents) {
        const std::filesystem::path proj_path = local_path_from_qstring(entry);
        if (proj_path.empty()) {
            continue;
        }
        const std::filesystem::path sidecar =
            ave::RecoveryInspector::named_recovery_path_for(proj_path);
        std::error_code ec;
        if (std::filesystem::exists(sidecar, ec) && !ec) {
            append_unique(ave::RecoveryInspector::inspect_file(sidecar, proj_path));
        }
    }

    auto status_rank = [](ave::RecoveryStatus s) noexcept -> int {
        switch (s) {
        case ave::RecoveryStatus::Valid:   return 0;
        case ave::RecoveryStatus::Stale:   return 1;
        case ave::RecoveryStatus::Corrupt: return 2;
        case ave::RecoveryStatus::Missing:
        default:                           return 3;
        }
    };

    std::stable_sort(discovered.begin(), discovered.end(),
                     [&status_rank](const ave::RecoveryInfo& a, const ave::RecoveryInfo& b) {
                         const int ra = status_rank(a.status);
                         const int rb = status_rank(b.status);
                         if (ra != rb) {
                             return ra < rb;
                         }
                         return a.snapshot_timestamp_ms > b.snapshot_timestamp_ms;
                     });

    QVariantList out;
    out.reserve(static_cast<int>(discovered.size()));
    for (const auto& info : discovered) {
        out.append(to_recovery_variant_map(info));
    }
    return out;
}

QVariantMap AutosaveService::inspectProjectRecovery(const QString& projectPathOrUrl) const {
    const std::filesystem::path proj_path = local_path_from_qstring(projectPathOrUrl);
    if (proj_path.empty()) {
        ave::RecoveryInfo missing;
        missing.status = ave::RecoveryStatus::Missing;
        return to_recovery_variant_map(missing);
    }
    const std::filesystem::path sidecar = ave::RecoveryInspector::named_recovery_path_for(proj_path);
    return to_recovery_variant_map(ave::RecoveryInspector::inspect_file(sidecar, proj_path));
}

bool AutosaveService::originalProjectExists(const QString& projectPathOrUrl) const {
    const std::filesystem::path proj_path = local_path_from_qstring(projectPathOrUrl);
    if (proj_path.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(proj_path, ec) &&
           std::filesystem::is_regular_file(proj_path, ec) && !ec;
}

QVariantMap AutosaveService::discardRecoveryRecord(const QString& recoveryFilePath) {
    const std::filesystem::path rec_path = local_path_from_qstring(recoveryFilePath);
    QVariantMap out;
    if (rec_path.empty()) {
        const QString err = tr("No recovery file was specified to discard.");
        set_last_recovery_error(err);
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), err);
        return out;
    }

    const ave::RecoveryCleanupResult res = engine_.discard_recovery_file(rec_path);
    if (!res.succeeded) {
        const QString err = res.error_message.empty()
            ? tr("Could not remove the recovery file. Check folder permissions.")
            : QString::fromStdString(res.error_message);
        set_last_recovery_error(err);
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), err);
        return out;
    }

    set_last_recovery_error(QString{});
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("error"), QString{});
    return out;
}

QVariantMap AutosaveService::discardActiveProjectChanges(const QString& projectId,
                                                         const QString& projectFilePath) {
    const ave::RecoveryCleanupResult res = engine_.discard_for_project(
        projectId.trimmed().toStdString(),
        local_path_from_qstring(projectFilePath));
    QVariantMap out;
    if (!res.succeeded) {
        const QString err = res.error_message.empty()
            ? tr("Could not discard the unsaved recovery snapshot.")
            : QString::fromStdString(res.error_message);
        set_last_recovery_error(err);
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), err);
        return out;
    }

    set_last_recovery_error(QString{});
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("error"), QString{});
    return out;
}

void AutosaveService::set_last_recovery_error(const QString& err) {
    if (last_recovery_error_ == err) {
        return;
    }
    last_recovery_error_ = err;
    emit lastRecoveryErrorChanged();
}

bool AutosaveService::waitForIdle(int timeout_ms) {
    return engine_.wait_for_idle(std::chrono::milliseconds{(std::max)(1, timeout_ms)});
}

void AutosaveService::start() {
    if (shutting_down_.load(std::memory_order_acquire) || !timer_) {
        return;
    }
    if (!timer_->isActive()) {
        timer_->start(interval_ms_);
    }
}

void AutosaveService::stop() {
    if (timer_ && timer_->isActive()) {
        timer_->stop();
    }
}

void AutosaveService::triggerAutosaveNow() {
    on_timer_tick();
}

void AutosaveService::onProjectStateChanged() {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return;
    }
    if (project_.is_dirty() && engine_.is_write_in_flight()) {
        // Record pending follow-up capture without queueing a duplicate concurrent job.
        (void)engine_.request_autosave(project_, false);
    }
}

void AutosaveService::onProjectReloaded() {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return;
    }
    last_error_.clear();
    set_last_recovery_error(QString{});
    engine_.reset_session_tracking();
}

void AutosaveService::onProjectOpened(const QString& openedFilePath) {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return;
    }
    recordRecentProject(openedFilePath);
}

void AutosaveService::onProjectSaved(const QString& savedFilePath,
                                     const QString& previousFilePath,
                                     quint64 savedRevision,
                                     const QString& projectId) {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return;
    }
    const std::filesystem::path saved_path = local_path_from_qstring(savedFilePath);
    const std::filesystem::path prev_path = local_path_from_qstring(previousFilePath);
    if (!saved_path.empty()) {
        recordRecentProject(QString::fromStdWString(saved_path.wstring()));
    }

    const ave::RecoveryCleanupResult res = engine_.cleanup_after_save(
        projectId.trimmed().toStdString(),
        saved_path,
        prev_path,
        static_cast<uint64_t>(savedRevision));
    if (!res.succeeded) {
        set_last_recovery_error(res.error_message.empty()
            ? tr("Project saved, but an old recovery snapshot could not be removed.")
            : QString::fromStdString(res.error_message));
    } else {
        set_last_recovery_error(QString{});
    }
}

void AutosaveService::shutdown() {
    if (shutting_down_.exchange(true, std::memory_order_acq_rel)) {
        engine_.shutdown();
        return;
    }
    if (alive_flag_) {
        alive_flag_->store(false, std::memory_order_release);
    }
    stop();
    engine_.shutdown();
}

void AutosaveService::on_timer_tick() {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return;
    }
    if (!project_.is_dirty()) {
        return;
    }
    const bool dispatched = engine_.request_autosave(project_, false);
    if (dispatched) {
        emit writeInFlightChanged();
    }
}

void AutosaveService::handle_write_completed(const ave::AutosaveWriteResult& result) {
    emit writeInFlightChanged();

    if (result.succeeded) {
        last_error_.clear();
        emit autosaveSucceeded(
            QString::fromStdWString(result.recovery_file_path.wstring()),
            static_cast<quint64>(result.snapshot_revision));
    } else if (!result.superseded) {
        last_error_ = QString::fromStdString(result.error_message);
        emit autosaveFailed(last_error_);
    }

    if (!shutting_down_.load(std::memory_order_acquire) &&
        engine_.has_pending_reschedule() &&
        project_.is_dirty()) {
        const bool dispatched = engine_.request_autosave(project_, false);
        if (dispatched) {
            emit writeInFlightChanged();
        }
    }
}

} // namespace ave::desktop