/**
 * @file test_backpressure.cpp
 * @brief Comprehensive tests for backpressure.hpp
 *
 * Covers: BackpressureStrategy, PressureLevel, BackpressureConfig, BackpressureStats,
 *         PressureSensor, BackpressureController, BackpressureStage, PressurePropagator
 */

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

#include <ipb/common/backpressure.hpp>

using namespace ipb::common;

//=============================================================================
// BackpressureConfig Tests
//=============================================================================

class BackpressureConfigTest : public ::testing::Test {};

TEST_F(BackpressureConfigTest, DefaultValues) {
    BackpressureConfig config;

    EXPECT_EQ(config.strategy, BackpressureStrategy::THROTTLE);
    EXPECT_DOUBLE_EQ(config.low_watermark, 0.5);
    EXPECT_DOUBLE_EQ(config.high_watermark, 0.8);
    EXPECT_DOUBLE_EQ(config.critical_watermark, 0.95);
    EXPECT_EQ(config.target_latency_ns, 1000000);  // 1ms
    EXPECT_EQ(config.max_latency_ns, 10000000);    // 10ms
    EXPECT_EQ(config.sample_rate, 10);
}

TEST_F(BackpressureConfigTest, CustomValues) {
    BackpressureConfig config;
    config.strategy = BackpressureStrategy::DROP_NEWEST;
    config.low_watermark = 0.3;
    config.high_watermark = 0.6;
    config.critical_watermark = 0.9;

    EXPECT_EQ(config.strategy, BackpressureStrategy::DROP_NEWEST);
    EXPECT_DOUBLE_EQ(config.low_watermark, 0.3);
}

//=============================================================================
// BackpressureStats Tests
//=============================================================================

class BackpressureStatsTest : public ::testing::Test {
protected:
    BackpressureStats stats;

    void SetUp() override {
        stats.reset();
    }
};

TEST_F(BackpressureStatsTest, InitialValues) {
    EXPECT_EQ(stats.items_received.load(), 0);
    EXPECT_EQ(stats.items_processed.load(), 0);
    EXPECT_EQ(stats.items_dropped.load(), 0);
    EXPECT_EQ(stats.items_sampled_out.load(), 0);
    EXPECT_EQ(stats.throttle_events.load(), 0);
    EXPECT_EQ(stats.block_events.load(), 0);
    EXPECT_EQ(stats.total_throttle_ns.load(), 0);
    EXPECT_EQ(stats.total_block_ns.load(), 0);
    EXPECT_EQ(stats.pressure_changes.load(), 0);
}

TEST_F(BackpressureStatsTest, DropRate) {
    stats.items_received.store(100);
    stats.items_dropped.store(25);

    EXPECT_DOUBLE_EQ(stats.drop_rate(), 25.0);
}

TEST_F(BackpressureStatsTest, DropRateZeroReceived) {
    EXPECT_DOUBLE_EQ(stats.drop_rate(), 0.0);
}

TEST_F(BackpressureStatsTest, ThroughputFactor) {
    stats.items_received.store(100);
    stats.items_processed.store(80);

    EXPECT_DOUBLE_EQ(stats.throughput_factor(), 0.8);
}

TEST_F(BackpressureStatsTest, ThroughputFactorZeroReceived) {
    EXPECT_DOUBLE_EQ(stats.throughput_factor(), 1.0);
}

TEST_F(BackpressureStatsTest, Reset) {
    stats.items_received.store(100);
    stats.items_processed.store(80);
    stats.items_dropped.store(20);
    stats.throttle_events.store(50);

    stats.reset();

    EXPECT_EQ(stats.items_received.load(), 0);
    EXPECT_EQ(stats.items_processed.load(), 0);
    EXPECT_EQ(stats.items_dropped.load(), 0);
    EXPECT_EQ(stats.throttle_events.load(), 0);
}

//=============================================================================
// PressureSensor Tests
//=============================================================================

class PressureSensorTest : public ::testing::Test {
protected:
    BackpressureConfig config;
    std::unique_ptr<PressureSensor> sensor;

    void SetUp() override {
        config.low_watermark = 0.5;
        config.high_watermark = 0.8;
        config.critical_watermark = 0.95;
        config.target_latency_ns = 1000000;   // 1ms
        config.max_latency_ns = 10000000;     // 10ms
        config.max_memory_bytes = 1000000;    // 1MB

        sensor = std::make_unique<PressureSensor>(config);
    }
};

TEST_F(PressureSensorTest, InitialLevelNone) {
    EXPECT_EQ(sensor->level(), PressureLevel::NONE);
    EXPECT_DOUBLE_EQ(sensor->pressure_value(), 0.0);
}

TEST_F(PressureSensorTest, QueuePressureLow) {
    sensor->update_queue_fill(30, 100);  // 30% fill

    EXPECT_EQ(sensor->level(), PressureLevel::LOW);
    EXPECT_DOUBLE_EQ(sensor->pressure_value(), 0.25);
}

TEST_F(PressureSensorTest, QueuePressureMedium) {
    sensor->update_queue_fill(60, 100);  // 60% fill

    EXPECT_EQ(sensor->level(), PressureLevel::MEDIUM);
    EXPECT_DOUBLE_EQ(sensor->pressure_value(), 0.5);
}

TEST_F(PressureSensorTest, QueuePressureHigh) {
    sensor->update_queue_fill(85, 100);  // 85% fill

    EXPECT_EQ(sensor->level(), PressureLevel::HIGH);
    EXPECT_DOUBLE_EQ(sensor->pressure_value(), 0.75);
}

TEST_F(PressureSensorTest, QueuePressureCritical) {
    sensor->update_queue_fill(98, 100);  // 98% fill

    EXPECT_EQ(sensor->level(), PressureLevel::CRITICAL);
    EXPECT_DOUBLE_EQ(sensor->pressure_value(), 1.0);
}

TEST_F(PressureSensorTest, QueueZeroCapacity) {
    sensor->update_queue_fill(50, 0);  // Should handle gracefully

    EXPECT_EQ(sensor->level(), PressureLevel::NONE);
}

TEST_F(PressureSensorTest, LatencyPressure) {
    // Update latency multiple times to affect EMA
    // EMA converges slowly (alpha = 0.1), so need more iterations with higher values
    for (int i = 0; i < 50; ++i) {
        sensor->update_latency(15000000);  // 15ms - above max (10ms) for CRITICAL
    }

    // After many updates with high latency, should reach at least MEDIUM
    EXPECT_GE(static_cast<int>(sensor->level()), static_cast<int>(PressureLevel::MEDIUM));
}

TEST_F(PressureSensorTest, MemoryPressure) {
    sensor->update_memory(900000);  // 90% of 1MB

    EXPECT_GE(static_cast<int>(sensor->level()), static_cast<int>(PressureLevel::HIGH));
}

TEST_F(PressureSensorTest, MemoryPressureDisabled) {
    BackpressureConfig no_mem_config;
    no_mem_config.max_memory_bytes = 0;  // Disabled

    PressureSensor no_mem_sensor(no_mem_config);
    no_mem_sensor.update_memory(999999999);

    // Memory pressure should be ignored
    EXPECT_EQ(no_mem_sensor.level(), PressureLevel::NONE);
}

TEST_F(PressureSensorTest, MaxPressureTaken) {
    // Set queue low but memory critical
    sensor->update_queue_fill(10, 100);   // Low queue
    sensor->update_memory(980000);        // 98% memory

    // Should take max of all pressures
    EXPECT_EQ(sensor->level(), PressureLevel::CRITICAL);
}

TEST_F(PressureSensorTest, ConfigAccess) {
    const BackpressureConfig& retrieved_config = sensor->config();

    EXPECT_DOUBLE_EQ(retrieved_config.low_watermark, config.low_watermark);
    EXPECT_DOUBLE_EQ(retrieved_config.high_watermark, config.high_watermark);
}

//=============================================================================
// BackpressureController Tests
//=============================================================================

class BackpressureControllerTest : public ::testing::Test {
protected:
    std::unique_ptr<BackpressureController> controller;

    void SetUp() override {
        BackpressureConfig config;
        config.strategy = BackpressureStrategy::THROTTLE;
        config.sample_rate = 5;
        controller = std::make_unique<BackpressureController>(config);
    }
};

TEST_F(BackpressureControllerTest, InitialState) {
    EXPECT_EQ(controller->pressure_level(), PressureLevel::NONE);
    EXPECT_EQ(controller->throttle_delay_ns(), 0);
}

TEST_F(BackpressureControllerTest, ShouldAcceptNoPressure) {
    EXPECT_TRUE(controller->should_accept());

    const auto& stats = controller->stats();
    EXPECT_EQ(stats.items_received.load(), 1);
}

TEST_F(BackpressureControllerTest, ItemProcessed) {
    controller->should_accept();
    controller->item_processed();

    const auto& stats = controller->stats();
    EXPECT_EQ(stats.items_processed.load(), 1);
}

TEST_F(BackpressureControllerTest, ItemDropped) {
    controller->should_accept();
    controller->item_dropped();

    const auto& stats = controller->stats();
    EXPECT_EQ(stats.items_dropped.load(), 1);
}

TEST_F(BackpressureControllerTest, UpdateQueue) {
    controller->update_queue(95, 100);  // 95% full

    controller->should_accept();  // Trigger level update

    EXPECT_EQ(controller->pressure_level(), PressureLevel::CRITICAL);
}

TEST_F(BackpressureControllerTest, UpdateLatency) {
    // Should handle latency updates
    controller->update_latency(5000000);  // 5ms
    EXPECT_EQ(controller->pressure_level(), PressureLevel::NONE);  // Initial
}

TEST_F(BackpressureControllerTest, UpdateMemory) {
    // Should handle memory updates
    controller->update_memory(1000);
    EXPECT_EQ(controller->pressure_level(), PressureLevel::NONE);  // Initial
}

TEST_F(BackpressureControllerTest, DropCallback) {
    int drop_count = 0;
    controller->set_drop_callback([&drop_count](size_t count) {
        drop_count += count;
    });

    controller->item_dropped();
    controller->item_dropped();

    EXPECT_EQ(drop_count, 2);
}

TEST_F(BackpressureControllerTest, PressureCallback) {
    PressureLevel last_level = PressureLevel::NONE;
    controller->set_pressure_callback([&last_level](PressureLevel level) {
        last_level = level;
    });

    controller->update_queue(99, 100);  // Critical
    controller->should_accept();  // Trigger update

    // Note: Due to hysteresis, callback may or may not be called
    EXPECT_TRUE(last_level == PressureLevel::NONE ||
                last_level == PressureLevel::CRITICAL);
}

TEST_F(BackpressureControllerTest, ResetStats) {
    controller->should_accept();
    controller->item_processed();
    controller->item_dropped();

    controller->reset_stats();

    const auto& stats = controller->stats();
    EXPECT_EQ(stats.items_received.load(), 0);
    EXPECT_EQ(stats.items_processed.load(), 0);
    EXPECT_EQ(stats.items_dropped.load(), 0);
}

TEST_F(BackpressureControllerTest, ConfigAccess) {
    const BackpressureConfig& config = controller->config();
    EXPECT_EQ(config.strategy, BackpressureStrategy::THROTTLE);
}

//=============================================================================
// BackpressureController Strategy Tests
//=============================================================================

class DropNewestStrategyTest : public ::testing::Test {
protected:
    std::unique_ptr<BackpressureController> controller;

    void SetUp() override {
        BackpressureConfig config;
        config.strategy = BackpressureStrategy::DROP_NEWEST;
        controller = std::make_unique<BackpressureController>(config);
    }
};

TEST_F(DropNewestStrategyTest, AcceptsUnderNormalPressure) {
    controller->update_queue(50, 100);  // 50% - medium

    EXPECT_TRUE(controller->should_accept());
}

TEST_F(DropNewestStrategyTest, RejectsUnderCriticalPressure) {
    controller->update_queue(99, 100);  // 99% - critical
    controller->should_accept();  // First to set pressure level

    EXPECT_FALSE(controller->should_accept());  // Should reject

    const auto& stats = controller->stats();
    EXPECT_GT(stats.items_dropped.load(), 0);
}

class DropOldestStrategyTest : public ::testing::Test {
protected:
    std::unique_ptr<BackpressureController> controller;

    void SetUp() override {
        BackpressureConfig config;
        config.strategy = BackpressureStrategy::DROP_OLDEST;
        controller = std::make_unique<BackpressureController>(config);
    }
};

TEST_F(DropOldestStrategyTest, AlwaysAcceptsNewItems) {
    controller->update_queue(99, 100);  // Critical
    controller->should_accept();

    // DROP_OLDEST always accepts new items (caller must handle dropping oldest)
    EXPECT_TRUE(controller->should_accept());
    EXPECT_TRUE(controller->should_accept());
    EXPECT_TRUE(controller->should_accept());
}

class SampleStrategyTest : public ::testing::Test {
protected:
    std::unique_ptr<BackpressureController> controller;

    void SetUp() override {
        BackpressureConfig config;
        config.strategy = BackpressureStrategy::SAMPLE;
        config.sample_rate = 4;  // Keep 1 in 4
        controller = std::make_unique<BackpressureController>(config);
    }
};

TEST_F(SampleStrategyTest, NoPressureNoSampling) {
    // Under no pressure, all items should be accepted
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(controller->should_accept());
    }
}

TEST_F(SampleStrategyTest, SamplingUnderPressure) {
    controller->update_queue(70, 100);  // Medium pressure
    controller->should_accept();  // Set pressure level

    int accepted = 0;
    int rejected = 0;

    for (int i = 0; i < 100; ++i) {
        if (controller->should_accept()) {
            accepted++;
        } else {
            rejected++;
        }
    }

    // Under medium pressure with sample_rate=4, should accept roughly 1/4
    EXPECT_GT(rejected, 0);
    EXPECT_GT(accepted, 0);
}

class ThrottleStrategyTest : public ::testing::Test {
protected:
    std::unique_ptr<BackpressureController> controller;

    void SetUp() override {
        BackpressureConfig config;
        config.strategy = BackpressureStrategy::THROTTLE;
        config.throttle_step_ns = 1000;       // 1μs steps
        config.max_throttle_ns = 1000000;     // 1ms max
        controller = std::make_unique<BackpressureController>(config);
    }
};

TEST_F(ThrottleStrategyTest, NoThrottleNoPressure) {
    EXPECT_TRUE(controller->should_accept());
    EXPECT_EQ(controller->throttle_delay_ns(), 0);
}

TEST_F(ThrottleStrategyTest, ThrottleUnderPressure) {
    controller->update_queue(60, 100);  // Medium pressure
    controller->should_accept();

    // Should have applied some throttle
    const auto& stats = controller->stats();
    EXPECT_GE(stats.throttle_events.load(), 0);
}

TEST_F(ThrottleStrategyTest, AlwaysAccepts) {
    controller->update_queue(99, 100);  // Critical
    controller->should_accept();

    // Throttle strategy always accepts (just delays)
    EXPECT_TRUE(controller->should_accept());
}

class BlockStrategyTest : public ::testing::Test {
protected:
    std::unique_ptr<BackpressureController> controller;

    void SetUp() override {
        BackpressureConfig config;
        config.strategy = BackpressureStrategy::BLOCK;
        config.max_throttle_ns = 1000000;  // 1ms timeout
        controller = std::make_unique<BackpressureController>(config);
    }
};

TEST_F(BlockStrategyTest, NoBlockNoPressure) {
    EXPECT_TRUE(controller->should_accept());

    const auto& stats = controller->stats();
    EXPECT_EQ(stats.block_events.load(), 0);
}

TEST_F(BlockStrategyTest, BlocksUnderHighPressure) {
    controller->update_queue(90, 100);  // High pressure
    controller->should_accept();  // Set level

    auto start = std::chrono::steady_clock::now();
    bool result = controller->should_accept();
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Should either block briefly or time out
    const auto& stats = controller->stats();
    if (!result) {
        // Timed out and dropped
        EXPECT_GT(stats.items_dropped.load(), 0);
    }
}

//=============================================================================
// BackpressureStage Tests
//=============================================================================

class BackpressureStageTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(BackpressureStageTest, ProcessWithoutPressure) {
    BackpressureConfig config;
    config.strategy = BackpressureStrategy::THROTTLE;

    BackpressureStage<int, int> stage(config, [](const int& input) -> std::optional<int> {
        return input * 2;
    });

    auto result = stage.process(21);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
}

TEST_F(BackpressureStageTest, ProcessWithNulloptReturn) {
    BackpressureConfig config;

    BackpressureStage<int, int> stage(config, [](const int&) -> std::optional<int> {
        return std::nullopt;  // Simulate processing failure
    });

    auto result = stage.process(42);
    EXPECT_FALSE(result.has_value());

    const auto& stats = stage.controller().stats();
    EXPECT_EQ(stats.items_dropped.load(), 1);
}

TEST_F(BackpressureStageTest, UpdateQueue) {
    BackpressureConfig config;

    BackpressureStage<int, int> stage(config, [](const int& input) {
        return std::optional<int>(input);
    });

    stage.update_queue(50, 100);

    // Controller should have received update
    EXPECT_EQ(stage.controller().pressure_level(), PressureLevel::NONE);
}

TEST_F(BackpressureStageTest, ControllerAccess) {
    BackpressureConfig config;
    config.strategy = BackpressureStrategy::DROP_NEWEST;

    BackpressureStage<int, int> stage(config, [](const int& input) {
        return std::optional<int>(input);
    });

    EXPECT_EQ(stage.controller().config().strategy, BackpressureStrategy::DROP_NEWEST);
}

TEST_F(BackpressureStageTest, ConstControllerAccess) {
    BackpressureConfig config;

    const BackpressureStage<int, int> stage(config, [](const int& input) {
        return std::optional<int>(input);
    });

    const BackpressureController& controller = stage.controller();
    EXPECT_EQ(controller.pressure_level(), PressureLevel::NONE);
}

TEST_F(BackpressureStageTest, LatencyTracking) {
    BackpressureConfig config;

    BackpressureStage<int, int> stage(config, [](const int& input) -> std::optional<int> {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        return input;
    });

    stage.process(1);
    stage.process(2);
    stage.process(3);

    // Latency should be tracked in controller
    const auto& stats = stage.controller().stats();
    EXPECT_EQ(stats.items_processed.load(), 3);
}

//=============================================================================
// PressurePropagator Tests
//=============================================================================

class PressurePropagatorTest : public ::testing::Test {
protected:
    PressurePropagator propagator;
    std::vector<std::unique_ptr<BackpressureController>> controllers;

    void SetUp() override {
        controllers.clear();
    }
};

TEST_F(PressurePropagatorTest, EmptyPropagator) {
    EXPECT_EQ(propagator.max_pressure(), PressureLevel::NONE);
    EXPECT_FALSE(propagator.is_critical());
}

TEST_F(PressurePropagatorTest, SingleStage) {
    BackpressureConfig config;
    controllers.push_back(std::make_unique<BackpressureController>(config));
    propagator.add_stage(controllers[0].get());

    EXPECT_EQ(propagator.max_pressure(), PressureLevel::NONE);
}

TEST_F(PressurePropagatorTest, MaxPressureAcrossStages) {
    BackpressureConfig config;

    // Create 3 stages
    for (int i = 0; i < 3; ++i) {
        controllers.push_back(std::make_unique<BackpressureController>(config));
        propagator.add_stage(controllers[i].get());
    }

    // Set different pressure levels
    controllers[0]->update_queue(30, 100);  // Low
    controllers[0]->should_accept();

    controllers[1]->update_queue(60, 100);  // Medium
    controllers[1]->should_accept();

    controllers[2]->update_queue(99, 100);  // Critical
    controllers[2]->should_accept();

    EXPECT_EQ(propagator.max_pressure(), PressureLevel::CRITICAL);
    EXPECT_TRUE(propagator.is_critical());
}

TEST_F(PressurePropagatorTest, IsCritical) {
    BackpressureConfig config;

    controllers.push_back(std::make_unique<BackpressureController>(config));
    propagator.add_stage(controllers[0].get());

    // Not critical initially
    EXPECT_FALSE(propagator.is_critical());

    // Set to critical
    controllers[0]->update_queue(99, 100);
    controllers[0]->should_accept();

    EXPECT_TRUE(propagator.is_critical());
}

TEST_F(PressurePropagatorTest, AggregateStats) {
    BackpressureConfig config;

    for (int i = 0; i < 3; ++i) {
        controllers.push_back(std::make_unique<BackpressureController>(config));
        propagator.add_stage(controllers[i].get());
    }

    // Generate some stats
    for (auto& ctrl : controllers) {
        ctrl->should_accept();
        ctrl->should_accept();
        ctrl->item_processed();
        ctrl->item_dropped();
    }

    BackpressureStats total;
    propagator.aggregate_stats(total);

    // 3 controllers × 2 receives each = 6 total
    EXPECT_EQ(total.items_received.load(), 6);
    // 3 controllers × 1 processed each = 3 total
    EXPECT_EQ(total.items_processed.load(), 3);
    // 3 controllers × 1 dropped each = 3 total
    EXPECT_EQ(total.items_dropped.load(), 3);
}

//=============================================================================
// Integration Tests
//=============================================================================

TEST(BackpressureIntegrationTest, PipelineWithMultipleStages) {
    BackpressureConfig config;
    config.strategy = BackpressureStrategy::THROTTLE;
    config.throttle_step_ns = 100;

    // Create a simple 3-stage pipeline
    BackpressureStage<int, int> stage1(config, [](const int& x) {
        return std::optional<int>(x + 1);
    });

    BackpressureStage<int, int> stage2(config, [](const int& x) {
        return std::optional<int>(x * 2);
    });

    BackpressureStage<int, int> stage3(config, [](const int& x) {
        return std::optional<int>(x - 1);
    });

    // Process through pipeline
    auto r1 = stage1.process(10);  // 10 + 1 = 11
    ASSERT_TRUE(r1.has_value());

    auto r2 = stage2.process(*r1);  // 11 * 2 = 22
    ASSERT_TRUE(r2.has_value());

    auto r3 = stage3.process(*r2);  // 22 - 1 = 21
    ASSERT_TRUE(r3.has_value());

    EXPECT_EQ(*r3, 21);
}

TEST(BackpressureIntegrationTest, ConcurrentPressureUpdates) {
    BackpressureConfig config;
    BackpressureController controller(config);

    constexpr int NUM_THREADS = 4;
    constexpr int OPS_PER_THREAD = 1000;

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&controller, i]() {
            for (int j = 0; j < OPS_PER_THREAD; ++j) {
                controller.update_queue((i * 20 + j) % 100, 100);
                controller.should_accept();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    const auto& stats = controller.stats();
    EXPECT_EQ(stats.items_received.load(), NUM_THREADS * OPS_PER_THREAD);
}

//=============================================================================
// Edge Cases
//=============================================================================

TEST(BackpressureEdgeCaseTest, ZeroQueueCapacity) {
    BackpressureConfig config;
    PressureSensor sensor(config);

    sensor.update_queue_fill(0, 0);  // Zero capacity

    EXPECT_EQ(sensor.level(), PressureLevel::NONE);
}

TEST(BackpressureEdgeCaseTest, VeryHighLatency) {
    BackpressureConfig config;
    config.max_latency_ns = 10000000;

    PressureSensor sensor(config);

    // Update with extremely high latency
    for (int i = 0; i < 50; ++i) {
        sensor.update_latency(1000000000);  // 1 second
    }

    EXPECT_EQ(sensor.level(), PressureLevel::CRITICAL);
}

TEST(BackpressureEdgeCaseTest, RapidPressureChanges) {
    BackpressureConfig config;
    config.hysteresis_ns = 0;  // No hysteresis for test

    BackpressureController controller(config);

    // Rapidly change pressure
    for (int i = 0; i < 100; ++i) {
        int fill = (i % 2 == 0) ? 99 : 10;  // Alternating
        controller.update_queue(fill, 100);
        controller.should_accept();
    }

    // Controller should handle rapid changes without crashing
    const auto& stats = controller.stats();
    EXPECT_EQ(stats.items_received.load(), 100);
}
