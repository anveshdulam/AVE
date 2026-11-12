#pragma once

#include "ave/project.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ave {

enum class RecoveryStatus {
    Missing,
    Valid,
    Stale,
    Corrupt
};

struct RecoveryInfo {
    RecoveryStatus status{RecoveryStatus::Missing};
    std::filesystem::path recovery_file_path{};
    int recovery_format_version{0};
    std::string project_id{};
    std::string project_name{};
    std::filesystem::path original_project_path{};
    uint64_t snapshot_revision{0};
    uint64_t saved_baseline_revision{0};
    int64_t snapshot_timestamp_ms{0};
    std::string project_sha256{};
    std::string detail_message{};

    [[nodiscard]] bool is_valid() const noexcept { return status == RecoveryStatus::Valid; }
    [[nodiscard]] bool is_recoverable() const noexcept { return status == RecoveryStatus::Valid; }
};

struct AutosaveCapture {
    uint64_t capture_seq{0};
    std::string project_id{};
    std::string project_name{};
    std::filesystem::path original_project_path{};
    std::filesystem::path target_recovery_path{};
    uint64_t snapshot_revision{0};
    uint64_t saved_baseline_revision{0};
    int64_t snapshot_timestamp_ms{0};
    uint64_t original_file_size{0};
    int64_t original_file_mtime_ns{0};
    std::string original_file_sha256{};
    std::string project_sha256{};
    std::string project_json{};
};

struct AutosaveWriteResult {
    bool succeeded{false};
    bool superseded{false};
    uint64_t capture_seq{0};
    std::string project_id{};
    uint64_t snapshot_revision{0};
    std::filesystem::path recovery_file_path{};
    std::string project_sha256{};
    std::string error_message{};
};

class RecoveryInspector {
public:
    static constexpr int kRecoveryFormatVersion = 1;

    [[nodiscard]] static std::filesystem::path named_recovery_path_for(
        const std::filesystem::path& original_project_path);

    [[nodiscard]] static std::filesystem::path untitled_recovery_path_for(
        const std::filesystem::path& recovery_dir,
        const std::string& project_id);

    [[nodiscard]] static std::filesystem::path recovery_path_for(
        const std::filesystem::path& recovery_dir,
        const std::string& project_id,
        const std::filesystem::path& original_project_path = {});

    [[nodiscard]] static std::string compute_sha256_hex(std::string_view data);

    [[nodiscard]] static RecoveryInfo inspect_file(
        const std::filesystem::path& recovery_file_path);

    [[nodiscard]] static RecoveryInfo inspect_file(
        const std::filesystem::path& recovery_file_path,
        const std::filesystem::path& reference_project_path,
        std::optional<uint64_t> saved_revision = std::nullopt);

    [[nodiscard]] static RecoveryInfo inspect_for_project(
        const Project& project,
        const std::filesystem::path& recovery_dir);

    [[nodiscard]] static std::vector<RecoveryInfo> scan_recovery_directory(
        const std::filesystem::path& recovery_dir);

    [[nodiscard]] static Project load_recovered_project(
        const std::filesystem::path& recovery_file_path);
};

class ProjectAutosaveEngine {
public:
    static constexpr std::chrono::milliseconds kDefaultInterval{std::chrono::seconds{60}};

    explicit ProjectAutosaveEngine(
        std::filesystem::path recovery_dir,
        std::chrono::milliseconds interval = kDefaultInterval);
    ~ProjectAutosaveEngine();

    ProjectAutosaveEngine(const ProjectAutosaveEngine&) = delete;
    ProjectAutosaveEngine& operator=(const ProjectAutosaveEngine&) = delete;

    [[nodiscard]] std::chrono::milliseconds interval() const noexcept;
    void set_interval(std::chrono::milliseconds interval) noexcept;

    [[nodiscard]] std::filesystem::path recovery_dir() const;
    void set_recovery_dir(std::filesystem::path dir);

    void set_completion_callback(std::function<void(const AutosaveWriteResult&)> callback);
    void set_pre_replace_hook_for_test(std::function<void(const AutosaveCapture&)> hook);

    [[nodiscard]] static std::optional<AutosaveCapture> capture_snapshot_if_dirty(
        const Project& project,
        const std::filesystem::path& recovery_dir,
        uint64_t capture_seq);

    /**
     * @brief Captures project state safely on the owning thread when dirty and dispatches
     * an asynchronous recovery write on the controlled worker if idle.
     *
     * Never queues or runs duplicate concurrent writes; if a write is already active,
     * flags a pending reschedule and returns false.
     */
    bool request_autosave(const Project& project, bool force = false);

    /**
     * @brief Commits a previously captured snapshot atomically under sequence-fence protection.
     *
     * Exposed both for internal worker execution and deterministic race/overwrite verification.
     */
    [[nodiscard]] AutosaveWriteResult commit_capture_blocking(const AutosaveCapture& capture);

    [[nodiscard]] uint64_t allocate_capture_sequence() noexcept {
        return next_capture_seq_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    [[nodiscard]] bool is_write_in_flight() const noexcept;
    [[nodiscard]] bool has_pending_reschedule() const noexcept;
    [[nodiscard]] uint64_t completed_count() const noexcept;
    [[nodiscard]] uint64_t failed_count() const noexcept;
    [[nodiscard]] AutosaveWriteResult last_result() const;

    bool wait_for_idle(std::chrono::milliseconds timeout = std::chrono::seconds{5});
    void reset_session_tracking();
    void shutdown();

private:
    void worker_loop();

    mutable std::mutex state_mutex_{};
    mutable std::mutex fence_mutex_{};
    std::mutex commit_mutex_{};
    std::condition_variable state_cv_{};

    std::filesystem::path recovery_dir_{};
    std::chrono::milliseconds interval_{kDefaultInterval};

    std::optional<AutosaveCapture> queued_capture_{};
    bool write_in_flight_{false};
    bool pending_reschedule_{false};
    bool stop_requested_{false};
    std::atomic<bool> cancel_requested_{false};
    std::atomic<bool> shutting_down_{false};
    std::atomic<uint64_t> next_capture_seq_{0};
    std::atomic<uint64_t> completed_count_{0};
    std::atomic<uint64_t> failed_count_{0};

    std::string in_flight_project_id_{};
    std::filesystem::path in_flight_target_{};
    uint64_t in_flight_revision_{0};
    std::string in_flight_project_sha256_{};

    std::string last_committed_project_id_{};
    std::filesystem::path last_committed_target_{};
    uint64_t last_committed_revision_{0};
    std::string last_committed_project_sha256_{};

    std::unordered_map<std::string, uint64_t> highest_submitted_seq_by_target_{};
    std::unordered_map<std::string, uint64_t> highest_committed_seq_by_target_{};

    AutosaveWriteResult last_result_{};
    std::function<void(const AutosaveWriteResult&)> completion_callback_{};
    std::function<void(const AutosaveCapture&)> pre_replace_hook_{};

    std::thread worker_thread_{};
};

} // namespace ave

