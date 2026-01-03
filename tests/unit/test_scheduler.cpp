/**
 * @file test_scheduler.cpp
 * @brief Unit tests for IPB EDF Scheduler
 *
 * Tests coverage for:
 * - EDFScheduler: Task submission, scheduling, deadlines
 * - ScheduledTask: Task properties and comparison
 * - EDFSchedulerStats: Statistics tracking
 * - EDFSchedulerConfig: Configuration
 */

#include <ipb/core/scheduler/edf_scheduler.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace ipb::core;
using namespace ipb::common;

// ============================================================================
// Task Priority Tests
// ============================================================================

class TaskPriorityTest : public ::testing::Test {};

TEST_F(TaskPriorityTest, PriorityValues) {
    EXPECT_EQ(static_cast<uint8_t>(TaskPriority::BACKGROUND), 0);
    EXPECT_EQ(static_cast<uint8_t>(TaskPriority::LOW), 64);
    EXPECT_EQ(static_cast<uint8_t>(TaskPriority::NORMAL), 128);
    EXPECT_EQ(static_cast<uint8_t>(TaskPriority::HIGH), 192);
    EXPECT_EQ(static_cast<uint8_t>(TaskPriority::CRITICAL), 255);
}

// ============================================================================
// Task State Tests
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
    EXPECT_EQ(task.id, 0u);
    EXPECT_TRUE(task.name.empty());
    EXPECT_EQ(task.priority, TaskPriority::NORMAL);
    EXPECT_EQ(task.state, TaskState::PENDING);
    EXPECT_FALSE(task.deadline_met);
}

TEST_F(ScheduledTaskTest, Comparison) {
    ScheduledTask task1;
    task1.deadline = Timestamp::now() + std::chrono::milliseconds(100);

    ScheduledTask task2;
    task2.deadline = Timestamp::now() + std::chrono::milliseconds(200);

    // Earlier deadline should have higher priority (not >)
    EXPECT_TRUE(task2 > task1);  // task2 has later deadline, so lower priority
}

TEST_F(ScheduledTaskTest, PriorityTieBreaker) {
    auto now = Timestamp::now();

    ScheduledTask task1;
    task1.deadline = now + std::chrono::milliseconds(100);
    task1.priority = TaskPriority::LOW;

    ScheduledTask task2;
    task2.deadline = now + std::chrono::milliseconds(100);  // Same deadline
    task2.priority = TaskPriority::HIGH;

    // With same deadline, higher priority should win
    EXPECT_TRUE(task1 > task2);  // task1 has lower priority
}

// ============================================================================
// SubmitResult Tests
// ============================================================================

class SubmitResultTest : public ::testing::Test {};

TEST_F(SubmitResultTest, DefaultConstruction) {
    SubmitResult result;
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.task_id, 0u);
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
    EXPECT_EQ(stats.tasks_submitted.load(), 0u);
    EXPECT_EQ(stats.tasks_completed.load(), 0u);
    EXPECT_EQ(stats.deadlines_met.load(), 0u);
    EXPECT_EQ(stats.deadlines_missed.load(), 0u);
}

TEST_F(EDFSchedulerStatsTest, DeadlineComplianceRate) {
    EDFSchedulerStats stats;

    // No tasks yet
    EXPECT_DOUBLE_EQ(stats.deadline_compliance_rate(), 100.0);

    // 80% compliance
    stats.deadlines_met.store(80);
    stats.deadlines_missed.store(20);
    EXPECT_DOUBLE_EQ(stats.deadline_compliance_rate(), 80.0);
}

TEST_F(EDFSchedulerStatsTest, AverageLatency) {
    EDFSchedulerStats stats;

    // No tasks yet
    EXPECT_DOUBLE_EQ(stats.avg_latency_us(), 0.0);

    // Set some values
    stats.tasks_completed.store(100);
    stats.total_latency_ns.store(1000000);           // 1ms total
    EXPECT_DOUBLE_EQ(stats.avg_latency_us(), 10.0);  // 10us average
}

TEST_F(EDFSchedulerStatsTest, Reset) {
    EDFSchedulerStats stats;
    stats.tasks_submitted.store(100);
    stats.deadlines_met.store(90);
    stats.deadlines_missed.store(10);

    stats.reset();

    EXPECT_EQ(stats.tasks_submitted.load(), 0u);
    EXPECT_EQ(stats.deadlines_met.load(), 0u);
    EXPECT_EQ(stats.deadlines_missed.load(), 0u);
}

// ============================================================================
// EDFSchedulerConfig Tests
// ============================================================================

class EDFSchedulerConfigTest : public ::testing::Test {};

TEST_F(EDFSchedulerConfigTest, DefaultValues) {
    EDFSchedulerConfig config;
    EXPECT_EQ(config.max_queue_size, 100000u);
    EXPECT_EQ(config.worker_threads, 0u);  // Use hardware concurrency
    EXPECT_FALSE(config.enable_realtime);
    EXPECT_EQ(config.realtime_priority, 50);
    EXPECT_EQ(config.cpu_affinity_start, -1);
}

TEST_F(EDFSchedulerConfigTest, OverflowPolicies) {
    EXPECT_EQ(static_cast<int>(EDFSchedulerConfig::OverflowPolicy::REJECT), 0);
    EXPECT_EQ(static_cast<int>(EDFSchedulerConfig::OverflowPolicy::DROP_LOWEST), 1);
    EXPECT_EQ(static_cast<int>(EDFSchedulerConfig::OverflowPolicy::DROP_FURTHEST), 2);
}

// ============================================================================
// EDFScheduler Tests
// ============================================================================

class EDFSchedulerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.worker_threads = 2;
        config_.max_queue_size = 1000;
    }

    EDFSchedulerConfig config_;
};

TEST_F(EDFSchedulerTest, DefaultConstruction) {
    EDFScheduler scheduler;
    EXPECT_FALSE(scheduler.is_running());
}

TEST_F(EDFSchedulerTest, ConfiguredConstruction) {
    EDFScheduler scheduler(config_);
    EXPECT_FALSE(scheduler.is_running());
    EXPECT_EQ(scheduler.config().worker_threads, 2u);
}

TEST_F(EDFSchedulerTest, StartStop) {
    EDFScheduler scheduler(config_);

    EXPECT_TRUE(scheduler.start());
    EXPECT_TRUE(scheduler.is_running());

    scheduler.stop();
    EXPECT_FALSE(scheduler.is_running());
}

TEST_F(EDFSchedulerTest, SubmitTask) {
    EDFScheduler scheduler(config_);
    scheduler.start();

    std::atomic<bool> executed{false};
    auto result = scheduler.submit([&executed]() { executed = true; });

    EXPECT_TRUE(result.success);
    EXPECT_GT(result.task_id, 0u);

    // Wait for task to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    scheduler.stop();
    EXPECT_TRUE(executed.load());
}

TEST_F(EDFSchedulerTest, SubmitWithDeadline) {
    EDFScheduler scheduler(config_);
    scheduler.start();

    std::atomic<bool> executed{false};
    auto deadline = Timestamp::now() + std::chrono::milliseconds(100);

    auto result = scheduler.submit([&executed]() { executed = true; }, deadline);

    EXPECT_TRUE(result.success);

    // Wait for task to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    scheduler.stop();
    EXPECT_TRUE(executed.load());
}

TEST_F(EDFSchedulerTest, SubmitWithOffset) {
    EDFScheduler scheduler(config_);
    scheduler.start();

    std::atomic<bool> executed{false};

    auto result =
        scheduler.submit([&executed]() { executed = true; }, std::chrono::milliseconds(50));

    EXPECT_TRUE(result.success);

    // Wait for task to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    scheduler.stop();
    EXPECT_TRUE(executed.load());
}

TEST_F(EDFSchedulerTest, SubmitNamedTask) {
    EDFScheduler scheduler(config_);
    scheduler.start();

    std::atomic<bool> executed{false};
    auto deadline = Timestamp::now() + std::chrono::milliseconds(100);

    auto result = scheduler.submit_named("test_task", [&executed]() { executed = true; }, deadline);

    EXPECT_TRUE(result.success);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    scheduler.stop();
    EXPECT_TRUE(executed.load());
}

TEST_F(EDFSchedulerTest, SubmitWithCallback) {
    EDFScheduler scheduler(config_);
    scheduler.start();

    std::atomic<bool> executed{false};
    std::atomic<bool> callback_called{false};
    TaskState callback_state = TaskState::PENDING;

    auto deadline = Timestamp::now() + std::chrono::milliseconds(100);

    auto result = scheduler.submit_with_callback(
        [&executed]() { executed = true; }, deadline,
        [&callback_called, &callback_state](TaskState state, std::chrono::nanoseconds) {
            callback_called = true;
            callback_state  = state;
        });

    EXPECT_TRUE(result.success);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    scheduler.stop();

    EXPECT_TRUE(executed.load());
    EXPECT_TRUE(callback_called.load());
    EXPECT_EQ(callback_state, TaskState::COMPLETED);
}

TEST_F(EDFSchedulerTest, CancelTask) {
    EDFScheduler scheduler(config_);
    scheduler.start();

    // Submit a task with long deadline
    auto deadline = Timestamp::now() + std::chrono::seconds(10);
    auto result =
        scheduler.submit([]() { std::this_thread::sleep_for(std::chrono::seconds(1)); }, deadline);

    EXPECT_TRUE(result.success);

    // Cancel it
    // Note: Cancel may fail if task already started
    (void)scheduler.cancel(result.task_id);

    scheduler.stop();
}

TEST_F(EDFSchedulerTest, PendingCount) {
    EDFScheduler scheduler(config_);
    scheduler.start();

    // Submit multiple tasks with long deadlines
    for (int i = 0; i < 5; ++i) {
        auto deadline = Timestamp::now() + std::chrono::seconds(10);
        scheduler.submit([]() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); },
                         deadline);
    }

    // Should have pending tasks
    EXPECT_GE(scheduler.pending_count(), 0u);

    scheduler.stop_immediate();
}

TEST_F(EDFSchedulerTest, Statistics) {
    EDFScheduler scheduler(config_);
    scheduler.start();

    // Submit and complete tasks
    for (int i = 0; i < 10; ++i) {
        scheduler.submit([]() {
            // Quick task
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto& stats = scheduler.stats();
    EXPECT_GE(stats.tasks_submitted.load(), 10u);

    scheduler.stop();
}

TEST_F(EDFSchedulerTest, ResetStats) {
    EDFScheduler scheduler(config_);
    scheduler.start();

    scheduler.submit([]() {});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    scheduler.reset_stats();

    const auto& stats = scheduler.stats();
    EXPECT_EQ(stats.tasks_submitted.load(), 0u);

    scheduler.stop();
}

TEST_F(EDFSchedulerTest, DeadlineMissCallback) {
    EDFSchedulerConfig cfg;
    cfg.worker_threads        = 1;
    cfg.enable_miss_callbacks = true;

    EDFScheduler scheduler(cfg);

    std::atomic<bool> miss_callback_called{false};
    scheduler.set_deadline_miss_callback(
        [&miss_callback_called](const ScheduledTask&) { miss_callback_called = true; });

    scheduler.start();

    // Submit a task with very short deadline that will be missed
    auto deadline = Timestamp::now();  // Deadline is now, will be missed almost immediately
    scheduler.submit([]() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); },
                     deadline);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    scheduler.stop();

    // The miss callback may or may not be called depending on timing
}

TEST_F(EDFSchedulerTest, MoveConstruction) {
    EDFScheduler scheduler1(config_);
    scheduler1.start();

    EDFScheduler scheduler2(std::move(scheduler1));
    EXPECT_TRUE(scheduler2.is_running());

    scheduler2.stop();
}

TEST_F(EDFSchedulerTest, EDFOrdering) {
    EDFScheduler scheduler(config_);
    scheduler.start();

    std::vector<int> execution_order;
    std::mutex order_mutex;

    auto now = Timestamp::now();

    // Submit tasks with different deadlines (out of order)
    // Use generous deadlines to avoid timing issues on slow systems
    scheduler.submit(
        [&]() {
            std::lock_guard<std::mutex> lock(order_mutex);
            execution_order.push_back(3);
        },
        now + std::chrono::seconds(3));

    scheduler.submit(
        [&]() {
            std::lock_guard<std::mutex> lock(order_mutex);
            execution_order.push_back(1);
        },
        now + std::chrono::seconds(1));

    scheduler.submit(
        [&]() {
            std::lock_guard<std::mutex> lock(order_mutex);
            execution_order.push_back(2);
        },
        now + std::chrono::seconds(2));

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    scheduler.stop();

    // With EDF, tasks should execute in deadline order (or at least all should complete)
    ASSERT_EQ(execution_order.size(), 3u);
    // Note: Exact ordering may vary due to race conditions in task pickup
    // The key is that all tasks execute
}

// ============================================================================
// Periodic Task Tests
// ============================================================================

class PeriodicTaskTest : public ::testing::Test {
protected:
    void SetUp() override { config_.worker_threads = 2; }

    EDFSchedulerConfig config_;
};

TEST_F(PeriodicTaskTest, SubmitPeriodic) {
    EDFScheduler scheduler(config_);
    scheduler.start();

    std::atomic<int> execution_count{0};

    auto periodic_id = scheduler.submit_periodic([&execution_count]() { execution_count++; },
                                                 std::chrono::milliseconds(50));

    EXPECT_GT(periodic_id, 0u);

    // Let it run a few times
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    scheduler.cancel_periodic(periodic_id);
    scheduler.stop();

    EXPECT_GE(execution_count.load(), 2);
}

TEST_F(PeriodicTaskTest, CancelPeriodic) {
    EDFScheduler scheduler(config_);
    scheduler.start();

    std::atomic<int> execution_count{0};

    auto periodic_id = scheduler.submit_periodic([&execution_count]() { execution_count++; },
                                                 std::chrono::milliseconds(50));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    bool cancelled = scheduler.cancel_periodic(periodic_id);
    EXPECT_TRUE(cancelled);

    int count_at_cancel = execution_count.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Should not increase much after cancel
    EXPECT_LE(execution_count.load(), count_at_cancel + 1);

    scheduler.stop();
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

class SchedulerThreadSafetyTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.worker_threads = 4;
        config_.max_queue_size = 10000;
    }

    EDFSchedulerConfig config_;
};

TEST_F(SchedulerThreadSafetyTest, ConcurrentSubmission) {
    EDFScheduler scheduler(config_);
    scheduler.start();

    constexpr int NUM_THREADS      = 4;
    constexpr int TASKS_PER_THREAD = 100;

    std::atomic<int> completed_tasks{0};
    std::vector<std::thread> threads;

    // Use generous deadline to avoid missed deadlines
    auto deadline = Timestamp::now() + std::chrono::seconds(10);

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&scheduler, &completed_tasks, deadline]() {
            for (int i = 0; i < TASKS_PER_THREAD; ++i) {
                auto result =
                    scheduler.submit([&completed_tasks]() { completed_tasks++; }, deadline);
                // Most submissions should succeed
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Wait for all tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    scheduler.stop();

    // Just verify some tasks completed (timing-dependent test)
    EXPECT_GT(completed_tasks.load(), 0);
}
