#include "ave/media_import_queue.hpp"

#include "ave/ffmpeg_probe.hpp"
#include "ave/media_extractor.hpp"

#include <algorithm>
#include <utility>

namespace ave {

MediaImportQueue::MediaImportQueue(WorkFn work_fn)
    : work_fn_(work_fn ? std::move(work_fn) : WorkFn(&MediaImportQueue::default_import_work)) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_worker_threads_ = kMaxActiveImports;
    workers_.reserve(kMaxActiveImports);
    for (std::size_t i = 0; i < kMaxActiveImports; ++i) {
        workers_.emplace_back([this]() { worker_loop(); });
    }
}

MediaImportQueue::~MediaImportQueue() {
    model_alive_->store(false, std::memory_order_release);
    shutdown();
}

void MediaImportQueue::set_callbacks(CompletionCallback on_completed, CountsCallback on_counts_changed) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_completed_ = std::move(on_completed);
    on_counts_changed_ = std::move(on_counts_changed);
}

void MediaImportQueue::clear_callbacks() {
    std::lock_guard<std::mutex> lock(mutex_);
    on_completed_ = nullptr;
    on_counts_changed_ = nullptr;
}

std::string MediaImportQueue::enqueue(const std::filesystem::path& file_path, std::string* error_out) {
    if (file_path.empty()) {
        if (error_out) {
            *error_out = "Import rejected: file path is empty";
        }
        return {};
    }

    std::string assigned_id;
    CountsCallback counts_cb;
    ImportQueueCounts counts;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_requested_) {
            if (error_out) {
                *error_out = "Import rejected: import queue is shutting down";
            }
            return {};
        }

        const std::size_t in_flight = active_count_ + pending_queue_.size();
        if (in_flight >= kMaxTotalAccepted) {
            if (error_out) {
                *error_out = "Import rejected: queue full (max 2 active + 16 pending = 18 jobs)";
            }
            return {};
        }

        assigned_id = "import-" + std::to_string(next_job_num_++);
        auto job = std::make_shared<JobRecord>();
        job->job_id = assigned_id;
        job->file_path = file_path;
        job->status.store(ImportJobStatus::Queued, std::memory_order_release);

        pending_queue_.push_back(job);
        jobs_by_id_.emplace(assigned_id, std::move(job));
        counts = advance_and_snapshot_counts_locked();
        counts_cb = on_counts_changed_;
    }

    cv_.notify_one();

    if (counts_cb) {
        counts_cb(counts);
    }
    return assigned_id;
}

bool MediaImportQueue::cancel(const std::string& job_id) {
    if (job_id.empty()) {
        return false;
    }

    bool won_cancel = false;
    ImportJobResult cancel_result;
    CompletionCallback completed_cb;
    CountsCallback counts_cb;
    ImportQueueCounts counts;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_by_id_.find(job_id);
        if (it == jobs_by_id_.end()) {
            return false;
        }

        const auto job = it->second;
        job->cancel_state->store(true, std::memory_order_release);

        ImportJobStatus expected_queued = ImportJobStatus::Queued;
        if (job->status.compare_exchange_strong(expected_queued, ImportJobStatus::Cancelled, std::memory_order_acq_rel)) {
            won_cancel = true;
            erase_pending_locked(job_id);
            jobs_by_id_.erase(it);
        } else {
            ImportJobStatus expected_running = ImportJobStatus::Running;
            if (job->status.compare_exchange_strong(expected_running, ImportJobStatus::Cancelled, std::memory_order_acq_rel)) {
                won_cancel = true;
                jobs_by_id_.erase(it);
            }
        }

        if (won_cancel) {
            cancel_result.job_id = job->job_id;
            cancel_result.file_path = job->file_path;
            cancel_result.status = ImportJobStatus::Cancelled;
            cancel_result.error_message = "Import cancelled";
            counts = advance_and_snapshot_counts_locked();
            completed_cb = on_completed_;
            counts_cb = on_counts_changed_;
        }
    }

    if (won_cancel) {
        if (completed_cb) {
            completed_cb(std::move(cancel_result));
        }
        if (counts_cb) {
            counts_cb(counts);
        }
    }

    return won_cancel;
}

void MediaImportQueue::cancel_all() {
    std::vector<ImportJobResult> cancelled_results;
    CompletionCallback completed_cb;
    CountsCallback counts_cb;
    ImportQueueCounts counts;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_queue_.clear();

        std::vector<std::shared_ptr<JobRecord>> jobs;
        jobs.reserve(jobs_by_id_.size());
        for (const auto& [id, job] : jobs_by_id_) {
            (void)id;
            jobs.push_back(job);
        }

        for (const auto& job : jobs) {
            job->cancel_state->store(true, std::memory_order_release);

            bool won = false;
            ImportJobStatus expected_queued = ImportJobStatus::Queued;
            if (job->status.compare_exchange_strong(expected_queued, ImportJobStatus::Cancelled, std::memory_order_acq_rel)) {
                won = true;
            } else {
                ImportJobStatus expected_running = ImportJobStatus::Running;
                if (job->status.compare_exchange_strong(expected_running, ImportJobStatus::Cancelled, std::memory_order_acq_rel)) {
                    won = true;
                }
            }

            if (won) {
                ImportJobResult res;
                res.job_id = job->job_id;
                res.file_path = job->file_path;
                res.status = ImportJobStatus::Cancelled;
                res.error_message = "Import cancelled";
                cancelled_results.push_back(std::move(res));
                jobs_by_id_.erase(job->job_id);
            }
        }

        if (!cancelled_results.empty()) {
            counts = advance_and_snapshot_counts_locked();
            completed_cb = on_completed_;
            counts_cb = on_counts_changed_;
        }
    }

    if (completed_cb) {
        for (auto& res : cancelled_results) {
            completed_cb(std::move(res));
        }
    }
    if (!cancelled_results.empty() && counts_cb) {
        counts_cb(counts);
    }
}

void MediaImportQueue::shutdown() {
    std::vector<std::thread> to_join;
    std::vector<ImportJobResult> cancelled_results;
    CompletionCallback completed_cb;
    CountsCallback counts_cb;
    ImportQueueCounts counts;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        shutdown_requested_ = true;
        pending_queue_.clear();

        std::vector<std::shared_ptr<JobRecord>> jobs;
        jobs.reserve(jobs_by_id_.size());
        for (const auto& [id, job] : jobs_by_id_) {
            (void)id;
            jobs.push_back(job);
        }

        for (const auto& job : jobs) {
            job->cancel_state->store(true, std::memory_order_release);
            ImportJobStatus expected_queued = ImportJobStatus::Queued;
            if (job->status.compare_exchange_strong(expected_queued, ImportJobStatus::Cancelled, std::memory_order_acq_rel)) {
                ImportJobResult res;
                res.job_id = job->job_id;
                res.file_path = job->file_path;
                res.status = ImportJobStatus::Cancelled;
                res.error_message = "Import cancelled during shutdown";
                cancelled_results.push_back(std::move(res));
                jobs_by_id_.erase(job->job_id);
            } else {
                ImportJobStatus expected_running = ImportJobStatus::Running;
                if (job->status.compare_exchange_strong(expected_running, ImportJobStatus::Cancelled, std::memory_order_acq_rel)) {
                    ImportJobResult res;
                    res.job_id = job->job_id;
                    res.file_path = job->file_path;
                    res.status = ImportJobStatus::Cancelled;
                    res.error_message = "Import cancelled during shutdown";
                    cancelled_results.push_back(std::move(res));
                    jobs_by_id_.erase(job->job_id);
                }
            }
        }

        if (!cancelled_results.empty()) {
            counts = advance_and_snapshot_counts_locked();
        }
        completed_cb = on_completed_;
        counts_cb = on_counts_changed_;
        to_join = std::move(workers_);
        cv_.notify_all();
    }

    if (completed_cb) {
        for (auto& res : cancelled_results) {
            completed_cb(std::move(res));
        }
    }
    if (!cancelled_results.empty() && counts_cb) {
        counts_cb(counts);
    }

    const auto current_tid = std::this_thread::get_id();
    bool self_is_worker = false;
    for (auto& worker : to_join) {
        if (worker.joinable()) {
            if (worker.get_id() == current_tid) {
                self_is_worker = true;
                worker.detach();
            } else {
                worker.join();
            }
        }
    }

    if (!self_is_worker) {
        std::unique_lock<std::mutex> lock(mutex_);
        workers_done_cv_.wait(lock, [this]() {
            return active_worker_threads_ == 0;
        });
    }
}

ImportQueueCounts MediaImportQueue::counts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_counts_locked();
}

bool MediaImportQueue::is_shutdown() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return shutdown_requested_;
}

ImportQueueCounts MediaImportQueue::snapshot_counts_locked() const noexcept {
    return ImportQueueCounts{active_count_, pending_queue_.size(), counts_seq_};
}

ImportQueueCounts MediaImportQueue::advance_and_snapshot_counts_locked() noexcept {
    ++counts_seq_;
    return ImportQueueCounts{active_count_, pending_queue_.size(), counts_seq_};
}

void MediaImportQueue::erase_pending_locked(const std::string& job_id) {
    auto it = std::find_if(pending_queue_.begin(), pending_queue_.end(),
        [&job_id](const std::shared_ptr<JobRecord>& item) {
            return item && item->job_id == job_id;
        });
    if (it != pending_queue_.end()) {
        pending_queue_.erase(it);
    }
}

void MediaImportQueue::worker_loop() {
    for (;;) {
        std::shared_ptr<JobRecord> job;
        CountsCallback start_counts_cb;
        ImportQueueCounts start_counts;
        WorkFn work;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() {
                return shutdown_requested_ || !pending_queue_.empty();
            });

            if (shutdown_requested_) {
                if (active_worker_threads_ > 0) {
                    --active_worker_threads_;
                }
                if (active_worker_threads_ == 0) {
                    workers_done_cv_.notify_all();
                }
                return;
            }

            if (pending_queue_.empty()) {
                continue;
            }

            job = std::move(pending_queue_.front());
            pending_queue_.pop_front();

            ImportJobStatus expected = ImportJobStatus::Queued;
            if (!job || !job->status.compare_exchange_strong(expected, ImportJobStatus::Running, std::memory_order_acq_rel)) {
                continue;
            }

            ++active_count_;
            start_counts = advance_and_snapshot_counts_locked();
            start_counts_cb = on_counts_changed_;
            work = work_fn_;
        }

        if (start_counts_cb) {
            start_counts_cb(start_counts);
        }

        WorkOutput output;
        if (!job->cancel_state->load(std::memory_order_acquire) && work) {
            try {
                output = work(job->file_path, job->cancel_state);
            } catch (const std::exception& ex) {
                output.terminal_status = ImportJobStatus::Failed;
                output.error_message = ex.what();
            } catch (...) {
                output.terminal_status = ImportJobStatus::Failed;
                output.error_message = "Unexpected exception during media import";
            }
        } else {
            output.terminal_status = ImportJobStatus::Cancelled;
            output.error_message = "Import cancelled";
        }

        ImportJobStatus desired_status = output.terminal_status;
        if (desired_status == ImportJobStatus::Queued || desired_status == ImportJobStatus::Running) {
            desired_status = ImportJobStatus::Failed;
            if (output.error_message.empty()) {
                output.error_message = "Invalid terminal import state";
            }
        }
        if (desired_status == ImportJobStatus::Succeeded && !output.asset.has_value()) {
            desired_status = ImportJobStatus::Failed;
            if (output.error_message.empty()) {
                output.error_message = "Import succeeded without decoded asset metadata";
            }
        }

        bool won_terminal = false;
        ImportJobResult terminal_result;
        CompletionCallback completed_cb;
        CountsCallback finish_counts_cb;
        ImportQueueCounts finish_counts;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            ImportJobStatus expected_running = ImportJobStatus::Running;
            if (job->status.compare_exchange_strong(expected_running, desired_status, std::memory_order_acq_rel)) {
                won_terminal = true;
                terminal_result.job_id = job->job_id;
                terminal_result.file_path = job->file_path;
                terminal_result.status = desired_status;
                terminal_result.error_message = std::move(output.error_message);
                if (desired_status == ImportJobStatus::Succeeded) {
                    terminal_result.asset = std::move(output.asset);
                    terminal_result.thumbnail = std::move(output.thumbnail);
                    terminal_result.waveform = std::move(output.waveform);
                }
            }

            jobs_by_id_.erase(job->job_id);
            if (active_count_ > 0) {
                --active_count_;
            }
            finish_counts = advance_and_snapshot_counts_locked();
            completed_cb = on_completed_;
            finish_counts_cb = on_counts_changed_;
        }

        // Discard output cleanly if cancellation won the CAS before commit.
        if (won_terminal && completed_cb) {
            completed_cb(std::move(terminal_result));
        }
        if (finish_counts_cb) {
            finish_counts_cb(finish_counts);
        }
    }
}

MediaImportQueue::WorkOutput MediaImportQueue::default_import_work(
    const std::filesystem::path& file_path,
    const std::shared_ptr<std::atomic<bool>>& cancel_state) {

    WorkOutput out;
    if (cancel_state && cancel_state->load(std::memory_order_acquire)) {
        out.terminal_status = ImportJobStatus::Cancelled;
        out.error_message = "Import cancelled";
        return out;
    }

    ProbeResult probe = FFmpegProbe::probe(file_path, cancel_state);
    if (probe.cancelled || (cancel_state && cancel_state->load(std::memory_order_acquire))) {
        out.terminal_status = ImportJobStatus::Cancelled;
        out.error_message = "Import cancelled during media probe";
        return out;
    }

    if (!probe.success || !probe.asset.has_value()) {
        out.terminal_status = ImportJobStatus::Failed;
        out.error_message = probe.error_message.empty() ? "Media probe failed" : std::move(probe.error_message);
        return out;
    }

    out.asset = std::move(probe.asset);

    if (out.asset->primary_video_stream() != nullptr) {
        auto thumb = ThumbnailExtractor::extract(file_path, Rational{0, 1}, 160, 90, cancel_state);
        if (cancel_state && cancel_state->load(std::memory_order_acquire)) {
            out.terminal_status = ImportJobStatus::Cancelled;
            out.error_message = "Import cancelled during thumbnail extraction";
            out.asset.reset();
            return out;
        }
        out.thumbnail = std::move(thumb);
    }

    if (out.asset->primary_audio_stream() != nullptr) {
        auto wave = WaveformExtractor::extract(file_path, 100, cancel_state);
        if (cancel_state && cancel_state->load(std::memory_order_acquire)) {
            out.terminal_status = ImportJobStatus::Cancelled;
            out.error_message = "Import cancelled during waveform extraction";
            out.asset.reset();
            out.thumbnail.reset();
            return out;
        }
        out.waveform = std::move(wave);
    }

    if (cancel_state && cancel_state->load(std::memory_order_acquire)) {
        out.terminal_status = ImportJobStatus::Cancelled;
        out.error_message = "Import cancelled";
        out.asset.reset();
        out.thumbnail.reset();
        out.waveform.reset();
        return out;
    }

    out.terminal_status = ImportJobStatus::Succeeded;
    return out;
}

} // namespace ave
