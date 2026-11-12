#pragma once

#include "ave/media_asset.hpp"
#include "ave/media_cache.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ave {

enum class ImportJobStatus {
    Queued,
    Running,
    Succeeded,
    Failed,
    Cancelled
};

struct ImportQueueCounts {
    std::size_t active_count{0};
    std::size_t pending_count{0};
    std::uint64_t seq{0};

    [[nodiscard]] bool is_importing() const noexcept {
        return (active_count + pending_count) > 0;
    }
};

struct ImportJobResult {
    std::string job_id{};
    std::filesystem::path file_path{};
    ImportJobStatus status{ImportJobStatus::Failed};
    std::string error_message{};
    std::optional<MediaAsset> asset{std::nullopt};
    std::optional<ThumbnailData> thumbnail{std::nullopt};
    std::optional<WaveformData> waveform{std::nullopt};
};

/**
 * @brief Bounded asynchronous media import queue with cooperative FFmpeg cancellation
 * and a strict single-terminal-transition CAS state machine (Queued -> Running -> {Succeeded|Failed|Cancelled}).
 */
class MediaImportQueue final {
public:
    static constexpr std::size_t kMaxActiveImports = 2;
    static constexpr std::size_t kMaxPendingImports = 16;
    static constexpr std::size_t kMaxTotalAccepted = kMaxActiveImports + kMaxPendingImports;

    struct WorkOutput {
        ImportJobStatus terminal_status{ImportJobStatus::Failed};
        std::string error_message{};
        std::optional<MediaAsset> asset{std::nullopt};
        std::optional<ThumbnailData> thumbnail{std::nullopt};
        std::optional<WaveformData> waveform{std::nullopt};
    };

    using WorkFn = std::function<WorkOutput(const std::filesystem::path& file_path,
                                            const std::shared_ptr<std::atomic<bool>>& cancel_state)>;
    using CompletionCallback = std::function<void(ImportJobResult)>;
    using CountsCallback = std::function<void(ImportQueueCounts)>;

    struct Diagnostics {
        uint64_t probe_decodes{0};
        uint64_t probe_cache_hits{0};
        uint64_t thumbnail_decodes{0};
        uint64_t thumbnail_cache_hits{0};
        uint64_t waveform_decodes{0};
        uint64_t waveform_cache_hits{0};
    };

    explicit MediaImportQueue(WorkFn work_fn = {},
                              std::shared_ptr<MediaCache> media_cache = nullptr);
    ~MediaImportQueue();

    MediaImportQueue(const MediaImportQueue&) = delete;
    MediaImportQueue& operator=(const MediaImportQueue&) = delete;

    void set_media_cache(std::shared_ptr<MediaCache> media_cache);
    [[nodiscard]] std::shared_ptr<MediaCache> media_cache() const;
    [[nodiscard]] Diagnostics diagnostics() const noexcept;
    void reset_diagnostics() noexcept;

    void set_callbacks(CompletionCallback on_completed, CountsCallback on_counts_changed);
    void clear_callbacks();

    [[nodiscard]] std::shared_ptr<std::atomic<bool>> model_alive_flag() const noexcept {
        return model_alive_;
    }

    /**
     * @brief Enqueues a local media path for background probe/thumbnail/waveform import.
     * Fails immediately without blocking if total in-flight >= kMaxTotalAccepted (18) or shutting down.
     */
    [[nodiscard]] std::string enqueue(const std::filesystem::path& file_path,
                                      std::string* error_out = nullptr);

    /**
     * @brief Cancels a queued or active job atomically. Returns true if this call won cancellation.
     */
    bool cancel(const std::string& job_id);

    /**
     * @brief Cancels all queued and running jobs atomically.
     */
    void cancel_all();

    /**
     * @brief Cancels all remaining work, stops admission, and joins all active workers cleanly.
     */
    void shutdown();

    [[nodiscard]] ImportQueueCounts counts() const;
    [[nodiscard]] bool is_shutdown() const noexcept;

    [[nodiscard]] static WorkOutput default_import_work(
        const std::filesystem::path& file_path,
        const std::shared_ptr<std::atomic<bool>>& cancel_state);

private:
    struct JobRecord {
        std::string job_id{};
        std::filesystem::path file_path{};
        std::shared_ptr<std::atomic<bool>> cancel_state{std::make_shared<std::atomic<bool>>(false)};
        std::atomic<ImportJobStatus> status{ImportJobStatus::Queued};
    };

    void worker_loop();
    [[nodiscard]] WorkOutput execute_default_import(
        const std::filesystem::path& file_path,
        const std::shared_ptr<std::atomic<bool>>& cancel_state);
    [[nodiscard]] ImportQueueCounts snapshot_counts_locked() const noexcept;
    [[nodiscard]] ImportQueueCounts advance_and_snapshot_counts_locked() noexcept;
    void erase_pending_locked(const std::string& job_id);

    WorkFn work_fn_{};
    bool uses_custom_work_fn_{false};
    std::shared_ptr<MediaCache> media_cache_{};
    std::shared_ptr<std::atomic<bool>> model_alive_{std::make_shared<std::atomic<bool>>(true)};

    std::atomic<uint64_t> probe_decodes_{0};
    std::atomic<uint64_t> probe_cache_hits_{0};
    std::atomic<uint64_t> thumbnail_decodes_{0};
    std::atomic<uint64_t> thumbnail_cache_hits_{0};
    std::atomic<uint64_t> waveform_decodes_{0};
    std::atomic<uint64_t> waveform_cache_hits_{0};

    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::condition_variable workers_done_cv_{};

    std::deque<std::shared_ptr<JobRecord>> pending_queue_{};
    std::unordered_map<std::string, std::shared_ptr<JobRecord>> jobs_by_id_{};

    std::size_t active_count_{0};
    std::size_t active_worker_threads_{0};
    std::uint64_t next_job_num_{1};
    std::uint64_t counts_seq_{1};
    bool shutdown_requested_{false};

    CompletionCallback on_completed_{};
    CountsCallback on_counts_changed_{};
    std::vector<std::thread> workers_{};
};

} // namespace ave

