#pragma once

#include "ave/project_recovery.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>

#include <QObject>
#include <QString>
#include <QTimer>

namespace ave {
class Project;
} // namespace ave

namespace ave::desktop {

class AutosaveService final : public QObject {
    Q_OBJECT

public:
    static constexpr int kDefaultAutosaveIntervalMs = 60000;

    explicit AutosaveService(ave::Project& project,
                             std::filesystem::path recovery_dir = {},
                             int interval_ms = kDefaultAutosaveIntervalMs,
                             QObject* parent = nullptr);
    ~AutosaveService() override;

    AutosaveService(const AutosaveService&) = delete;
    AutosaveService& operator=(const AutosaveService&) = delete;

    [[nodiscard]] int intervalMs() const noexcept { return interval_ms_; }
    void setIntervalMs(int interval_ms);

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool isWriteInFlight() const noexcept;
    [[nodiscard]] bool hasPendingReschedule() const noexcept;
    [[nodiscard]] quint64 completedCount() const noexcept;
    [[nodiscard]] quint64 failedCount() const noexcept;
    [[nodiscard]] QString lastError() const { return last_error_; }
    [[nodiscard]] std::filesystem::path recoveryDir() const;
    [[nodiscard]] std::filesystem::path currentRecoveryPath() const;
    [[nodiscard]] ave::RecoveryInfo inspectCurrentProjectRecovery() const;

    bool waitForIdle(int timeout_ms = 5000);

public slots:
    void start();
    void stop();
    void triggerAutosaveNow();
    void onProjectStateChanged();
    void onProjectReloaded();
    void shutdown();

signals:
    void autosaveSucceeded(const QString& recoveryPath, quint64 snapshotRevision);
    void autosaveFailed(const QString& errorMessage);
    void writeInFlightChanged();

private:
    void on_timer_tick();
    void handle_write_completed(const ave::AutosaveWriteResult& result);
    static std::filesystem::path default_app_recovery_dir();

    ave::Project& project_;
    std::shared_ptr<std::atomic<bool>> alive_flag_;
    std::atomic<bool> shutting_down_{false};
    int interval_ms_{kDefaultAutosaveIntervalMs};
    QTimer* timer_{nullptr};
    QString last_error_{};
    ave::ProjectAutosaveEngine engine_;
};

} // namespace ave::desktop

