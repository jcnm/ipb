/**
 * @file test_sink_registry.cpp
 * @brief Unit tests for IPB SinkRegistry
 *
 * Tests coverage for:
 * - LoadBalanceStrategy, SinkHealth enums
 * - SinkInfo: Sink metadata and statistics
 * - SinkSelectionResult: Selection results
 * - SinkRegistryStats: Registry statistics
 * - SinkRegistry: Sink management and load balancing
 */

#include <ipb/core/sink_registry/sink_registry.hpp>

#include <atomic>
#include <future>
#include <memory>

#include <gtest/gtest.h>

using namespace ipb::core;
using namespace ipb::common;

// ============================================================================
// Mock Sink for Testing
// ============================================================================

// Shared state for mock sink (allows tracking writes even through type-erased wrapper)
struct MockSinkState {
    std::string name;
    std::atomic<bool> started{false};
    std::atomic<bool> healthy{true};
    std::atomic<int> write_count{0};

    explicit MockSinkState(const std::string& n) : name(n) {}
};

// Mock sink implementation that inherits from IIPBSinkBase
class MockSinkImpl : public IIPBSinkBase {
public:
    explicit MockSinkImpl(std::shared_ptr<MockSinkState> state) : state_(std::move(state)) {}

    // IIPBComponent interface
    Result<void> start() override {
        state_->started = true;
        return ok();
    }
    Result<void> stop() override {
        state_->started = false;
        return ok();
    }
    bool is_running() const noexcept override { return state_->started; }

    Result<void> configure(const ConfigurationBase&) override { return ok(); }
    std::unique_ptr<ConfigurationBase> get_configuration() const override { return nullptr; }

    Statistics get_statistics() const noexcept override { return {}; }
    void reset_statistics() noexcept override {}

    bool is_healthy() const noexcept override { return state_->healthy; }
    std::string get_health_status() const override { return state_->healthy ? "OK" : "ERROR"; }

    std::string_view component_name() const noexcept override { return state_->name; }
    std::string_view component_version() const noexcept override { return "1.0.0"; }

    // IIPBSinkBase interface
    Result<void> write(const DataPoint&) override {
        state_->write_count++;
        return ok();
    }

    Result<void> write_batch(std::span<const DataPoint> batch) override {
        state_->write_count += static_cast<int>(batch.size());
        return ok();
    }

    Result<void> write_dataset(const DataSet&) override { return ok(); }

    std::future<Result<void>> write_async(const DataPoint&) override {
        std::promise<Result<void>> p;
        p.set_value(ok());
        return p.get_future();
    }

    std::future<Result<void>> write_batch_async(std::span<const DataPoint>) override {
        std::promise<Result<void>> p;
        p.set_value(ok());
        return p.get_future();
    }

    Result<void> flush() override { return ok(); }
    size_t pending_count() const noexcept override { return 0; }
    bool can_accept_data() const noexcept override { return true; }

    std::string_view sink_type() const noexcept override { return "mock"; }
    size_t max_batch_size() const noexcept override { return 1000; }

private:
    std::shared_ptr<MockSinkState> state_;
};

// Test helper that provides both IIPBSink wrapper and access to mock state
class MockSink {
public:
    explicit MockSink(const std::string& name = "mock")
        : state_(std::make_shared<MockSinkState>(name))
          // Note: Can't use make_shared with unique_ptr argument; use shared_ptr constructor
          // directly
          ,
          sink_(std::shared_ptr<IIPBSink>(new IIPBSink(std::make_unique<MockSinkImpl>(state_)))) {}

    // Get the IIPBSink to pass to registry
    std::shared_ptr<IIPBSink> get() const { return sink_; }

    // Implicit conversion to shared_ptr<IIPBSink>
    operator std::shared_ptr<IIPBSink>() const { return sink_; }

    // Access mock state for test assertions
    int write_count() const { return state_->write_count.load(); }
    void set_healthy(bool h) { state_->healthy = h; }
    bool is_started() const { return state_->started; }

private:
    std::shared_ptr<MockSinkState> state_;
    std::shared_ptr<IIPBSink> sink_;
};

// ============================================================================
// LoadBalanceStrategy Tests
// ============================================================================

class LoadBalanceStrategyTest : public ::testing::Test {};

TEST_F(LoadBalanceStrategyTest, StrategyValues) {
    EXPECT_EQ(static_cast<uint8_t>(LoadBalanceStrategy::ROUND_ROBIN), 0);
    EXPECT_EQ(static_cast<uint8_t>(LoadBalanceStrategy::WEIGHTED_ROUND_ROBIN), 1);
    EXPECT_EQ(static_cast<uint8_t>(LoadBalanceStrategy::LEAST_CONNECTIONS), 2);
    EXPECT_EQ(static_cast<uint8_t>(LoadBalanceStrategy::LEAST_LATENCY), 3);
    EXPECT_EQ(static_cast<uint8_t>(LoadBalanceStrategy::HASH_BASED), 4);
    EXPECT_EQ(static_cast<uint8_t>(LoadBalanceStrategy::RANDOM), 5);
    EXPECT_EQ(static_cast<uint8_t>(LoadBalanceStrategy::FAILOVER), 6);
    EXPECT_EQ(static_cast<uint8_t>(LoadBalanceStrategy::BROADCAST), 7);
}

// ============================================================================
// SinkHealth Tests
// ============================================================================

class SinkHealthTest : public ::testing::Test {};

TEST_F(SinkHealthTest, HealthValues) {
    EXPECT_EQ(static_cast<uint8_t>(SinkHealth::HEALTHY), 0);
    EXPECT_EQ(static_cast<uint8_t>(SinkHealth::DEGRADED), 1);
    EXPECT_EQ(static_cast<uint8_t>(SinkHealth::UNHEALTHY), 2);
    EXPECT_EQ(static_cast<uint8_t>(SinkHealth::UNKNOWN), 3);
}

// ============================================================================
// SinkInfo Tests
// ============================================================================

class SinkInfoTest : public ::testing::Test {};

TEST_F(SinkInfoTest, DefaultConstruction) {
    SinkInfo info;
    EXPECT_TRUE(info.id.empty());
    EXPECT_TRUE(info.type.empty());
    EXPECT_EQ(info.weight, 100u);
    EXPECT_TRUE(info.enabled);
    EXPECT_EQ(info.priority, 0u);
    EXPECT_EQ(info.health, SinkHealth::UNKNOWN);
}

TEST_F(SinkInfoTest, SuccessRate) {
    SinkInfo info;

    // No messages yet
    EXPECT_DOUBLE_EQ(info.success_rate(), 100.0);

    // 90% success rate
    info.messages_sent.store(90);
    info.messages_failed.store(10);
    EXPECT_DOUBLE_EQ(info.success_rate(), 90.0);
}

TEST_F(SinkInfoTest, AverageLatency) {
    SinkInfo info;

    // No messages yet
    EXPECT_DOUBLE_EQ(info.avg_latency_us(), 0.0);

    // Set values
    info.messages_sent.store(100);
    info.total_latency_ns.store(1000000);           // 1ms total
    EXPECT_DOUBLE_EQ(info.avg_latency_us(), 10.0);  // 10us average
}

TEST_F(SinkInfoTest, CopyConstruction) {
    SinkInfo original;
    original.id     = "sink1";
    original.type   = "kafka";
    original.weight = 150;
    original.messages_sent.store(100);

    SinkInfo copy(original);

    EXPECT_EQ(copy.id, "sink1");
    EXPECT_EQ(copy.type, "kafka");
    EXPECT_EQ(copy.weight, 150u);
    EXPECT_EQ(copy.messages_sent.load(), 100u);
}

TEST_F(SinkInfoTest, MoveConstruction) {
    SinkInfo original;
    original.id   = "sink1";
    original.type = "kafka";

    SinkInfo moved(std::move(original));

    EXPECT_EQ(moved.id, "sink1");
    EXPECT_EQ(moved.type, "kafka");
}

// ============================================================================
// SinkSelectionResult Tests
// ============================================================================

class SinkSelectionResultTest : public ::testing::Test {};

TEST_F(SinkSelectionResultTest, DefaultConstruction) {
    SinkSelectionResult result;
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.selected_sink_ids.empty());
    EXPECT_TRUE(result.error_message.empty());
}

TEST_F(SinkSelectionResultTest, BoolConversion) {
    SinkSelectionResult success;
    success.success = true;

    SinkSelectionResult failure;
    failure.success = false;

    EXPECT_TRUE(static_cast<bool>(success));
    EXPECT_FALSE(static_cast<bool>(failure));
}

// ============================================================================
// SinkRegistryStats Tests
// ============================================================================

class SinkRegistryStatsTest : public ::testing::Test {};

TEST_F(SinkRegistryStatsTest, DefaultValues) {
    SinkRegistryStats stats;
    EXPECT_EQ(stats.total_selections.load(), 0u);
    EXPECT_EQ(stats.successful_selections.load(), 0u);
    EXPECT_EQ(stats.failed_selections.load(), 0u);
    EXPECT_EQ(stats.failover_events.load(), 0u);
}

TEST_F(SinkRegistryStatsTest, CopyConstruction) {
    SinkRegistryStats original;
    original.total_selections.store(100);
    original.successful_selections.store(90);

    SinkRegistryStats copy(original);

    EXPECT_EQ(copy.total_selections.load(), 100u);
    EXPECT_EQ(copy.successful_selections.load(), 90u);
}

TEST_F(SinkRegistryStatsTest, Reset) {
    SinkRegistryStats stats;
    stats.total_selections.store(100);
    stats.successful_selections.store(90);
    stats.failover_events.store(5);

    stats.reset();

    EXPECT_EQ(stats.total_selections.load(), 0u);
    EXPECT_EQ(stats.successful_selections.load(), 0u);
    EXPECT_EQ(stats.failover_events.load(), 0u);
}

// ============================================================================
// SinkRegistryConfig Tests
// ============================================================================

class SinkRegistryConfigTest : public ::testing::Test {};

TEST_F(SinkRegistryConfigTest, DefaultValues) {
    SinkRegistryConfig config;
    EXPECT_EQ(config.default_strategy, LoadBalanceStrategy::ROUND_ROBIN);
    EXPECT_TRUE(config.enable_health_check);
    EXPECT_EQ(config.health_check_interval, std::chrono::milliseconds(5000));
    EXPECT_EQ(config.unhealthy_threshold, 3u);
    EXPECT_TRUE(config.enable_failover);
}

// ============================================================================
// SinkRegistry Tests
// ============================================================================

class SinkRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.enable_health_check = false;  // Disable for faster tests
    }

    SinkRegistryConfig config_;
};

TEST_F(SinkRegistryTest, DefaultConstruction) {
    SinkRegistry registry;
    EXPECT_FALSE(registry.is_running());
    EXPECT_EQ(registry.sink_count(), 0u);
}

TEST_F(SinkRegistryTest, ConfiguredConstruction) {
    SinkRegistry registry(config_);
    EXPECT_FALSE(registry.is_running());
}

TEST_F(SinkRegistryTest, StartStop) {
    SinkRegistry registry(config_);

    EXPECT_TRUE(registry.start());
    EXPECT_TRUE(registry.is_running());

    registry.stop();
    EXPECT_FALSE(registry.is_running());
}

TEST_F(SinkRegistryTest, RegisterSink) {
    SinkRegistry registry(config_);

    auto sink       = std::make_shared<MockSink>("test_sink");
    bool registered = registry.register_sink("sink1", sink->get());

    EXPECT_TRUE(registered);
    EXPECT_EQ(registry.sink_count(), 1u);
    EXPECT_TRUE(registry.has_sink("sink1"));
}

TEST_F(SinkRegistryTest, RegisterSinkWithWeight) {
    SinkRegistry registry(config_);

    auto sink       = std::make_shared<MockSink>("test_sink");
    bool registered = registry.register_sink("sink1", sink->get(), 200);

    EXPECT_TRUE(registered);

    auto info = registry.get_sink_info("sink1");
    EXPECT_TRUE(info.has_value());
    EXPECT_EQ(info->weight, 200u);
}

TEST_F(SinkRegistryTest, RegisterDuplicateSink) {
    SinkRegistry registry(config_);

    auto sink1 = std::make_shared<MockSink>("sink1");
    auto sink2 = std::make_shared<MockSink>("sink2");

    EXPECT_TRUE(registry.register_sink("sink1", sink1->get()));
    EXPECT_FALSE(registry.register_sink("sink1", sink2->get()));  // Duplicate

    EXPECT_EQ(registry.sink_count(), 1u);
}

TEST_F(SinkRegistryTest, UnregisterSink) {
    SinkRegistry registry(config_);

    auto sink = std::make_shared<MockSink>("test_sink");
    registry.register_sink("sink1", sink->get());

    EXPECT_TRUE(registry.has_sink("sink1"));
    EXPECT_TRUE(registry.unregister_sink("sink1"));
    EXPECT_FALSE(registry.has_sink("sink1"));
}

TEST_F(SinkRegistryTest, UnregisterNonexistentSink) {
    SinkRegistry registry(config_);

    EXPECT_FALSE(registry.unregister_sink("nonexistent"));
}

TEST_F(SinkRegistryTest, GetSink) {
    SinkRegistry registry(config_);

    auto sink = std::make_shared<MockSink>("test_sink");
    registry.register_sink("sink1", sink->get());

    auto retrieved = registry.get_sink("sink1");
    EXPECT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved, sink->get());
}

TEST_F(SinkRegistryTest, GetNonexistentSink) {
    SinkRegistry registry(config_);

    auto retrieved = registry.get_sink("nonexistent");
    EXPECT_EQ(retrieved, nullptr);
}

TEST_F(SinkRegistryTest, GetSinkInfo) {
    SinkRegistry registry(config_);

    auto sink = std::make_shared<MockSink>("test_sink");
    registry.register_sink("sink1", sink->get(), 150);

    auto info = registry.get_sink_info("sink1");
    EXPECT_TRUE(info.has_value());
    EXPECT_EQ(info->id, "sink1");
    EXPECT_EQ(info->weight, 150u);
}

TEST_F(SinkRegistryTest, GetSinkIds) {
    SinkRegistry registry(config_);

    for (int i = 0; i < 5; ++i) {
        auto sink = std::make_shared<MockSink>("sink" + std::to_string(i));
        registry.register_sink("sink" + std::to_string(i), sink->get());
    }

    auto ids = registry.get_sink_ids();
    EXPECT_EQ(ids.size(), 5u);
}

TEST_F(SinkRegistryTest, SetSinkEnabled) {
    SinkRegistry registry(config_);

    auto sink = std::make_shared<MockSink>("test_sink");
    registry.register_sink("sink1", sink->get());

    EXPECT_TRUE(registry.set_sink_enabled("sink1", false));

    auto info = registry.get_sink_info("sink1");
    EXPECT_FALSE(info->enabled);
}

TEST_F(SinkRegistryTest, SetSinkWeight) {
    SinkRegistry registry(config_);

    auto sink = std::make_shared<MockSink>("test_sink");
    registry.register_sink("sink1", sink->get(), 100);

    EXPECT_TRUE(registry.set_sink_weight("sink1", 200));

    auto info = registry.get_sink_info("sink1");
    EXPECT_EQ(info->weight, 200u);
}

TEST_F(SinkRegistryTest, SetSinkPriority) {
    SinkRegistry registry(config_);

    auto sink = std::make_shared<MockSink>("test_sink");
    registry.register_sink("sink1", sink->get());

    EXPECT_TRUE(registry.set_sink_priority("sink1", 10));

    auto info = registry.get_sink_info("sink1");
    EXPECT_EQ(info->priority, 10u);
}

// ============================================================================
// Load Balancing Tests
// ============================================================================

class LoadBalancingTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.enable_health_check = false;

        // Register multiple sinks
        for (int i = 0; i < 3; ++i) {
            auto sink = std::make_unique<MockSink>("sink" + std::to_string(i));
            sinks_.push_back(std::move(sink));
            sink_ids_.push_back("sink" + std::to_string(i));
        }
    }

    SinkRegistryConfig config_;
    std::vector<std::unique_ptr<MockSink>> sinks_;
    std::vector<std::string> sink_ids_;
};

TEST_F(LoadBalancingTest, RoundRobinSelection) {
    SinkRegistry registry(config_);

    for (size_t i = 0; i < sinks_.size(); ++i) {
        registry.register_sink(sink_ids_[i], sinks_[i]->get());
    }

    // Make multiple selections
    std::vector<std::string> selections;
    for (int i = 0; i < 6; ++i) {
        auto result = registry.select_sink(sink_ids_, LoadBalanceStrategy::ROUND_ROBIN);
        EXPECT_TRUE(result.success);
        EXPECT_EQ(result.selected_sink_ids.size(), 1u);
        selections.push_back(result.selected_sink_ids[0]);
    }

    // Round robin should distribute evenly
    // Each sink should be selected twice in 6 selections
}

TEST_F(LoadBalancingTest, RandomSelection) {
    SinkRegistry registry(config_);

    for (size_t i = 0; i < sinks_.size(); ++i) {
        registry.register_sink(sink_ids_[i], sinks_[i]->get());
    }

    auto result = registry.select_sink(sink_ids_, LoadBalanceStrategy::RANDOM);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.selected_sink_ids.size(), 1u);
}

TEST_F(LoadBalancingTest, BroadcastSelection) {
    SinkRegistry registry(config_);

    for (size_t i = 0; i < sinks_.size(); ++i) {
        registry.register_sink(sink_ids_[i], sinks_[i]->get());
    }

    auto result = registry.select_sink(sink_ids_, LoadBalanceStrategy::BROADCAST);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.selected_sink_ids.size(), sinks_.size());
}

TEST_F(LoadBalancingTest, WeightedSelection) {
    SinkRegistry registry(config_);

    // Register sinks with different weights
    registry.register_sink("heavy", sinks_[0]->get(), 300);
    registry.register_sink("medium", sinks_[1]->get(), 100);
    registry.register_sink("light", sinks_[2]->get(), 50);

    std::vector<std::string> candidates = {"heavy", "medium", "light"};

    // Make many selections
    std::map<std::string, int> selection_counts;
    for (int i = 0; i < 100; ++i) {
        auto result = registry.select_sink(candidates, LoadBalanceStrategy::WEIGHTED_ROUND_ROBIN);
        if (result.success && !result.selected_sink_ids.empty()) {
            selection_counts[result.selected_sink_ids[0]]++;
        }
    }

    // Heavy sink should be selected more often
}

TEST_F(LoadBalancingTest, HashBasedSelection) {
    SinkRegistry registry(config_);

    for (size_t i = 0; i < sinks_.size(); ++i) {
        registry.register_sink(sink_ids_[i], sinks_[i]->get());
    }

    DataPoint dp1("sensors/temp1");
    DataPoint dp2("sensors/temp2");

    // Same data point should always select same sink
    auto result1a = registry.select_sink(sink_ids_, dp1, LoadBalanceStrategy::HASH_BASED);
    auto result1b = registry.select_sink(sink_ids_, dp1, LoadBalanceStrategy::HASH_BASED);

    if (result1a.success && result1b.success) {
        EXPECT_EQ(result1a.selected_sink_ids, result1b.selected_sink_ids);
    }
}

TEST_F(LoadBalancingTest, FailoverSelection) {
    SinkRegistry registry(config_);

    for (size_t i = 0; i < sinks_.size(); ++i) {
        registry.register_sink(sink_ids_[i], sinks_[i]->get());
        registry.set_sink_priority(sink_ids_[i], static_cast<uint32_t>(i));
    }

    auto result = registry.select_sink(sink_ids_, LoadBalanceStrategy::FAILOVER);
    EXPECT_TRUE(result.success);
    // Should select highest priority (lowest number) sink
}

TEST_F(LoadBalancingTest, EmptyCandidates) {
    SinkRegistry registry(config_);

    std::vector<std::string> empty_candidates;
    auto result = registry.select_sink(empty_candidates, LoadBalanceStrategy::ROUND_ROBIN);

    EXPECT_FALSE(result.success);
}

// ============================================================================
// Data Routing Tests
// ============================================================================

class DataRoutingTest : public ::testing::Test {
protected:
    void SetUp() override { config_.enable_health_check = false; }

    SinkRegistryConfig config_;
};

TEST_F(DataRoutingTest, WriteToSink) {
    SinkRegistry registry(config_);

    auto sink = std::make_shared<MockSink>("test_sink");
    registry.register_sink("sink1", sink->get());

    DataPoint dp("sensors/temp1");
    dp.set_value(25.5);

    auto result = registry.write_to_sink("sink1", dp);
    EXPECT_TRUE(result.is_success());
    EXPECT_EQ(sink->write_count(), 1);
}

TEST_F(DataRoutingTest, WriteToNonexistentSink) {
    SinkRegistry registry(config_);

    DataPoint dp("sensors/temp1");
    auto result = registry.write_to_sink("nonexistent", dp);

    EXPECT_TRUE(result.is_error());
}

TEST_F(DataRoutingTest, WriteBatchToSink) {
    SinkRegistry registry(config_);

    auto sink = std::make_shared<MockSink>("test_sink");
    registry.register_sink("sink1", sink->get());

    std::vector<DataPoint> batch;
    for (int i = 0; i < 10; ++i) {
        DataPoint dp("sensors/temp" + std::to_string(i));
        dp.set_value(static_cast<double>(20 + i));
        batch.push_back(dp);
    }

    auto result = registry.write_batch_to_sink("sink1", batch);
    EXPECT_TRUE(result.is_success());
    EXPECT_EQ(sink->write_count(), 10);
}

TEST_F(DataRoutingTest, WriteWithLoadBalancing) {
    SinkRegistry registry(config_);

    auto sink1 = std::make_shared<MockSink>("sink1");
    auto sink2 = std::make_shared<MockSink>("sink2");

    registry.register_sink("sink1", sink1->get());
    registry.register_sink("sink2", sink2->get());

    std::vector<std::string> candidates = {"sink1", "sink2"};

    DataPoint dp("sensors/temp1");
    dp.set_value(25.5);

    auto result =
        registry.write_with_load_balancing(candidates, dp, LoadBalanceStrategy::ROUND_ROBIN);
    EXPECT_TRUE(result.is_success());
}

TEST_F(DataRoutingTest, WriteToAll) {
    SinkRegistry registry(config_);

    auto sink1 = std::make_shared<MockSink>("sink1");
    auto sink2 = std::make_shared<MockSink>("sink2");
    auto sink3 = std::make_shared<MockSink>("sink3");

    registry.register_sink("sink1", sink1->get());
    registry.register_sink("sink2", sink2->get());
    registry.register_sink("sink3", sink3->get());

    std::vector<std::string> sink_ids = {"sink1", "sink2", "sink3"};

    DataPoint dp("sensors/temp1");
    dp.set_value(25.5);

    auto results = registry.write_to_all(sink_ids, dp);
    EXPECT_EQ(results.size(), 3u);

    for (const auto& [id, result] : results) {
        EXPECT_TRUE(result.is_success());
    }

    EXPECT_EQ(sink1->write_count(), 1);
    EXPECT_EQ(sink2->write_count(), 1);
    EXPECT_EQ(sink3->write_count(), 1);
}

// ============================================================================
// Health Management Tests
// ============================================================================

class HealthManagementTest : public ::testing::Test {
protected:
    void SetUp() override { config_.enable_health_check = false; }

    SinkRegistryConfig config_;
};

TEST_F(HealthManagementTest, GetSinkHealth) {
    SinkRegistry registry(config_);

    auto sink = std::make_shared<MockSink>("test_sink");
    registry.register_sink("sink1", sink->get());

    auto health = registry.get_sink_health("sink1");
    // Initial health is UNKNOWN
    EXPECT_EQ(health, SinkHealth::UNKNOWN);
}

TEST_F(HealthManagementTest, MarkSinkUnhealthy) {
    SinkRegistry registry(config_);

    auto sink = std::make_shared<MockSink>("test_sink");
    registry.register_sink("sink1", sink->get());

    registry.mark_sink_unhealthy("sink1", "Test failure");

    auto info = registry.get_sink_info("sink1");
    EXPECT_EQ(info->health, SinkHealth::UNHEALTHY);
}

TEST_F(HealthManagementTest, MarkSinkHealthy) {
    SinkRegistry registry(config_);

    auto sink = std::make_shared<MockSink>("test_sink");
    registry.register_sink("sink1", sink->get());

    registry.mark_sink_unhealthy("sink1", "Test failure");
    registry.mark_sink_healthy("sink1");

    auto info = registry.get_sink_info("sink1");
    EXPECT_EQ(info->health, SinkHealth::HEALTHY);
}

TEST_F(HealthManagementTest, GetHealthySinks) {
    SinkRegistry registry(config_);

    auto sink1 = std::make_shared<MockSink>("sink1");
    auto sink2 = std::make_shared<MockSink>("sink2");

    registry.register_sink("sink1", sink1->get());
    registry.register_sink("sink2", sink2->get());

    registry.mark_sink_healthy("sink1");
    registry.mark_sink_unhealthy("sink2", "Failed");

    auto healthy = registry.get_healthy_sinks();
    EXPECT_EQ(healthy.size(), 1u);
    EXPECT_EQ(healthy[0], "sink1");
}

TEST_F(HealthManagementTest, GetUnhealthySinks) {
    SinkRegistry registry(config_);

    auto sink1 = std::make_shared<MockSink>("sink1");
    auto sink2 = std::make_shared<MockSink>("sink2");

    registry.register_sink("sink1", sink1->get());
    registry.register_sink("sink2", sink2->get());

    registry.mark_sink_healthy("sink1");
    registry.mark_sink_unhealthy("sink2", "Failed");

    auto unhealthy = registry.get_unhealthy_sinks();
    EXPECT_EQ(unhealthy.size(), 1u);
    EXPECT_EQ(unhealthy[0], "sink2");
}

// ============================================================================
// Statistics Tests
// ============================================================================

class SinkRegistryStatsIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override { config_.enable_health_check = false; }

    SinkRegistryConfig config_;
};

TEST_F(SinkRegistryStatsIntegrationTest, SelectionStatistics) {
    SinkRegistry registry(config_);

    auto sink = std::make_shared<MockSink>("test_sink");
    registry.register_sink("sink1", sink->get());

    std::vector<std::string> candidates = {"sink1"};

    for (int i = 0; i < 10; ++i) {
        registry.select_sink(candidates, LoadBalanceStrategy::ROUND_ROBIN);
    }

    const auto& stats = registry.stats();
    EXPECT_GE(stats.total_selections.load(), 10u);
}

TEST_F(SinkRegistryStatsIntegrationTest, ResetStatistics) {
    SinkRegistry registry(config_);

    auto sink = std::make_shared<MockSink>("test_sink");
    registry.register_sink("sink1", sink->get());

    std::vector<std::string> candidates = {"sink1"};
    registry.select_sink(candidates, LoadBalanceStrategy::ROUND_ROBIN);

    registry.reset_stats();

    const auto& stats = registry.stats();
    EXPECT_EQ(stats.total_selections.load(), 0u);
}

TEST_F(SinkRegistryStatsIntegrationTest, GetAllSinkStats) {
    SinkRegistry registry(config_);

    for (int i = 0; i < 3; ++i) {
        auto sink = std::make_shared<MockSink>("sink" + std::to_string(i));
        registry.register_sink("sink" + std::to_string(i), sink->get());
    }

    auto all_stats = registry.get_all_sink_stats();
    EXPECT_EQ(all_stats.size(), 3u);
}

TEST_F(SinkRegistryStatsIntegrationTest, MoveConstruction) {
    SinkRegistry registry1(config_);

    auto sink = std::make_shared<MockSink>("test_sink");
    registry1.register_sink("sink1", sink->get());

    SinkRegistry registry2(std::move(registry1));
    EXPECT_EQ(registry2.sink_count(), 1u);
}
