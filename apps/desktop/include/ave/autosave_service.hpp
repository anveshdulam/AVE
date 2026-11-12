#pragma once

#include "ave/project_recovery.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace ave {
class Project;
} // namespace ave

namespace ave::desktop {

class AutosaveService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastRecoveryError READ lastRecoveryError NOTIFY lastRecoveryErrorChanged)

public:
    static constexpr int kDefaultAutosaveIntervalMs = 60000;
    static constexpr int kMaxRecentProjects = 5;

    explicit AutosaveService(ave::Project& project,
                             std::filesystem::path recovery_dir = {},
                             int interval_ms = kDefaultAutosaveIntervalMs,
                             QObject* parent = nullptr);
    ~AutosaveService() override;

    AutosaveService(const AutosaveService&) = delete;
    AutosaveService& operator=(const AutosaveService&) = delete;

    [[nodiscard]] int intervalMs() const noexcept { return interval_ms_; }
    void setIntervalMs(int interval_ms);
    void setRecentProjectsIniPath(const std::filesystem::path& ini_path);
    void setRecentProjectsTrackingEnabled(bool enabled) noexcept { recent_tracking_enabled_ = enabled; }

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool isWriteInFlight() const noexcept;
    [[nodiscard]] bool hasPendingReschedule() const noexcept;
    [[nodiscard]] quint64 completedCount() const noexcept;
    [[nodiscard]] quint64 failedCount() const noexcept;
    [[nodiscard]] QString lastError() const { return last_error_; }
    [[nodiscard]] QString lastRecoveryError() const { return last_recovery_error_; }
    [[nodiscard]] std::filesystem::path recoveryDir() const;
    [[nodiscard]] std::filesystem::path currentRecoveryPath() const;
    [[nodiscard]] ave::RecoveryInfo inspectCurrentProjectRecovery() const;
    [[nodiscard]] ave::ProjectAutosaveEngine& engine() noexcept { return engine_; }

    Q_INVOKABLE QVariantList discoverStartupRecovery() const;
    Q_INVOKABLE QVariantMap inspectProjectRecovery(const QString& projectPathOrUrl) const;
    Q_INVOKABLE QVariantMap discardRecoveryRecord(const QString& recoveryFilePath);
    Q_INVOKABLE QVariantMap discardActiveProjectChanges(const QString& projectId, const QString& projectFilePath);
    Q_INVOKABLE bool originalProjectExists(const QString& projectPathOrUrl) const;
    Q_INVOKABLE void recordRecentProject(const QString& projectPathOrUrl);
    [[nodiscard]] QStringList recentProjectPaths() const;

    bool waitForIdle(int timeout_ms = 5000);

public slots:
    void start();
    void stop();
    void triggerAutosaveNow();
    void onProjectStateChanged();
    void onProjectReloaded();
    void onProjectOpened(const QString& openedFilePath);
    void onProjectSaved(const QString& savedFilePath,
                        const QString& previousFilePath,
                        quint64 savedRevision,
                        const QString& projectId);
    void shutdown();

signals:
    void autosaveSucceeded(const QString& recoveryPath, quint64 snapshotRevision);
    void autosaveFailed(const QString& errorMessage);
    void writeInFlightChanged();
    void lastRecoveryErrorChanged();

private:
    void on_timer_tick();
    void handle_write_completed(const ave::AutosaveWriteResult& result);
    void set_last_recovery_error(const QString& err);
    void save_recent_project_paths(const QStringList& paths) const;
    static QVariantMap to_recovery_variant_map(const ave::RecoveryInfo& info);
    static std::filesystem::path local_path_from_qstring(const QString& input);
    static std::filesystem::path default_app_recovery_dir();

    ave::Project& project_;
    std::shared_ptr<std::atomic<bool>> alive_flag_;
    std::atomic<bool> shutting_down_{false};
    int interval_ms_{kDefaultAutosaveIntervalMs};
    QTimer* timer_{nullptr};
    QString last_error_{};
    QString last_recovery_error_{};
    std::filesystem::path settings_ini_path_{};
    bool use_native_settings_{true};
    bool recent_tracking_enabled_{true};
    ave::ProjectAutosaveEngine engine_;
};

} // namespace ave::desktop

