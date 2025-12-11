#pragma once

/**
 * @file task_queue.hpp
 * @brief Thread-safe priority queue for EDF scheduling
 */

#include "edf_scheduler.hpp"
#include <mutex>
#include <queue>
#include <vector>

namespace ipb::core {

/**
 * @brief Thread-safe priority queue optimized for EDF scheduling
 *
 * Uses a min-heap based on deadlines for O(log n) operations.
 * Thread-safe via fine-grained locking.
 */
class TaskQueue {
public:
    explicit TaskQueue(size_t max_size = 100000);
    ~TaskQueue() = default;

    // Non-copyable
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    /// Push a task into the queue
    bool push(ScheduledTask task);

    /// Pop the task with earliest deadline
    bool pop(ScheduledTask& task);

    /// Try to pop without blocking
    bool try_pop(ScheduledTask& task);

    /// Peek at the earliest deadline task without removing
    bool peek(ScheduledTask& task) const;

    /// Remove a task by ID
    bool remove(uint64_t task_id);

    /// Check if queue is empty
    bool empty() const;

    /// Get current size
    size_t size() const;

    /// Get maximum size
    size_t max_size() const noexcept { return max_size_; }

    /// Clear all tasks
    void clear();

    /// Get nearest deadline (or nullopt if empty)
    std::optional<common::Timestamp> nearest_deadline() const;

private:
    size_t max_size_;
    mutable std::mutex mutex_;

    // Priority queue with earliest deadline at top
    std::priority_queue<ScheduledTask,
                       std::vector<ScheduledTask>,
                       std::greater<ScheduledTask>> queue_;
};

} // namespace ipb::core
