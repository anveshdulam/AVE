#include "ave/autosave_service.hpp"

#include "ave/project.hpp"

#include <QMetaObject>
#include <QPointer>
#include <QStandardPaths>

#include <algorithm>
#include <chrono>
#include <utility>

namespace ave::desktop {

std::filesystem::path AutosaveService::default_app_recovery_dir() {
    const QString app_data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!app_data.trimmed().isEmpty()) {
        return std::filesystem::path(app_data.toStdWString()) / "recovery";
    }
    return std::filesystem::temp_directory_path() / "ave_recovery";
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
    engine_.reset_session_tracking();
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

