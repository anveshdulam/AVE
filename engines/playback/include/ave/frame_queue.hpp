#pragma once

#include "ave/time.hpp"
#include "ave/video_frame.hpp"

#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>
#include <cstdint>

namespace ave {

/**
 * @brief Thread-safe bounded queue enforcing backpressure on upstream decoders.
 */
template <typename T>
class BoundedFrameQueue {
public:
    explicit BoundedFrameQueue(size_t capacity = 12)
        : capacity_(capacity > 0 ? capacity : 1) {}

    ~BoundedFrameQueue() {
        abort();
    }

    /**
     * @brief Pushes an item into the queue. Blocks if full until space is available or timeout occurs.
     * @return true if successfully pushed, false if timed out or aborted.
     */
    bool push(T item, std::chrono::milliseconds timeout = std::chrono::milliseconds(50)) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!not_full_cv_.wait_for(lock, timeout, [this] {
            return queue_.size() < capacity_ || aborted_;
        })) {
            return false;
        }

        if (aborted_) return false;

        queue_.push_back(std::move(item));
        not_empty_cv_.notify_one();
        return true;
    }

    /**
     * @brief Pops an item from the queue. Blocks if empty until an item is available or timeout occurs.
     */
    std::optional<T> pop(std::chrono::milliseconds timeout = std::chrono::milliseconds(50)) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!not_empty_cv_.wait_for(lock, timeout, [this] {
            return !queue_.empty() || aborted_;
        })) {
            return std::nullopt;
        }

        if (queue_.empty()) return std::nullopt;

        T item = std::move(queue_.front());
        queue_.pop_front();
        not_full_cv_.notify_one();
        return item;
    }

    /**
     * @brief Returns a copy of the front item without removing it, or nullopt if empty.
     */
    std::optional<T> peek() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        return queue_.front();
    }

    /**
     * @brief Clears all pending items and notifies any waiting producers.
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        not_full_cv_.notify_all();
    }

    void abort() {
        std::lock_guard<std::mutex> lock(mutex_);
        aborted_ = true;
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();
    }

    void reset_abort() {
        std::lock_guard<std::mutex> lock(mutex_);
        aborted_ = false;
    }

    [[nodiscard]] size_t size() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    [[nodiscard]] size_t capacity() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return capacity_;
    }

    void set_capacity(size_t cap) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        capacity_ = (cap > 0 ? cap : 1);
        not_full_cv_.notify_all();
    }

    [[nodiscard]] bool is_empty() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    [[nodiscard]] bool is_full() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size() >= capacity_;
    }

private:
    mutable std::mutex mutex_{};
    std::condition_variable not_empty_cv_{};
    std::condition_variable not_full_cv_{};
    std::deque<T> queue_{};
    size_t capacity_{12};
    bool aborted_{false};
};

} // namespace ave

