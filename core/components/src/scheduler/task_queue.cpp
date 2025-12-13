#include "ipb/core/scheduler/task_queue.hpp"

#include <algorithm>

namespace ipb::core {

TaskQueue::TaskQueue(size_t max_size) : max_size_(max_size) {}

bool TaskQueue::push(ScheduledTask task) {
    std::lock_guard lock(mutex_);

    if (queue_.size() >= max_size_) {
        return false;
    }

    queue_.push(std::move(task));
    return true;
}

bool TaskQueue::pop(ScheduledTask& task) {
    std::lock_guard lock(mutex_);

    if (queue_.empty()) {
        return false;
    }

    task = std::move(const_cast<ScheduledTask&>(queue_.top()));
    queue_.pop();
    return true;
}

bool TaskQueue::try_pop(ScheduledTask& task) {
    std::unique_lock lock(mutex_, std::try_to_lock);

    if (!lock.owns_lock() || queue_.empty()) {
        return false;
    }

    task = std::move(const_cast<ScheduledTask&>(queue_.top()));
    queue_.pop();
    return true;
}

bool TaskQueue::peek(ScheduledTask& task) const {
    std::lock_guard lock(mutex_);

    if (queue_.empty()) {
        return false;
    }

    task = queue_.top();
    return true;
}

bool TaskQueue::remove(uint64_t task_id) {
    std::lock_guard lock(mutex_);

    // Unfortunately, std::priority_queue doesn't support removal
    // We need to rebuild the queue without the target task
    std::vector<ScheduledTask> tasks;
    bool found = false;

    while (!queue_.empty()) {
        auto task = std::move(const_cast<ScheduledTask&>(queue_.top()));
        queue_.pop();

        if (task.id == task_id) {
            found = true;
        } else {
            tasks.push_back(std::move(task));
        }
    }

    // Rebuild queue
    for (auto& task : tasks) {
        queue_.push(std::move(task));
    }

    return found;
}

bool TaskQueue::empty() const {
    std::lock_guard lock(mutex_);
    return queue_.empty();
}

size_t TaskQueue::size() const {
    std::lock_guard lock(mutex_);
    return queue_.size();
}

void TaskQueue::clear() {
    std::lock_guard lock(mutex_);
    while (!queue_.empty()) {
        queue_.pop();
    }
}

std::optional<common::Timestamp> TaskQueue::nearest_deadline() const {
    std::lock_guard lock(mutex_);

    if (queue_.empty()) {
        return std::nullopt;
    }

    return queue_.top().deadline;
}

}  // namespace ipb::core
