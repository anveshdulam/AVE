#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ave {

using JobId = uint64_t;

enum class JobPriority {
    Low = 0,
    Normal = 1,
    High = 2
};

enum class JobStatus {
    Queued,
    Running,
    Completed,
    Failed,
    Cancelled,
    Paused
};

using PerceptionTask = std::function<bool(
    const std::atomic<bool>& cancel_token,
    std::function<void(float progress)> report_progress
)>;

/**
 * @brief Thread-safe background scheduler for AI video perception jobs.
 *
 * Enforces the core architecture invariant: "Playback outranks analysis".
 * When playback or real-time compositing is active, perception workers pause
 * or yield CPU/GPU execution.
 */
class PerceptionScheduler {
public:
    explicit PerceptionScheduler(size_t worker_threads = 2);
    ~PerceptionScheduler();

    PerceptionScheduler(const PerceptionScheduler&) = delete;
    PerceptionScheduler& operator=(const PerceptionScheduler&) = delete;

    /**
     * @brief Submits a new perception task to the background queue.
     */
    JobId submit(std::string name, JobPriority priority, PerceptionTask task);

    /**
     * @brief Requests cancellation of an in-flight or queued job.
     */
    bool cancel(JobId id);

    /**
     * @brief Cancels all pending and in-flight jobs.
     */
    void cancel_all();

    /**
     * @brief Queries the current status of a job.
     */
    [[nodiscard]] JobStatus status(JobId id) const;

    /**
     * @brief Queries the progress of a job (0.0 to 1.0).
     */
    [[nodiscard]] float progress(JobId id) const;

    /**
     * @brief Waits for a specific job to finish with a timeout.
     */
    bool wait(JobId id, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    /**
     * @brief Waits for all queued and running jobs to complete.
     */
    void wait_all();

    /**
     * @brief Sets whether real-time timeline playback is currently active.
     *
     * When active, worker threads suspend job execution until playback stops.
     */
    void set_playback_active(bool active);

    [[nodiscard]] bool is_playback_active() const noexcept {
        return m_playback_active.load(std::memory_order_acquire);
    }

    [[nodiscard]] size_t worker_count() const noexcept { return m_workers.size(); }

private:
    struct InternalJob {
        JobId id;
        std::string name;
        JobPriority priority;
        PerceptionTask task;
        std::atomic<bool> cancel_token{false};
        std::atomic<float> progress{0.0f};
        std::atomic<JobStatus> status{JobStatus::Queued};
    };

    struct JobComparator {
        bool operator()(const std::shared_ptr<InternalJob>& a, const std::shared_ptr<InternalJob>& b) const {
            return static_cast<int>(a->priority) < static_cast<int>(b->priority);
        }
    };

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::condition_variable m_completion_cv;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_playback_active{false};
    JobId m_next_id{1};

    std::priority_queue<std::shared_ptr<InternalJob>,
                        std::vector<std::shared_ptr<InternalJob>>,
                        JobComparator> m_queue;

    std::unordered_map<JobId, std::shared_ptr<InternalJob>> m_jobs;
    std::vector<std::thread> m_workers;

    void worker_loop();
};

} // namespace ave
