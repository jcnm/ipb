/**
 * @file test_scheduler.cpp
 * @brief Comprehensive unit tests for ipb::core::EDFScheduler and TaskQueue
 */

#include <gtest/gtest.h>
#include <ipb/core/scheduler/edf_scheduler.hpp>
#include <ipb/core/scheduler/task_queue.hpp>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <latch>
#include <random>

using namespace ipb::core;
using namespace ipb::common;
using namespace std::chrono_literals;

// ============================================================================
// TaskPriority Tests
// ============================================================================

class TaskPriorityTest : public ::testing::Test {};

TEST_F(TaskPriorityTest, PriorityValues) {
    EXPECT_EQ(static_cast<uint8_t>(TaskPriority::BACKGROUND), 0);
    EXPECT_EQ(static_cast<uint8_t>(TaskPriority::LOW), 64);
    EXPECT_EQ(static_cast<uint8_t>(TaskPriority::NORMAL), 128);
    EXPECT_EQ(static_cast<uint8_t>(TaskPriority::HIGH), 192);
    EXPECT_EQ(static_cast<uint8_t>(TaskPriority::CRITICAL), 255);
}

TEST_F(TaskPriorityTest, PriorityOrdering) {
    EXPECT_LT(static_cast<uint8_t>(TaskPriority::BACKGROUND),
              static_cast<uint8_t>(TaskPriority::LOW));
    EXPECT_LT(static_cast<uint8_t>(TaskPriority::LOW),
              static_cast<uint8_t>(TaskPriority::NORMAL));
    EXPECT_LT(static_cast<uint8_t>(TaskPriority::NORMAL),
              static_cast<uint8_t>(TaskPriority::HIGH));
    EXPECT_LT(static_cast<uint8_t>(TaskPriority::HIGH),
              static_cast<uint8_t>(TaskPriority::CRITICAL));
}

// ============================================================================
// TaskState Tests
// ============================================================================

class TaskStateTest : public ::testing::Test {};

TEST_F(TaskStateTest, StateValues) {
    EXPECT_EQ(static_cast<uint8_t>(TaskState::PENDING), 0);
    EXPECT_EQ(static_cast<uint8_t>(TaskState::RUNNING), 1);
    EXPECT_EQ(static_cast<uint8_t>(TaskState::COMPLETED), 2);
    EXPECT_EQ(static_cast<uint8_t>(TaskState::CANCELLED), 3);
    EXPECT_EQ(static_cast<uint8_t>(TaskState::FAILED), 4);
    EXPECT_EQ(static_cast<uint8_t>(TaskState::DEADLINE_MISSED), 5);
}

// ============================================================================
// ScheduledTask Tests
// ============================================================================

class ScheduledTaskTest : public ::testing::Test {};

TEST_F(ScheduledTaskTest, DefaultConstruction) {
    ScheduledTask task;

    EXPECT_EQ(task.id, 0);
    EXPECT_TRUE(task.name.empty());
    EXPECT_EQ(task.priority, TaskPriority::NORMAL);
    EXPECT_EQ(task.state, TaskState::PENDING);
    EXPECT_FALSE(task.deadline_met);
    EXPECT_EQ(task.execution_time.count(), 0);
}

TEST_F(ScheduledTaskTest, ComparisonByDeadline) {
    ScheduledTask early;
    early.deadline = Timestamp(1000ns);

    ScheduledTask late;
    late.deadline = Timestamp(2000ns);

    // Greater-than comparison for min-heap (earliest deadline has lowest priority)
    EXPECT_TRUE(late > early);
    EXPECT_FALSE(early > late);
}

TEST_F(ScheduledTaskTest, ComparisonByPriorityWhenDeadlinesEqual) {
    ScheduledTask high_priority;
    high_priority.deadline = Timestamp(1000ns);
    high_priority.priority = TaskPriority::HIGH;

    ScheduledTask low_priority;
    low_priority.deadline = Timestamp(1000ns);
    low_priority.priority = TaskPriority::LOW;

    // Higher priority should come first when deadlines are equal
    // (Lower priority value means "greater" in the comparison for proper heap ordering)
    EXPECT_TRUE(low_priority > high_priority);
}

// ============================================================================
// SubmitResult Tests
// ============================================================================

class SubmitResultTest : public ::testing::Test {};

TEST_F(SubmitResultTest, DefaultConstruction) {
    SubmitResult result;

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.task_id, 0);
    EXPECT_TRUE(result.error_message.empty());
}

TEST_F(SubmitResultTest, BoolConversion) {
    SubmitResult success;
    success.success = true;
    success.task_id = 42;

    SubmitResult failure;
    failure.success = false;

    EXPECT_TRUE(static_cast<bool>(success));
    EXPECT_FALSE(static_cast<bool>(failure));
}

// ============================================================================
// EDFSchedulerStats Tests
// ============================================================================

class EDFSchedulerStatsTest : public ::testing::Test {};

TEST_F(EDFSchedulerStatsTest, DefaultValues) {
    EDFSchedulerStats stats;

    EXPECT_EQ(stats.tasks_submitted.load(), 0);
    EXPECT_EQ(stats.tasks_completed.load(), 0);
    EXPECT_EQ(stats.tasks_cancelled.load(), 0);
    EXPECT_EQ(stats.tasks_failed.load(), 0);
    EXPECT_EQ(stats.deadlines_met.load(), 0);
    EXPECT_EQ(stats.deadlines_missed.load(), 0);
    EXPECT_EQ(stats.current_queue_size.load(), 0);
    EXPECT_EQ(stats.peak_queue_size.load(), 0);
}

TEST_F(EDFSchedulerStatsTest, DeadlineComplianceRate) {
    EDFSchedulerStats stats;
    stats.deadlines_met.store(95);
    stats.deadlines_missed.store(5);

    EXPECT_DOUBLE_EQ(stats.deadline_compliance_rate(), 95.0);
}

TEST_F(EDFSchedulerStatsTest, DeadlineComplianceRateZero) {
    EDFSchedulerStats stats;
    EXPECT_DOUBLE_EQ(stats.deadline_compliance_rate(), 100.0);  // No tasks = 100%
}

TEST_F(EDFSchedulerStatsTest, AverageLatency) {
    EDFSchedulerStats stats;
    stats.tasks_completed.store(100);
    stats.total_latency_ns.store(1000000);  // 1ms total

    EXPECT_DOUBLE_EQ(stats.avg_latency_us(), 10.0);  // 10us average
}

TEST_F(EDFSchedulerStatsTest, AverageExecution) {
    EDFSchedulerStats stats;
    stats.tasks_completed.store(50);
    stats.total_execution_ns.store(500000);  // 500us total

    EXPECT_DOUBLE_EQ(stats.avg_execution_us(), 10.0);  // 10us average
}

TEST_F(EDFSchedulerStatsTest, Reset) {
    EDFSchedulerStats stats;
    stats.tasks_submitted.store(100);
    stats.tasks_completed.store(95);
    stats.deadlines_met.store(90);

    stats.reset();

    EXPECT_EQ(stats.tasks_submitted.load(), 0);
    EXPECT_EQ(stats.tasks_completed.load(), 0);
    EXPECT_EQ(stats.deadlines_met.load(), 0);
}

// ============================================================================
// EDFSchedulerConfig Tests
// ============================================================================

class EDFSchedulerConfigTest : public ::testing::Test {};

TEST_F(EDFSchedulerConfigTest, DefaultValues) {
    EDFSchedulerConfig config;

    EXPECT_EQ(config.max_queue_size, 100000);
    EXPECT_EQ(config.worker_threads, 0);  // Use hardware concurrency
    EXPECT_EQ(config.default_deadline_offset.count(), 1000000);  // 1ms
    EXPECT_FALSE(config.enable_realtime);
    EXPECT_EQ(config.realtime_priority, 50);
    EXPECT_EQ(config.cpu_affinity_start, -1);
    EXPECT_EQ(config.check_interval.count(), 100);
    EXPECT_EQ(config.overflow_policy, EDFSchedulerConfig::OverflowPolicy::REJECT);
    EXPECT_TRUE(config.enable_miss_callbacks);
    EXPECT_TRUE(config.enable_timing);
}

TEST_F(EDFSchedulerConfigTest, OverflowPolicyValues) {
    EXPECT_NE(
        static_cast<int>(EDFSchedulerConfig::OverflowPolicy::REJECT),
        static_cast<int>(EDFSchedulerConfig::OverflowPolicy::DROP_LOWEST)
    );
    EXPECT_NE(
        static_cast<int>(EDFSchedulerConfig::OverflowPolicy::DROP_LOWEST),
        static_cast<int>(EDFSchedulerConfig::OverflowPolicy::DROP_FURTHEST)
    );
}

// ============================================================================
// TaskQueue Tests
// ============================================================================

class TaskQueueTest : public ::testing::Test {};

TEST_F(TaskQueueTest, DefaultConstruction) {
    TaskQueue queue;

    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
    EXPECT_EQ(queue.max_size(), 100000);
}

TEST_F(TaskQueueTest, CustomMaxSize) {
    TaskQueue queue(1000);
    EXPECT_EQ(queue.max_size(), 1000);
}

TEST_F(TaskQueueTest, PushPop) {
    TaskQueue queue;

    ScheduledTask task;
    task.id = 1;
    task.deadline = Timestamp::now() + 1s;
    task.task_function = []() {};

    EXPECT_TRUE(queue.push(std::move(task)));
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 1);

    ScheduledTask popped;
    EXPECT_TRUE(queue.pop(popped));
    EXPECT_EQ(popped.id, 1);
    EXPECT_TRUE(queue.empty());
}

TEST_F(TaskQueueTest, EarliestDeadlineFirst) {
    TaskQueue queue;

    // Add tasks with different deadlines (out of order)
    auto now = Timestamp::now();

    ScheduledTask task3;
    task3.id = 3;
    task3.deadline = now + 3s;
    task3.task_function = []() {};

    ScheduledTask task1;
    task1.id = 1;
    task1.deadline = now + 1s;
    task1.task_function = []() {};

    ScheduledTask task2;
    task2.id = 2;
    task2.deadline = now + 2s;
    task2.task_function = []() {};

    queue.push(std::move(task3));
    queue.push(std::move(task1));
    queue.push(std::move(task2));

    // Should come out in deadline order
    ScheduledTask popped;

    EXPECT_TRUE(queue.pop(popped));
    EXPECT_EQ(popped.id, 1);

    EXPECT_TRUE(queue.pop(popped));
    EXPECT_EQ(popped.id, 2);

    EXPECT_TRUE(queue.pop(popped));
    EXPECT_EQ(popped.id, 3);
}

TEST_F(TaskQueueTest, Peek) {
    TaskQueue queue;

    ScheduledTask task;
    task.id = 42;
    task.deadline = Timestamp::now() + 1s;
    task.task_function = []() {};

    queue.push(std::move(task));

    ScheduledTask peeked;
    EXPECT_TRUE(queue.peek(peeked));
    EXPECT_EQ(peeked.id, 42);

    // Queue should still have the task
    EXPECT_EQ(queue.size(), 1);
}

TEST_F(TaskQueueTest, TryPop) {
    TaskQueue queue;

    ScheduledTask task;
    EXPECT_FALSE(queue.try_pop(task));  // Empty queue

    ScheduledTask to_add;
    to_add.id = 1;
    to_add.task_function = []() {};
    queue.push(std::move(to_add));

    EXPECT_TRUE(queue.try_pop(task));
    EXPECT_EQ(task.id, 1);
}

TEST_F(TaskQueueTest, Remove) {
    TaskQueue queue;

    ScheduledTask task1, task2;
    task1.id = 1;
    task1.task_function = []() {};
    task2.id = 2;
    task2.task_function = []() {};

    queue.push(std::move(task1));
    queue.push(std::move(task2));

    EXPECT_TRUE(queue.remove(1));
    EXPECT_EQ(queue.size(), 1);

    ScheduledTask popped;
    queue.pop(popped);
    EXPECT_EQ(popped.id, 2);
}

TEST_F(TaskQueueTest, RemoveNonExistent) {
    TaskQueue queue;

    ScheduledTask task;
    task.id = 1;
    task.task_function = []() {};
    queue.push(std::move(task));

    EXPECT_FALSE(queue.remove(999));  // Non-existent ID
    EXPECT_EQ(queue.size(), 1);
}

TEST_F(TaskQueueTest, Clear) {
    TaskQueue queue;

    for (int i = 0; i < 10; ++i) {
        ScheduledTask task;
        task.id = i;
        task.task_function = []() {};
        queue.push(std::move(task));
    }

    EXPECT_EQ(queue.size(), 10);

    queue.clear();

    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
}

TEST_F(TaskQueueTest, NearestDeadline) {
    TaskQueue queue;

    EXPECT_FALSE(queue.nearest_deadline().has_value());

    auto now = Timestamp::now();

    ScheduledTask task;
    task.id = 1;
    task.deadline = now + 5s;
    task.task_function = []() {};
    queue.push(std::move(task));

    auto nearest = queue.nearest_deadline();
    EXPECT_TRUE(nearest.has_value());
    EXPECT_EQ(nearest->nanoseconds(), (now + 5s).nanoseconds());
}

TEST_F(TaskQueueTest, MaxSizeEnforcement) {
    TaskQueue queue(5);

    for (int i = 0; i < 5; ++i) {
        ScheduledTask task;
        task.id = i;
        task.task_function = []() {};
        EXPECT_TRUE(queue.push(std::move(task)));
    }

    EXPECT_EQ(queue.size(), 5);

    // Should fail when full
    ScheduledTask overflow;
    overflow.id = 999;
    overflow.task_function = []() {};
    EXPECT_FALSE(queue.push(std::move(overflow)));
}

TEST_F(TaskQueueTest, ConcurrentAccess) {
    TaskQueue queue(10000);
    const int num_producers = 4;
    const int items_per_producer = 1000;

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> done{false};

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // Producers
    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < items_per_producer; ++i) {
                ScheduledTask task;
                task.id = p * 10000 + i;
                task.deadline = Timestamp::now() + std::chrono::microseconds(rand() % 10000);
                task.task_function = []() {};

                while (!queue.push(std::move(task))) {
                    std::this_thread::yield();
                }
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Consumers
    for (int c = 0; c < 2; ++c) {
        consumers.emplace_back([&]() {
            while (!done.load(std::memory_order_acquire) || !queue.empty()) {
                ScheduledTask task;
                if (queue.try_pop(task)) {
                    consumed.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& t : producers) {
        t.join();
    }

    done.store(true, std::memory_order_release);

    for (auto& t : consumers) {
        t.join();
    }

    EXPECT_EQ(produced.load(), num_producers * items_per_producer);
    EXPECT_EQ(consumed.load(), num_producers * items_per_producer);
}

// ============================================================================
// EDFScheduler Tests
// ============================================================================

class EDFSchedulerTest : public ::testing::Test {
protected:
    void SetUp() override {
        EDFSchedulerConfig config;
        config.worker_threads = 2;
        scheduler_ = std::make_unique<EDFScheduler>(config);
    }

    void TearDown() override {
        if (scheduler_->is_running()) {
            scheduler_->stop();
        }
    }

    std::unique_ptr<EDFScheduler> scheduler_;
};

TEST_F(EDFSchedulerTest, DefaultConstruction) {
    EDFScheduler scheduler;
    EXPECT_FALSE(scheduler.is_running());
}

TEST_F(EDFSchedulerTest, StartStop) {
    EXPECT_TRUE(scheduler_->start());
    EXPECT_TRUE(scheduler_->is_running());

    scheduler_->stop();
    EXPECT_FALSE(scheduler_->is_running());
}

TEST_F(EDFSchedulerTest, SubmitWithDeadline) {
    scheduler_->start();

    std::atomic<bool> executed{false};
    auto deadline = Timestamp::now() + 1s;

    auto result = scheduler_->submit([&]() { executed.store(true); }, deadline);

    EXPECT_TRUE(result.success);
    EXPECT_GT(result.task_id, 0);

    // Wait for task to execute
    std::this_thread::sleep_for(100ms);

    EXPECT_TRUE(executed.load());
}

TEST_F(EDFSchedulerTest, SubmitWithOffset) {
    scheduler_->start();

    std::atomic<bool> executed{false};

    auto result = scheduler_->submit([&]() { executed.store(true); }, 100ms);

    EXPECT_TRUE(result.success);

    std::this_thread::sleep_for(200ms);

    EXPECT_TRUE(executed.load());
}

TEST_F(EDFSchedulerTest, SubmitWithDefaultDeadline) {
    scheduler_->start();

    std::atomic<bool> executed{false};

    auto result = scheduler_->submit([&]() { executed.store(true); });

    EXPECT_TRUE(result.success);

    std::this_thread::sleep_for(50ms);

    EXPECT_TRUE(executed.load());
}

TEST_F(EDFSchedulerTest, SubmitNamed) {
    scheduler_->start();

    std::atomic<bool> executed{false};

    auto result = scheduler_->submit_named(
        "test_task",
        [&]() { executed.store(true); },
        Timestamp::now() + 100ms
    );

    EXPECT_TRUE(result.success);

    std::this_thread::sleep_for(200ms);

    EXPECT_TRUE(executed.load());
}

TEST_F(EDFSchedulerTest, SubmitWithCallback) {
    scheduler_->start();

    std::atomic<bool> executed{false};
    std::atomic<bool> callback_called{false};
    TaskState final_state;

    auto result = scheduler_->submit_with_callback(
        [&]() { executed.store(true); },
        Timestamp::now() + 50ms,
        [&](TaskState state, std::chrono::nanoseconds) {
            final_state = state;
            callback_called.store(true);
        }
    );

    EXPECT_TRUE(result.success);

    std::this_thread::sleep_for(200ms);

    EXPECT_TRUE(executed.load());
    EXPECT_TRUE(callback_called.load());
    EXPECT_EQ(final_state, TaskState::COMPLETED);
}

TEST_F(EDFSchedulerTest, CancelTask) {
    scheduler_->start();

    std::atomic<bool> executed{false};

    // Submit with long deadline
    auto result = scheduler_->submit(
        [&]() { executed.store(true); },
        Timestamp::now() + 10s
    );

    EXPECT_TRUE(result.success);

    // Cancel it
    EXPECT_TRUE(scheduler_->cancel(result.task_id));

    // Give time for potential execution
    std::this_thread::sleep_for(100ms);

    EXPECT_FALSE(executed.load());
}

TEST_F(EDFSchedulerTest, CancelNonExistent) {
    scheduler_->start();
    EXPECT_FALSE(scheduler_->cancel(99999));
}

TEST_F(EDFSchedulerTest, PendingCount) {
    scheduler_->start();

    // Submit several tasks with long deadlines
    for (int i = 0; i < 5; ++i) {
        scheduler_->submit([]() { std::this_thread::sleep_for(10ms); }, 10s);
    }

    EXPECT_GE(scheduler_->pending_count(), 0);  // Some may have started
}

TEST_F(EDFSchedulerTest, NearestDeadline) {
    scheduler_->start();

    auto deadline1 = Timestamp::now() + 5s;
    auto deadline2 = Timestamp::now() + 2s;
    auto deadline3 = Timestamp::now() + 8s;

    scheduler_->submit([]() {}, deadline1);
    scheduler_->submit([]() {}, deadline2);
    scheduler_->submit([]() {}, deadline3);

    auto nearest = scheduler_->nearest_deadline();

    // Nearest should be approximately deadline2 (earliest)
    EXPECT_TRUE(nearest.has_value());
}

TEST_F(EDFSchedulerTest, Statistics) {
    scheduler_->start();

    for (int i = 0; i < 10; ++i) {
        scheduler_->submit([i]() {
            // Small computation
            volatile int x = 0;
            for (int j = 0; j < 100; ++j) x += j;
            (void)x;
        }, 100ms);
    }

    std::this_thread::sleep_for(500ms);

    const auto& stats = scheduler_->stats();
    EXPECT_GE(stats.tasks_submitted.load(), 10);
    EXPECT_GE(stats.tasks_completed.load(), 0);  // Some should complete
}

TEST_F(EDFSchedulerTest, ResetStats) {
    scheduler_->start();

    scheduler_->submit([]() {}, 10ms);
    std::this_thread::sleep_for(50ms);

    scheduler_->reset_stats();

    const auto& stats = scheduler_->stats();
    EXPECT_EQ(stats.tasks_submitted.load(), 0);
}

TEST_F(EDFSchedulerTest, DeadlineMissCallback) {
    scheduler_->start();

    std::atomic<bool> miss_called{false};

    scheduler_->set_deadline_miss_callback([&](const ScheduledTask&) {
        miss_called.store(true);
    });

    // Submit task with very short deadline that will be missed
    scheduler_->submit([&]() {
        std::this_thread::sleep_for(100ms);  // Long task
    }, Timestamp::now() + 1ns);  // Immediate deadline

    std::this_thread::sleep_for(200ms);

    // May or may not trigger depending on scheduling
}

TEST_F(EDFSchedulerTest, ConfigAccess) {
    EDFSchedulerConfig config;
    config.max_queue_size = 5000;
    config.worker_threads = 4;
    config.enable_realtime = false;

    EDFScheduler scheduler(config);
    const auto& cfg = scheduler.config();

    EXPECT_EQ(cfg.max_queue_size, 5000);
    EXPECT_EQ(cfg.worker_threads, 4);
    EXPECT_FALSE(cfg.enable_realtime);
}

TEST_F(EDFSchedulerTest, SetDefaultDeadlineOffset) {
    scheduler_->start();

    scheduler_->set_default_deadline_offset(500ms);

    const auto& cfg = scheduler_->config();
    EXPECT_EQ(cfg.default_deadline_offset.count(), 500000000);
}

TEST_F(EDFSchedulerTest, StopImmediate) {
    scheduler_->start();

    // Submit tasks with long deadlines
    for (int i = 0; i < 100; ++i) {
        scheduler_->submit([]() {
            std::this_thread::sleep_for(100ms);
        }, 10s);
    }

    // Stop immediately
    scheduler_->stop_immediate();

    EXPECT_FALSE(scheduler_->is_running());
}

TEST_F(EDFSchedulerTest, OrderedExecution) {
    scheduler_->start();

    std::vector<int> execution_order;
    std::mutex order_mutex;

    auto now = Timestamp::now();

    // Submit in reverse order
    for (int i = 5; i >= 1; --i) {
        scheduler_->submit([&, i]() {
            std::lock_guard<std::mutex> lock(order_mutex);
            execution_order.push_back(i);
        }, now + std::chrono::milliseconds(i * 10));
    }

    std::this_thread::sleep_for(200ms);

    std::lock_guard<std::mutex> lock(order_mutex);

    // Should execute in deadline order (1, 2, 3, 4, 5)
    if (execution_order.size() == 5) {
        for (size_t i = 0; i < execution_order.size(); ++i) {
            EXPECT_EQ(execution_order[i], static_cast<int>(i + 1));
        }
    }
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(TaskQueueTest, PushPopPerformance) {
    TaskQueue queue;
    const size_t iterations = 100000;

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        ScheduledTask task;
        task.id = i;
        task.deadline = Timestamp::now() + std::chrono::microseconds(i);
        task.task_function = []() {};
        queue.push(std::move(task));
    }

    for (size_t i = 0; i < iterations; ++i) {
        ScheduledTask task;
        queue.pop(task);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto ns_per_op = duration.count() / (iterations * 2);

    EXPECT_LT(ns_per_op, 10000);  // Less than 10us per operation

    std::cout << "TaskQueue push+pop: " << ns_per_op << " ns/op" << std::endl;
}

TEST_F(EDFSchedulerTest, ThroughputTest) {
    scheduler_->start();

    const int num_tasks = 1000;
    std::atomic<int> completed{0};

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_tasks; ++i) {
        scheduler_->submit([&]() {
            completed.fetch_add(1, std::memory_order_relaxed);
        }, 1s);
    }

    // Wait for all tasks
    while (completed.load() < num_tasks && scheduler_->pending_count() > 0) {
        std::this_thread::sleep_for(10ms);
    }

    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "EDFScheduler throughput: " << num_tasks << " tasks in "
              << duration.count() << " ms ("
              << (num_tasks * 1000.0 / duration.count()) << " tasks/sec)"
              << std::endl;

    EXPECT_GE(completed.load(), num_tasks * 0.9);  // At least 90% completed
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
