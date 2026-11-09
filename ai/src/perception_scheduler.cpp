#include <ave/perception_scheduler.hpp>

namespace ave {

PerceptionScheduler::PerceptionScheduler(size_t worker_threads) {
    if (worker_threads == 0) worker_threads = 1;
    m_workers.reserve(worker_threads);
    for (size_t i = 0; i < worker_threads; ++i) {
        m_workers.emplace_back(&PerceptionScheduler::worker_loop, this);
    }
}

PerceptionScheduler::~PerceptionScheduler() {
    cancel_all();
    {
        std::scoped_lock lock(m_mutex);
        m_stop.store(true, std::memory_order_release);
    }
    m_cv.notify_all();
    m_completion_cv.notify_all();

    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

JobId PerceptionScheduler::submit(std::string name, JobPriority priority, PerceptionTask task) {
    auto job = std::make_shared<InternalJob>();
    job->name = std::move(name);
    job->priority = priority;
    job->task = std::move(task);

    JobId id = 0;
    {
        std::scoped_lock lock(m_mutex);
        id = m_next_id++;
        job->id = id;
        m_jobs[id] = job;
        m_queue.push(job);
    }
    m_cv.notify_one();
    return id;
}

bool PerceptionScheduler::cancel(JobId id) {
    std::scoped_lock lock(m_mutex);
    auto it = m_jobs.find(id);
    if (it == m_jobs.end()) return false;

    auto job = it->second;
    job->cancel_token.store(true, std::memory_order_release);
    if (job->status.load() == JobStatus::Queued) {
        job->status.store(JobStatus::Cancelled, std::memory_order_release);
        m_completion_cv.notify_all();
    }
    return true;
}

void PerceptionScheduler::cancel_all() {
    std::scoped_lock lock(m_mutex);
    for (auto& [id, job] : m_jobs) {
        job->cancel_token.store(true, std::memory_order_release);
        if (job->status.load() == JobStatus::Queued) {
            job->status.store(JobStatus::Cancelled, std::memory_order_release);
        }
    }
    m_completion_cv.notify_all();
}

JobStatus PerceptionScheduler::status(JobId id) const {
    std::scoped_lock lock(m_mutex);
    auto it = m_jobs.find(id);
    if (it == m_jobs.end()) return JobStatus::Failed;
    return it->second->status.load(std::memory_order_acquire);
}

float PerceptionScheduler::progress(JobId id) const {
    std::scoped_lock lock(m_mutex);
    auto it = m_jobs.find(id);
    if (it == m_jobs.end()) return 0.0f;
    return it->second->progress.load(std::memory_order_acquire);
}

bool PerceptionScheduler::wait(JobId id, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);
    auto it = m_jobs.find(id);
    if (it == m_jobs.end()) return false;
    auto job = it->second;

    return m_completion_cv.wait_for(lock, timeout, [&] {
        auto st = job->status.load(std::memory_order_acquire);
        return st == JobStatus::Completed || st == JobStatus::Failed || st == JobStatus::Cancelled;
    });
}

void PerceptionScheduler::wait_all() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_completion_cv.wait(lock, [&] {
        for (const auto& [id, job] : m_jobs) {
            auto st = job->status.load(std::memory_order_acquire);
            if (st == JobStatus::Queued || st == JobStatus::Running) {
                return false;
            }
        }
        return true;
    });
}

void PerceptionScheduler::set_playback_active(bool active) {
    m_playback_active.store(active, std::memory_order_release);
    if (!active) {
        m_cv.notify_all();
    }
}

void PerceptionScheduler::worker_loop() {
    while (!m_stop.load(std::memory_order_acquire)) {
        std::shared_ptr<InternalJob> job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [&] {
                return m_stop.load(std::memory_order_acquire) ||
                       (!m_queue.empty() && !m_playback_active.load(std::memory_order_acquire));
            });

            if (m_stop.load(std::memory_order_acquire)) return;

            // If playback became active, wait until it finishes
            while (m_playback_active.load(std::memory_order_acquire) && !m_stop.load(std::memory_order_acquire)) {
                m_cv.wait(lock, [&] {
                    return m_stop.load(std::memory_order_acquire) ||
                           !m_playback_active.load(std::memory_order_acquire);
                });
            }

            if (m_stop.load(std::memory_order_acquire)) return;
            if (m_queue.empty()) continue;

            job = m_queue.top();
            m_queue.pop();

            if (job->cancel_token.load(std::memory_order_acquire)) {
                job->status.store(JobStatus::Cancelled, std::memory_order_release);
                m_completion_cv.notify_all();
                continue;
            }

            job->status.store(JobStatus::Running, std::memory_order_release);
        }

        bool success = false;
        try {
            success = job->task(job->cancel_token, [job](float p) {
                job->progress.store(p, std::memory_order_release);
            });
        } catch (...) {
            success = false;
        }

        {
            std::scoped_lock lock(m_mutex);
            if (job->cancel_token.load(std::memory_order_acquire)) {
                job->status.store(JobStatus::Cancelled, std::memory_order_release);
            } else if (success) {
                job->progress.store(1.0f, std::memory_order_release);
                job->status.store(JobStatus::Completed, std::memory_order_release);
            } else {
                job->status.store(JobStatus::Failed, std::memory_order_release);
            }
        }
        m_completion_cv.notify_all();
    }
}

} // namespace ave
