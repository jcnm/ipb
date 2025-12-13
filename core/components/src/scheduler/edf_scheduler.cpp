#include "ipb/core/scheduler/edf_scheduler.hpp"

#include <ipb/common/debug.hpp>
#include <ipb/common/error.hpp>
#include <ipb/common/platform.hpp>

#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "ipb/core/scheduler/task_queue.hpp"

namespace ipb::core {

using namespace common::debug;

namespace {
constexpr std::string_view LOG_CAT = category::SCHEDULER;
}  // anonymous namespace

// ============================================================================
// EDFSchedulerImpl - Private Implementation
// ============================================================================

class EDFSchedulerImpl {
public:
    explicit EDFSchedulerImpl(const EDFSchedulerConfig& config)
        : config_(config), task_queue_(config.max_queue_size) {
        if (config_.worker_threads == 0) {
            config_.worker_threads = std::thread::hardware_concurrency();
        }
    }

    ~EDFSchedulerImpl() { stop_immediate(); }

    bool start() {
        IPB_SPAN_CAT("EDFScheduler::start", LOG_CAT);

        if (IPB_UNLIKELY(running_.exchange(true))) {
            IPB_LOG_WARN(LOG_CAT, "EDFScheduler already running");
            return false;  // Already running
        }

        stop_requested_.store(false);

        IPB_LOG_INFO(LOG_CAT,
                     "Starting EDFScheduler with " << config_.worker_threads << " workers");

        // Start worker threads
        for (size_t i = 0; i < config_.worker_threads; ++i) {
            workers_.emplace_back([this, i]() { worker_loop(i); });

            // Set CPU affinity if configured
            if (config_.cpu_affinity_start >= 0) {
                IPB_LOG_DEBUG(LOG_CAT, "Setting CPU affinity for worker "
                                           << i << " to CPU "
                                           << (config_.cpu_affinity_start + static_cast<int>(i)));
                common::rt::CPUAffinity::set_thread_affinity(
                    workers_.back().get_id(), config_.cpu_affinity_start + static_cast<int>(i));
            }

            // Set real-time priority if configured
            if (config_.enable_realtime) {
                IPB_LOG_DEBUG(LOG_CAT, "Setting real-time priority " << config_.realtime_priority
                                                                     << " for worker " << i);
                common::rt::ThreadPriority::set_realtime_priority(workers_.back().get_id(),
                                                                  config_.realtime_priority);
            }
        }

        // Start deadline checker thread
        IPB_LOG_DEBUG(LOG_CAT, "Starting deadline checker thread");
        deadline_checker_ = std::thread([this]() { deadline_check_loop(); });

        IPB_LOG_INFO(LOG_CAT, "EDFScheduler started successfully");
        return true;
    }

    void stop() {
        IPB_SPAN_CAT("EDFScheduler::stop", LOG_CAT);

        if (IPB_UNLIKELY(!running_.exchange(false))) {
            IPB_LOG_DEBUG(LOG_CAT, "EDFScheduler stop called but not running");
            return;
        }

        IPB_LOG_INFO(LOG_CAT, "Stopping EDFScheduler...");

        stop_requested_.store(true);
        task_cv_.notify_all();

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();

        if (deadline_checker_.joinable()) {
            deadline_checker_.join();
        }

        IPB_LOG_INFO(LOG_CAT, "EDFScheduler stopped");
    }

    void stop_immediate() {
        stop_requested_.store(true);
        running_.store(false);
        task_cv_.notify_all();

        // Cancel all pending tasks
        ScheduledTask task;
        while (task_queue_.pop(task)) {
            task.state = TaskState::CANCELLED;
            stats_.tasks_cancelled.fetch_add(1, std::memory_order_relaxed);

            if (task.completion_callback) {
                task.completion_callback(TaskState::CANCELLED, std::chrono::nanoseconds(0));
            }
        }

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();

        if (deadline_checker_.joinable()) {
            deadline_checker_.join();
        }
    }

    bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

    SubmitResult submit(ScheduledTask task) {
        SubmitResult result;

        if (IPB_UNLIKELY(!running_.load(std::memory_order_acquire))) {
            IPB_LOG_WARN(LOG_CAT, "Cannot submit task: scheduler not running");
            result.error_message = "Scheduler not running";
            return result;
        }

        task.id           = next_task_id_.fetch_add(1, std::memory_order_relaxed);
        task.arrival_time = common::Timestamp::now();
        task.state        = TaskState::PENDING;

        IPB_LOG_TRACE(LOG_CAT, "Submitting task id=" << task.id << " name=\"" << task.name << "\"");

        // Check if deadline already passed
        if (IPB_UNLIKELY(task.deadline <= task.arrival_time)) {
            task.state = TaskState::DEADLINE_MISSED;
            stats_.deadlines_missed.fetch_add(1, std::memory_order_relaxed);

            IPB_LOG_WARN(LOG_CAT, "Task " << task.id << " deadline already passed at submission");

            if (deadline_miss_callback_) {
                deadline_miss_callback_(task);
            }

            if (task.completion_callback) {
                task.completion_callback(TaskState::DEADLINE_MISSED, std::chrono::nanoseconds(0));
            }

            result.task_id       = task.id;
            result.error_message = "Deadline already passed";
            return result;
        }

        if (IPB_UNLIKELY(!task_queue_.push(std::move(task)))) {
            // Queue full - apply overflow policy
            IPB_LOG_WARN(LOG_CAT, "Task queue full (size=" << task_queue_.size() << ")");

            if (config_.overflow_policy == EDFSchedulerConfig::OverflowPolicy::REJECT) {
                result.error_message = "Queue full";
                return result;
            }
            // TODO: Implement DROP_LOWEST and DROP_FURTHEST policies
            result.error_message = "Queue full";
            return result;
        }

        stats_.tasks_submitted.fetch_add(1, std::memory_order_relaxed);

        auto queue_size = task_queue_.size();
        stats_.current_queue_size.store(queue_size, std::memory_order_relaxed);

        // Update peak
        auto peak = stats_.peak_queue_size.load(std::memory_order_relaxed);
        while (queue_size > peak) {
            stats_.peak_queue_size.compare_exchange_weak(peak, queue_size);
        }

        // Wake up a worker
        task_cv_.notify_one();

        result.success = true;
        result.task_id = task.id;
        IPB_LOG_TRACE(LOG_CAT, "Task " << task.id << " submitted successfully");
        return result;
    }

    SubmitResult submit(std::function<void()> func, common::Timestamp deadline) {
        ScheduledTask task;
        task.deadline      = deadline;
        task.task_function = std::move(func);
        return submit(std::move(task));
    }

    SubmitResult submit(std::function<void()> func, std::chrono::nanoseconds deadline_offset) {
        auto deadline = common::Timestamp::now() + deadline_offset;
        return submit(std::move(func), deadline);
    }

    SubmitResult submit(std::function<void()> func) {
        return submit(std::move(func), default_deadline_offset_.load(std::memory_order_relaxed));
    }

    SubmitResult submit_named(std::string name, std::function<void()> func,
                              common::Timestamp deadline) {
        ScheduledTask task;
        task.name          = std::move(name);
        task.deadline      = deadline;
        task.task_function = std::move(func);
        return submit(std::move(task));
    }

    SubmitResult submit_with_callback(
        std::function<void()> func, common::Timestamp deadline,
        std::function<void(TaskState, std::chrono::nanoseconds)> callback) {
        ScheduledTask task;
        task.deadline            = deadline;
        task.task_function       = std::move(func);
        task.completion_callback = std::move(callback);
        return submit(std::move(task));
    }

    uint64_t submit_periodic(std::function<void()> func, std::chrono::nanoseconds period,
                             TaskPriority priority) {
        uint64_t periodic_id = next_periodic_id_.fetch_add(1, std::memory_order_relaxed);

        {
            std::lock_guard lock(periodic_mutex_);
            periodic_tasks_[periodic_id] =
                PeriodicTask{periodic_id, std::move(func), period, priority, true};
        }

        // Submit first occurrence
        schedule_periodic_instance(periodic_id);

        return periodic_id;
    }

    bool cancel_periodic(uint64_t periodic_id) {
        std::lock_guard lock(periodic_mutex_);
        return periodic_tasks_.erase(periodic_id) > 0;
    }

    bool cancel(uint64_t task_id) {
        if (task_queue_.remove(task_id)) {
            stats_.tasks_cancelled.fetch_add(1, std::memory_order_relaxed);
            stats_.current_queue_size.store(task_queue_.size(), std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    std::optional<TaskState> get_task_state(uint64_t task_id) const {
        std::lock_guard lock(completed_mutex_);
        auto it = completed_states_.find(task_id);
        if (it != completed_states_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    size_t pending_count() const noexcept { return task_queue_.size(); }

    std::optional<common::Timestamp> nearest_deadline() const {
        return task_queue_.nearest_deadline();
    }

    void set_deadline_miss_callback(EDFScheduler::DeadlineMissCallback callback) {
        std::lock_guard lock(callback_mutex_);
        deadline_miss_callback_ = std::move(callback);
    }

    uint64_t missed_deadline_count() const noexcept {
        return stats_.deadlines_missed.load(std::memory_order_relaxed);
    }

    const EDFSchedulerStats& stats() const noexcept { return stats_; }

    void reset_stats() { stats_.reset(); }

    const EDFSchedulerConfig& config() const noexcept { return config_; }

    void set_default_deadline_offset(std::chrono::nanoseconds offset) {
        default_deadline_offset_.store(offset, std::memory_order_relaxed);
    }

    std::chrono::nanoseconds get_default_deadline_offset() const noexcept {
        return default_deadline_offset_.load(std::memory_order_relaxed);
    }

private:
    void worker_loop(size_t worker_id) {
        IPB_LOG_DEBUG(LOG_CAT, "Worker " << worker_id << " started");

        while (!stop_requested_.load(std::memory_order_acquire)) {
            ScheduledTask task;

            // Wait for task
            {
                std::unique_lock lock(task_mutex_);
                task_cv_.wait_for(lock, config_.check_interval, [this]() {
                    return stop_requested_.load(std::memory_order_acquire) || !task_queue_.empty();
                });
            }

            if (stop_requested_.load(std::memory_order_acquire)) {
                break;
            }

            // Try to get a task
            if (!task_queue_.try_pop(task)) {
                continue;
            }

            stats_.current_queue_size.store(task_queue_.size(), std::memory_order_relaxed);

            IPB_LOG_TRACE(LOG_CAT, "Worker " << worker_id << " executing task " << task.id);

            // Check deadline before execution
            auto now     = common::Timestamp::now();
            auto latency = now - task.arrival_time;

            if (IPB_UNLIKELY(now > task.deadline)) {
                // Deadline missed before execution started
                task.state = TaskState::DEADLINE_MISSED;
                stats_.deadlines_missed.fetch_add(1, std::memory_order_relaxed);

                IPB_LOG_WARN(LOG_CAT, "Task " << task.id << " missed deadline before execution");

                if (config_.enable_miss_callbacks && deadline_miss_callback_) {
                    std::lock_guard lock(callback_mutex_);
                    deadline_miss_callback_(task);
                }

                if (task.completion_callback) {
                    task.completion_callback(TaskState::DEADLINE_MISSED, latency);
                }

                record_completed(task.id, TaskState::DEADLINE_MISSED);
                continue;
            }

            // Execute task
            task.state = TaskState::RUNNING;
            common::rt::HighResolutionTimer exec_timer;

            try {
                task.task_function();
                task.state = TaskState::COMPLETED;
                stats_.tasks_completed.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::exception& e) {
                task.state = TaskState::FAILED;
                stats_.tasks_failed.fetch_add(1, std::memory_order_relaxed);
                IPB_LOG_ERROR(LOG_CAT,
                              "Task " << task.id << " failed with exception: " << e.what());
            } catch (...) {
                task.state = TaskState::FAILED;
                stats_.tasks_failed.fetch_add(1, std::memory_order_relaxed);
                IPB_LOG_ERROR(LOG_CAT, "Task " << task.id << " failed with unknown exception");
            }

            auto exec_time      = exec_timer.elapsed();
            task.execution_time = exec_time;

            // Check if deadline was met
            auto finish_time  = common::Timestamp::now();
            task.deadline_met = finish_time <= task.deadline;

            if (IPB_LIKELY(task.deadline_met)) {
                stats_.deadlines_met.fetch_add(1, std::memory_order_relaxed);
                IPB_LOG_TRACE(LOG_CAT, "Task " << task.id << " completed in "
                                               << exec_time.count() / 1000.0 << "us");
            } else {
                stats_.deadlines_missed.fetch_add(1, std::memory_order_relaxed);
                IPB_LOG_WARN(LOG_CAT, "Task " << task.id << " missed deadline during execution");

                if (config_.enable_miss_callbacks && deadline_miss_callback_) {
                    std::lock_guard lock(callback_mutex_);
                    deadline_miss_callback_(task);
                }
            }

            // Update timing stats
            if (config_.enable_timing) {
                update_latency_stats(latency.count());
                update_execution_stats(exec_time.count());
            }

            // Call completion callback
            if (task.completion_callback) {
                task.completion_callback(task.state, exec_time);
            }

            record_completed(task.id, task.state);
        }

        IPB_LOG_DEBUG(LOG_CAT, "Worker " << worker_id << " stopped");
    }

    void deadline_check_loop() {
        while (!stop_requested_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(config_.check_interval);

            // Check for imminent deadlines and wake workers if needed
            auto nearest = task_queue_.nearest_deadline();
            if (nearest) {
                auto now        = common::Timestamp::now();
                auto time_until = *nearest - now;

                if (time_until.count() < config_.check_interval.count() * 1000) {
                    // Deadline is soon - make sure workers are awake
                    task_cv_.notify_all();
                }
            }
        }
    }

    void schedule_periodic_instance(uint64_t periodic_id) {
        std::shared_lock lock(periodic_mutex_);

        auto it = periodic_tasks_.find(periodic_id);
        if (it == periodic_tasks_.end() || !it->second.active) {
            return;
        }

        auto& periodic = it->second;
        auto deadline  = common::Timestamp::now() + periodic.period;

        // Create task that reschedules itself
        ScheduledTask task;
        task.deadline      = deadline;
        task.priority      = periodic.priority;
        task.task_function = [this, periodic_id, func = periodic.task_function]() {
            func();
            schedule_periodic_instance(periodic_id);  // Reschedule
        };

        lock.unlock();
        submit(std::move(task));
    }

    void update_latency_stats(int64_t latency_ns) {
        stats_.total_latency_ns.fetch_add(latency_ns, std::memory_order_relaxed);

        int64_t current_min = stats_.min_latency_ns.load(std::memory_order_relaxed);
        while (latency_ns < current_min &&
               !stats_.min_latency_ns.compare_exchange_weak(current_min, latency_ns)) {}

        int64_t current_max = stats_.max_latency_ns.load(std::memory_order_relaxed);
        while (latency_ns > current_max &&
               !stats_.max_latency_ns.compare_exchange_weak(current_max, latency_ns)) {}
    }

    void update_execution_stats(int64_t exec_ns) {
        stats_.total_execution_ns.fetch_add(exec_ns, std::memory_order_relaxed);

        int64_t current_min = stats_.min_execution_ns.load(std::memory_order_relaxed);
        while (exec_ns < current_min &&
               !stats_.min_execution_ns.compare_exchange_weak(current_min, exec_ns)) {}

        int64_t current_max = stats_.max_execution_ns.load(std::memory_order_relaxed);
        while (exec_ns > current_max &&
               !stats_.max_execution_ns.compare_exchange_weak(current_max, exec_ns)) {}
    }

    void record_completed(uint64_t task_id, TaskState state) {
        std::lock_guard lock(completed_mutex_);

        // Keep limited history
        if (completed_states_.size() >= 10000) {
            completed_states_.clear();
        }

        completed_states_[task_id] = state;
    }

    EDFSchedulerConfig config_;
    EDFSchedulerStats stats_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    TaskQueue task_queue_;
    std::atomic<uint64_t> next_task_id_{1};

    std::vector<std::thread> workers_;
    std::mutex task_mutex_;
    std::condition_variable task_cv_;

    std::thread deadline_checker_;

    // Periodic tasks
    struct PeriodicTask {
        uint64_t id;
        std::function<void()> task_function;
        std::chrono::nanoseconds period;
        TaskPriority priority;
        bool active;
    };
    mutable std::shared_mutex periodic_mutex_;
    std::unordered_map<uint64_t, PeriodicTask> periodic_tasks_;
    std::atomic<uint64_t> next_periodic_id_{1};

    // Completed task states
    mutable std::mutex completed_mutex_;
    std::unordered_map<uint64_t, TaskState> completed_states_;

    // Callbacks
    std::mutex callback_mutex_;
    EDFScheduler::DeadlineMissCallback deadline_miss_callback_;

    std::atomic<std::chrono::nanoseconds> default_deadline_offset_{
        std::chrono::nanoseconds(1000000)};  // 1ms default
};

// ============================================================================
// EDFScheduler Public Interface
// ============================================================================

EDFScheduler::EDFScheduler() : impl_(std::make_unique<EDFSchedulerImpl>(EDFSchedulerConfig{})) {}

EDFScheduler::EDFScheduler(const EDFSchedulerConfig& config)
    : impl_(std::make_unique<EDFSchedulerImpl>(config)) {}

EDFScheduler::~EDFScheduler() = default;

EDFScheduler::EDFScheduler(EDFScheduler&&) noexcept            = default;
EDFScheduler& EDFScheduler::operator=(EDFScheduler&&) noexcept = default;

bool EDFScheduler::start() {
    return impl_->start();
}

void EDFScheduler::stop() {
    impl_->stop();
}

void EDFScheduler::stop_immediate() {
    impl_->stop_immediate();
}

bool EDFScheduler::is_running() const noexcept {
    return impl_->is_running();
}

SubmitResult EDFScheduler::submit(std::function<void()> task, common::Timestamp deadline) {
    return impl_->submit(std::move(task), deadline);
}

SubmitResult EDFScheduler::submit(std::function<void()> task,
                                  std::chrono::nanoseconds deadline_offset) {
    return impl_->submit(std::move(task), deadline_offset);
}

SubmitResult EDFScheduler::submit(std::function<void()> task) {
    return impl_->submit(std::move(task));
}

SubmitResult EDFScheduler::submit_named(std::string name, std::function<void()> task,
                                        common::Timestamp deadline) {
    return impl_->submit_named(std::move(name), std::move(task), deadline);
}

SubmitResult EDFScheduler::submit_with_callback(
    std::function<void()> task, common::Timestamp deadline,
    std::function<void(TaskState, std::chrono::nanoseconds)> callback) {
    return impl_->submit_with_callback(std::move(task), deadline, std::move(callback));
}

SubmitResult EDFScheduler::submit(ScheduledTask task) {
    return impl_->submit(std::move(task));
}

uint64_t EDFScheduler::submit_periodic(std::function<void()> task, std::chrono::nanoseconds period,
                                       TaskPriority priority) {
    return impl_->submit_periodic(std::move(task), period, priority);
}

bool EDFScheduler::cancel_periodic(uint64_t periodic_id) {
    return impl_->cancel_periodic(periodic_id);
}

bool EDFScheduler::cancel(uint64_t task_id) {
    return impl_->cancel(task_id);
}

std::optional<TaskState> EDFScheduler::get_task_state(uint64_t task_id) const {
    return impl_->get_task_state(task_id);
}

size_t EDFScheduler::pending_count() const noexcept {
    return impl_->pending_count();
}

std::optional<common::Timestamp> EDFScheduler::nearest_deadline() const {
    return impl_->nearest_deadline();
}

void EDFScheduler::set_deadline_miss_callback(DeadlineMissCallback callback) {
    impl_->set_deadline_miss_callback(std::move(callback));
}

uint64_t EDFScheduler::missed_deadline_count() const noexcept {
    return impl_->missed_deadline_count();
}

const EDFSchedulerStats& EDFScheduler::stats() const noexcept {
    return impl_->stats();
}

void EDFScheduler::reset_stats() {
    impl_->reset_stats();
}

const EDFSchedulerConfig& EDFScheduler::config() const noexcept {
    return impl_->config();
}

void EDFScheduler::set_default_deadline_offset(std::chrono::nanoseconds offset) {
    impl_->set_default_deadline_offset(offset);
}

std::chrono::nanoseconds EDFScheduler::get_default_deadline_offset() const noexcept {
    return impl_->get_default_deadline_offset();
}

}  // namespace ipb::core
