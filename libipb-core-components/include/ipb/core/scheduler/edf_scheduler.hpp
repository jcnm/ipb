#pragma once

/**
 * @file edf_scheduler.hpp
 * @brief Earliest Deadline First (EDF) scheduler for real-time task execution
 *
 * The EDFScheduler provides deterministic task scheduling based on deadlines:
 * - EDF algorithm guarantees optimal scheduling for periodic tasks
 * - Lock-free task submission
 * - Deadline miss detection and reporting
 * - Priority-based fallback for equal deadlines
 *
 * Target: <1us scheduling overhead, 100% deadline compliance under load
 */

#include <ipb/common/data_point.hpp>
#include <ipb/common/endpoint.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ipb::core {

// Forward declarations
class EDFSchedulerImpl;
class TaskQueue;

/**
 * @brief Task priority for tie-breaking when deadlines are equal
 */
enum class TaskPriority : uint8_t {
    BACKGROUND = 0,
    LOW = 64,
    NORMAL = 128,
    HIGH = 192,
    CRITICAL = 255
};

/**
 * @brief Task state in the scheduler
 */
enum class TaskState : uint8_t {
    PENDING,    ///< Waiting to be executed
    RUNNING,    ///< Currently executing
    COMPLETED,  ///< Successfully completed
    CANCELLED,  ///< Cancelled before execution
    FAILED,     ///< Execution failed
    DEADLINE_MISSED  ///< Deadline passed before execution started
};

/**
 * @brief Task definition for EDF scheduler
 */
struct ScheduledTask {
    /// Unique task identifier
    uint64_t id = 0;

    /// Human-readable task name (optional)
    std::string name;

    /// Absolute deadline (nanoseconds since epoch)
    common::Timestamp deadline;

    /// Task arrival time
    common::Timestamp arrival_time;

    /// Priority for tie-breaking
    TaskPriority priority = TaskPriority::NORMAL;

    /// The task function to execute
    std::function<void()> task_function;

    /// Optional callback when task completes
    std::function<void(TaskState, std::chrono::nanoseconds)> completion_callback;

    /// Current state
    TaskState state = TaskState::PENDING;

    /// Execution duration (set after completion)
    std::chrono::nanoseconds execution_time{0};

    /// Whether deadline was met
    bool deadline_met = false;

    // Comparison for priority queue (earliest deadline first)
    bool operator>(const ScheduledTask& other) const {
        if (deadline == other.deadline) {
            return static_cast<uint8_t>(priority) < static_cast<uint8_t>(other.priority);
        }
        return deadline > other.deadline;
    }
};

/**
 * @brief Result of task submission
 */
struct SubmitResult {
    bool success = false;
    uint64_t task_id = 0;
    std::string error_message;

    explicit operator bool() const noexcept { return success; }
};

/**
 * @brief Statistics for scheduler monitoring
 */
struct EDFSchedulerStats {
    std::atomic<uint64_t> tasks_submitted{0};
    std::atomic<uint64_t> tasks_completed{0};
    std::atomic<uint64_t> tasks_cancelled{0};
    std::atomic<uint64_t> tasks_failed{0};
    std::atomic<uint64_t> deadlines_met{0};
    std::atomic<uint64_t> deadlines_missed{0};

    std::atomic<uint64_t> current_queue_size{0};
    std::atomic<uint64_t> peak_queue_size{0};

    std::atomic<int64_t> min_latency_ns{INT64_MAX};  ///< Submission to start
    std::atomic<int64_t> max_latency_ns{0};
    std::atomic<int64_t> total_latency_ns{0};

    std::atomic<int64_t> min_execution_ns{INT64_MAX};
    std::atomic<int64_t> max_execution_ns{0};
    std::atomic<int64_t> total_execution_ns{0};

    /// Calculate deadline compliance rate
    double deadline_compliance_rate() const noexcept {
        auto total = deadlines_met.load() + deadlines_missed.load();
        return total > 0 ?
            static_cast<double>(deadlines_met) / total * 100.0 : 100.0;
    }

    /// Calculate average latency in microseconds
    double avg_latency_us() const noexcept {
        auto count = tasks_completed.load() + tasks_failed.load();
        return count > 0 ?
            static_cast<double>(total_latency_ns) / count / 1000.0 : 0.0;
    }

    /// Calculate average execution time in microseconds
    double avg_execution_us() const noexcept {
        auto count = tasks_completed.load();
        return count > 0 ?
            static_cast<double>(total_execution_ns) / count / 1000.0 : 0.0;
    }

    void reset() noexcept {
        tasks_submitted.store(0);
        tasks_completed.store(0);
        tasks_cancelled.store(0);
        tasks_failed.store(0);
        deadlines_met.store(0);
        deadlines_missed.store(0);
        current_queue_size.store(0);
        peak_queue_size.store(0);
        min_latency_ns.store(INT64_MAX);
        max_latency_ns.store(0);
        total_latency_ns.store(0);
        min_execution_ns.store(INT64_MAX);
        max_execution_ns.store(0);
        total_execution_ns.store(0);
    }
};

/**
 * @brief Configuration for EDFScheduler
 */
struct EDFSchedulerConfig {
    /// Maximum queue size
    size_t max_queue_size = 100000;

    /// Number of worker threads (0 = hardware concurrency)
    size_t worker_threads = 0;

    /// Default deadline offset from now (for tasks without explicit deadline)
    std::chrono::nanoseconds default_deadline_offset{1000000};  // 1ms

    /// Enable real-time scheduling for workers
    bool enable_realtime = false;

    /// Real-time priority (1-99, higher = more priority)
    int realtime_priority = 50;

    /// CPU affinity for workers (-1 = no affinity, else starting CPU)
    int cpu_affinity_start = -1;

    /// Deadline check interval
    std::chrono::microseconds check_interval{100};

    /// Action when queue is full
    enum class OverflowPolicy {
        REJECT,         ///< Reject new tasks
        DROP_LOWEST,    ///< Drop lowest priority task
        DROP_FURTHEST   ///< Drop task with furthest deadline
    } overflow_policy = OverflowPolicy::REJECT;

    /// Enable deadline miss callbacks
    bool enable_miss_callbacks = true;

    /// Enable execution timing
    bool enable_timing = true;
};

/**
 * @brief Earliest Deadline First scheduler for real-time task execution
 *
 * Features:
 * - O(log n) task insertion and extraction
 * - Lock-free task submission (in most cases)
 * - Deadline miss detection
 * - Priority-based tie-breaking
 * - Real-time thread support
 *
 * Example usage:
 * @code
 * EDFScheduler scheduler;
 * scheduler.start();
 *
 * // Submit a task with deadline
 * auto deadline = Timestamp::now() + std::chrono::milliseconds(10);
 * auto result = scheduler.submit([](){ process_data(); }, deadline);
 *
 * // Submit periodic task
 * scheduler.submit_periodic([](){ read_sensor(); },
 *                          std::chrono::milliseconds(100));
 * @endcode
 */
class EDFScheduler {
public:
    /// Callback for deadline misses
    using DeadlineMissCallback = std::function<void(const ScheduledTask&)>;

    EDFScheduler();
    explicit EDFScheduler(const EDFSchedulerConfig& config);
    ~EDFScheduler();

    // Non-copyable, movable
    EDFScheduler(const EDFScheduler&) = delete;
    EDFScheduler& operator=(const EDFScheduler&) = delete;
    EDFScheduler(EDFScheduler&&) noexcept;
    EDFScheduler& operator=(EDFScheduler&&) noexcept;

    // Lifecycle

    /// Start the scheduler
    bool start();

    /// Stop the scheduler (waits for current tasks)
    void stop();

    /// Stop immediately (cancels pending tasks)
    void stop_immediate();

    /// Check if scheduler is running
    bool is_running() const noexcept;

    // Task Submission

    /// Submit a task with explicit deadline
    SubmitResult submit(std::function<void()> task, common::Timestamp deadline);

    /// Submit a task with deadline offset from now
    SubmitResult submit(std::function<void()> task,
                       std::chrono::nanoseconds deadline_offset);

    /// Submit a task with default deadline
    SubmitResult submit(std::function<void()> task);

    /// Submit a named task
    SubmitResult submit_named(std::string name,
                             std::function<void()> task,
                             common::Timestamp deadline);

    /// Submit a task with completion callback
    SubmitResult submit_with_callback(
        std::function<void()> task,
        common::Timestamp deadline,
        std::function<void(TaskState, std::chrono::nanoseconds)> callback);

    /// Submit a full task structure
    SubmitResult submit(ScheduledTask task);

    // Periodic Tasks

    /// Submit a periodic task
    uint64_t submit_periodic(std::function<void()> task,
                            std::chrono::nanoseconds period,
                            TaskPriority priority = TaskPriority::NORMAL);

    /// Cancel a periodic task
    bool cancel_periodic(uint64_t periodic_id);

    // Task Management

    /// Cancel a pending task
    bool cancel(uint64_t task_id);

    /// Get task state
    std::optional<TaskState> get_task_state(uint64_t task_id) const;

    /// Get number of pending tasks
    size_t pending_count() const noexcept;

    /// Get nearest deadline
    std::optional<common::Timestamp> nearest_deadline() const;

    // Deadline Miss Handling

    /// Set callback for deadline misses
    void set_deadline_miss_callback(DeadlineMissCallback callback);

    /// Get number of missed deadlines
    uint64_t missed_deadline_count() const noexcept;

    // Statistics

    /// Get current statistics
    const EDFSchedulerStats& stats() const noexcept;

    /// Reset statistics
    void reset_stats();

    // Configuration

    /// Get current configuration
    const EDFSchedulerConfig& config() const noexcept;

    /// Update default deadline offset (thread-safe)
    void set_default_deadline_offset(std::chrono::nanoseconds offset);

private:
    std::unique_ptr<EDFSchedulerImpl> impl_;
};

} // namespace ipb::core
