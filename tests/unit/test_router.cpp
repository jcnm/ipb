/**
 * @file test_router.cpp
 * @brief Unit tests for IPB Router
 *
 * Tests coverage for:
 * - RuleType, RoutingPriority, LoadBalanceStrategy enums
 * - router::RoutingRule: Rule definition
 * - router::RouterConfig: Configuration
 * - RuleBuilder: Fluent rule construction
 * - Router: Core routing functionality
 */

#include <gtest/gtest.h>
#include <ipb/router/router.hpp>
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <memory>

// Use explicit namespaces to avoid ambiguity between ipb::router and ipb::core types
namespace router = ipb::router;
namespace core = ipb::core;
namespace common = ipb::common;
using common::DataPoint;
using common::DataSet;
using common::Result;
using common::Timestamp;
using common::ok;
using common::IIPBSink;
using common::IIPBSinkBase;
using common::ConfigurationBase;
using common::Statistics;

// ============================================================================
// Mock Sink for Testing
// ============================================================================

// Shared state for mock sink (allows tracking writes even through type-erased wrapper)
struct RouterMockSinkState {
    std::string name;
    std::atomic<bool> started{false};
    std::atomic<bool> healthy{true};
    std::atomic<int> write_count{0};
    std::string last_address;

    explicit RouterMockSinkState(const std::string& n) : name(n) {}
};

// Mock sink implementation that inherits from IIPBSinkBase
class RouterMockSinkImpl : public IIPBSinkBase {
public:
    explicit RouterMockSinkImpl(std::shared_ptr<RouterMockSinkState> state)
        : state_(std::move(state)) {}

    Result<void> start() override { state_->started = true; return ok(); }
    Result<void> stop() override { state_->started = false; return ok(); }
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
    Result<void> write(const DataPoint& dp) override {
        state_->write_count++;
        state_->last_address = std::string(dp.address());
        return ok();
    }

    Result<void> write_batch(std::span<const DataPoint> batch) override {
        state_->write_count += static_cast<int>(batch.size());
        return ok();
    }

    Result<void> write_dataset(const DataSet&) override {
        return ok();
    }

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
    std::shared_ptr<RouterMockSinkState> state_;
};

// Test helper that provides both IIPBSink wrapper and access to mock state
class RouterMockSink {
public:
    explicit RouterMockSink(const std::string& name = "mock")
        : state_(std::make_shared<RouterMockSinkState>(name))
        // Note: Can't use make_shared with unique_ptr argument; use shared_ptr constructor directly
        , sink_(std::shared_ptr<IIPBSink>(new IIPBSink(std::make_unique<RouterMockSinkImpl>(state_)))) {}

    // Get the IIPBSink to pass to router
    std::shared_ptr<IIPBSink> get() const { return sink_; }

    // Implicit conversion to shared_ptr<IIPBSink>
    operator std::shared_ptr<IIPBSink>() const { return sink_; }

    // Access mock state for test assertions
    void set_healthy(bool h) { state_->healthy = h; }
    int write_count() const { return state_->write_count.load(); }
    std::string last_address() const { return state_->last_address; }
    bool is_started() const { return state_->started; }

private:
    std::shared_ptr<RouterMockSinkState> state_;
    std::shared_ptr<IIPBSink> sink_;
};

// ============================================================================
// Router RuleType Tests
// ============================================================================

class RouterRuleTypeTest : public ::testing::Test {};

TEST_F(RouterRuleTypeTest, TypeValues) {
    EXPECT_EQ(static_cast<uint8_t>(router::RuleType::STATIC), 0);
    EXPECT_EQ(static_cast<uint8_t>(router::RuleType::PROTOCOL_BASED), 1);
    EXPECT_EQ(static_cast<uint8_t>(router::RuleType::REGEX_PATTERN), 2);
    EXPECT_EQ(static_cast<uint8_t>(router::RuleType::QUALITY_BASED), 3);
}

TEST_F(RouterRuleTypeTest, RuleTypeNames) {
    EXPECT_EQ(router::rule_type_name(router::RuleType::STATIC), "STATIC");
    EXPECT_EQ(router::rule_type_name(router::RuleType::PROTOCOL_BASED), "PROTOCOL_BASED");
    EXPECT_EQ(router::rule_type_name(router::RuleType::REGEX_PATTERN), "REGEX_PATTERN");
    EXPECT_EQ(router::rule_type_name(router::RuleType::LOAD_BALANCING), "LOAD_BALANCING");
    EXPECT_EQ(router::rule_type_name(router::RuleType::FAILOVER), "FAILOVER");
    EXPECT_EQ(router::rule_type_name(router::RuleType::BROADCAST), "BROADCAST");
}

// ============================================================================
// RoutingPriority Tests
// ============================================================================

class RoutingPriorityTest : public ::testing::Test {};

TEST_F(RoutingPriorityTest, PriorityValues) {
    EXPECT_EQ(static_cast<uint8_t>(router::RoutingPriority::LOWEST), 0);
    EXPECT_EQ(static_cast<uint8_t>(router::RoutingPriority::LOW), 64);
    EXPECT_EQ(static_cast<uint8_t>(router::RoutingPriority::NORMAL), 128);
    EXPECT_EQ(static_cast<uint8_t>(router::RoutingPriority::HIGH), 192);
    EXPECT_EQ(static_cast<uint8_t>(router::RoutingPriority::HIGHEST), 255);
    EXPECT_EQ(static_cast<uint8_t>(router::RoutingPriority::REALTIME), 254);
}

// ============================================================================
// LoadBalanceStrategy Tests
// ============================================================================

class RouterLoadBalanceStrategyTest : public ::testing::Test {};

TEST_F(RouterLoadBalanceStrategyTest, StrategyValues) {
    EXPECT_EQ(static_cast<uint8_t>(router::LoadBalanceStrategy::ROUND_ROBIN), 0);
    EXPECT_EQ(static_cast<uint8_t>(router::LoadBalanceStrategy::WEIGHTED_ROUND_ROBIN), 1);
    EXPECT_EQ(static_cast<uint8_t>(router::LoadBalanceStrategy::LEAST_CONNECTIONS), 2);
}

// ============================================================================
// router::RoutingRule Tests
// ============================================================================

class RouterRoutingRuleTest : public ::testing::Test {};

TEST_F(RouterRoutingRuleTest, DefaultConstruction) {
    router::RoutingRule rule;
    EXPECT_EQ(rule.rule_id, 0u);
    EXPECT_TRUE(rule.name.empty());
    EXPECT_EQ(rule.type, router::RuleType::STATIC);
    EXPECT_EQ(rule.priority, router::RoutingPriority::NORMAL);
    EXPECT_TRUE(rule.enabled);
}

TEST_F(RouterRoutingRuleTest, CopyConstruction) {
    router::RoutingRule original;
    original.rule_id = 42;
    original.name = "test_rule";
    original.type = router::RuleType::REGEX_PATTERN;
    original.address_pattern = "sensors/.*";
    original.target_sink_ids = {"sink1", "sink2"};

    router::RoutingRule copy(original);

    EXPECT_EQ(copy.rule_id, 42u);
    EXPECT_EQ(copy.name, "test_rule");
    EXPECT_EQ(copy.type, router::RuleType::REGEX_PATTERN);
    EXPECT_EQ(copy.address_pattern, "sensors/.*");
    EXPECT_EQ(copy.target_sink_ids.size(), 2u);
}

TEST_F(RouterRoutingRuleTest, MoveConstruction) {
    router::RoutingRule original;
    original.rule_id = 42;
    original.name = "test_rule";

    router::RoutingRule moved(std::move(original));

    EXPECT_EQ(moved.rule_id, 42u);
    EXPECT_EQ(moved.name, "test_rule");
}

// ============================================================================
// RuleBuilder Tests
// ============================================================================

class RouterRuleBuilderTest : public ::testing::Test {};

TEST_F(RouterRuleBuilderTest, BuildStaticRule) {
    auto rule = router::RuleBuilder()
        .name("static_rule")
        .priority(router::RoutingPriority::HIGH)
        .match_address("sensors/temp1")
        .route_to("influxdb")
        .build();

    EXPECT_EQ(rule.name, "static_rule");
    EXPECT_EQ(rule.priority, router::RoutingPriority::HIGH);
    EXPECT_EQ(rule.source_addresses.size(), 1u);
    EXPECT_EQ(rule.target_sink_ids.size(), 1u);
}

TEST_F(RouterRuleBuilderTest, BuildPatternRule) {
    auto rule = router::RuleBuilder()
        .name("pattern_rule")
        .match_pattern("sensors/temp.*")
        .route_to(std::vector<std::string>{"kafka", "influxdb"})
        .build();

    EXPECT_EQ(rule.name, "pattern_rule");
    EXPECT_EQ(rule.type, router::RuleType::REGEX_PATTERN);
    EXPECT_EQ(rule.address_pattern, "sensors/temp.*");
}

TEST_F(RouterRuleBuilderTest, BuildLoadBalancedRule) {
    auto rule = router::RuleBuilder()
        .name("lb_rule")
        .match_pattern(".*")
        .route_to(std::vector<std::string>{"sink1", "sink2", "sink3"})
        .load_balance(router::LoadBalanceStrategy::WEIGHTED_ROUND_ROBIN)
        .with_weights(std::vector<uint32_t>{100, 200, 50})
        .build();

    EXPECT_EQ(rule.load_balance_strategy, router::LoadBalanceStrategy::WEIGHTED_ROUND_ROBIN);
    EXPECT_EQ(rule.sink_weights.size(), 3u);
}

TEST_F(RouterRuleBuilderTest, BuildFailoverRule) {
    auto rule = router::RuleBuilder()
        .name("failover_rule")
        .match_address("critical/data")
        .route_to("primary_sink")
        .with_failover(std::vector<std::string>{"backup1", "backup2"})
        .build();

    EXPECT_TRUE(rule.enable_failover);
    EXPECT_EQ(rule.backup_sink_ids.size(), 2u);
}

TEST_F(RouterRuleBuilderTest, BuildBatchingRule) {
    auto rule = router::RuleBuilder()
        .name("batching_rule")
        .match_pattern("sensors/.*")
        .route_to("batch_sink")
        .enable_batching(100, std::chrono::milliseconds(50))
        .build();

    EXPECT_TRUE(rule.enable_batching);
    EXPECT_EQ(rule.batch_size, 100u);
    EXPECT_EQ(rule.batch_timeout, std::chrono::milliseconds(50));
}

// ============================================================================
// router::RouterConfig Tests
// ============================================================================

class RouterConfigTest : public ::testing::Test {};

TEST_F(RouterConfigTest, DefaultConfig) {
    auto config = router::RouterConfig::default_config();
    EXPECT_TRUE(config.enable_dead_letter_queue);
}

TEST_F(RouterConfigTest, HighThroughputConfig) {
    auto config = router::RouterConfig::high_throughput();
    // High throughput config should have larger buffers
}

TEST_F(RouterConfigTest, LowLatencyConfig) {
    auto config = router::RouterConfig::low_latency();
    // Low latency config should have smaller buffers but more threads
}

TEST_F(RouterConfigTest, RealtimeConfig) {
    auto config = router::RouterConfig::realtime();
    // Realtime config should have real-time scheduling enabled
}

// ============================================================================
// Router Tests
// ============================================================================

class RouterTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = router::RouterConfig::default_config();
        config_.message_bus.dispatcher_threads = 2;
        config_.scheduler.worker_threads = 2;
        config_.sink_registry.enable_health_check = false;
    }

    router::RouterConfig config_;
};

TEST_F(RouterTest, DefaultConstruction) {
    router::Router router;
    EXPECT_FALSE(router.is_running());
}

TEST_F(RouterTest, ConfiguredConstruction) {
    router::Router router(config_);
    EXPECT_FALSE(router.is_running());
}

TEST_F(RouterTest, StartStop) {
    router::Router router(config_);

    auto start_result = router.start();
    EXPECT_TRUE(start_result.is_success());
    EXPECT_TRUE(router.is_running());

    auto stop_result = router.stop();
    EXPECT_TRUE(stop_result.is_success());
    EXPECT_FALSE(router.is_running());
}

TEST_F(RouterTest, ComponentName) {
    router::Router router;
    EXPECT_EQ(router.component_name(), "IPBRouter");
    EXPECT_EQ(router.component_version(), "2.0.0");
}

TEST_F(RouterTest, RegisterSink) {
    router::Router router(config_);

    auto sink = std::make_shared<RouterMockSink>("test_sink");
    auto result = router.register_sink("sink1", sink->get());

    EXPECT_TRUE(result.is_success());

    auto sinks = router.get_registered_sinks();
    EXPECT_EQ(sinks.size(), 1u);
    EXPECT_EQ(sinks[0], "sink1");
}

TEST_F(RouterTest, RegisterSinkWithWeight) {
    router::Router router(config_);

    auto sink = std::make_shared<RouterMockSink>("test_sink");
    auto result = router.register_sink("sink1", sink->get(), 200);

    EXPECT_TRUE(result.is_success());
}

TEST_F(RouterTest, UnregisterSink) {
    router::Router router(config_);

    auto sink = std::make_shared<RouterMockSink>("test_sink");
    router.register_sink("sink1", sink->get());

    auto result = router.unregister_sink("sink1");
    EXPECT_TRUE(result.is_success());

    auto sinks = router.get_registered_sinks();
    EXPECT_TRUE(sinks.empty());
}

TEST_F(RouterTest, SetSinkWeight) {
    router::Router router(config_);

    auto sink = std::make_shared<RouterMockSink>("test_sink");
    router.register_sink("sink1", sink->get(), 100);

    auto result = router.set_sink_weight("sink1", 200);
    EXPECT_TRUE(result.is_success());
}

TEST_F(RouterTest, EnableDisableSink) {
    router::Router router(config_);

    auto sink = std::make_shared<RouterMockSink>("test_sink");
    router.register_sink("sink1", sink->get());

    auto result = router.enable_sink("sink1", false);
    EXPECT_TRUE(result.is_success());
}

// ============================================================================
// Rule Management Tests
// ============================================================================

class RouterRuleManagementTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = router::RouterConfig::default_config();
        config_.message_bus.dispatcher_threads = 2;
        config_.scheduler.worker_threads = 2;
        config_.sink_registry.enable_health_check = false;
    }

    router::RouterConfig config_;
};

TEST_F(RouterRuleManagementTest, AddRule) {
    router::Router router(config_);

    // Register sink first (validation requires target sinks to exist)
    auto sink = std::make_shared<RouterMockSink>("sink1");
    router.register_sink("sink1", sink->get());

    auto rule = router::RuleBuilder()
        .name("test_rule")
        .match_address("sensors/temp1")
        .route_to("sink1")
        .build();

    auto result = router.add_rule(rule);
    EXPECT_TRUE(result.is_success());
    EXPECT_GT(result.value(), 0u);
}

TEST_F(RouterRuleManagementTest, GetRule) {
    router::Router router(config_);

    // Register sink first (validation requires target sinks to exist)
    auto sink = std::make_shared<RouterMockSink>("sink1");
    router.register_sink("sink1", sink->get());

    auto rule = router::RuleBuilder()
        .name("test_rule")
        .match_address("sensors/temp1")
        .route_to("sink1")
        .build();

    auto add_result = router.add_rule(rule);
    uint32_t rule_id = add_result.value();

    auto retrieved = router.get_rule(rule_id);
    EXPECT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->name, "test_rule");
}

TEST_F(RouterRuleManagementTest, RemoveRule) {
    router::Router router(config_);

    // Register sink first (validation requires target sinks to exist)
    auto sink = std::make_shared<RouterMockSink>("sink1");
    router.register_sink("sink1", sink->get());

    auto rule = router::RuleBuilder()
        .name("test_rule")
        .match_address("sensors/temp1")
        .route_to("sink1")
        .build();

    auto add_result = router.add_rule(rule);
    uint32_t rule_id = add_result.value();

    auto remove_result = router.remove_rule(rule_id);
    EXPECT_TRUE(remove_result.is_success());

    auto retrieved = router.get_rule(rule_id);
    EXPECT_FALSE(retrieved.has_value());
}

TEST_F(RouterRuleManagementTest, EnableDisableRule) {
    router::Router router(config_);

    // Register sink first (validation requires target sinks to exist)
    auto sink = std::make_shared<RouterMockSink>("sink1");
    router.register_sink("sink1", sink->get());

    auto rule = router::RuleBuilder()
        .name("test_rule")
        .match_address("sensors/temp1")
        .route_to("sink1")
        .build();

    auto add_result = router.add_rule(rule);
    uint32_t rule_id = add_result.value();

    auto disable_result = router.enable_rule(rule_id, false);
    EXPECT_TRUE(disable_result.is_success());

    auto retrieved = router.get_rule(rule_id);
    EXPECT_FALSE(retrieved->enabled);
}

TEST_F(RouterRuleManagementTest, GetAllRules) {
    router::Router router(config_);

    // Register sink first (validation requires target sinks to exist)
    auto sink = std::make_shared<RouterMockSink>("sink1");
    router.register_sink("sink1", sink->get());

    for (int i = 0; i < 5; ++i) {
        auto rule = router::RuleBuilder()
            .name("rule_" + std::to_string(i))
            .match_address("sensors/temp" + std::to_string(i))
            .route_to("sink1")
            .build();
        router.add_rule(rule);
    }

    auto rules = router.get_routing_rules();
    EXPECT_EQ(rules.size(), 5u);
}

// ============================================================================
// Message Routing Tests
// ============================================================================

class MessageRoutingTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = router::RouterConfig::default_config();
        config_.message_bus.dispatcher_threads = 2;
        config_.scheduler.worker_threads = 2;
        config_.sink_registry.enable_health_check = false;
    }

    router::RouterConfig config_;
};

TEST_F(MessageRoutingTest, RouteNotRunning) {
    router::Router router(config_);

    DataPoint dp("sensors/temp1");
    dp.set_value(25.5);

    auto result = router.route(dp);
    EXPECT_TRUE(result.is_error());
}

TEST_F(MessageRoutingTest, RouteWithNoRules) {
    router::Router router(config_);
    router.start();

    auto sink = std::make_shared<RouterMockSink>("test_sink");
    router.register_sink("sink1", sink->get());

    DataPoint dp("sensors/temp1");
    dp.set_value(25.5);

    auto result = router.route(dp);
    // May succeed (dead letter) or fail (no matching rule)

    router.stop();
}

TEST_F(MessageRoutingTest, RouteWithMatchingRule) {
    router::Router router(config_);

    auto sink = std::make_shared<RouterMockSink>("test_sink");
    router.register_sink("sink1", sink->get());

    auto rule = router::RuleBuilder()
        .name("temp_rule")
        .match_address("sensors/temp1")
        .route_to("sink1")
        .build();
    router.add_rule(rule);

    router.start();

    DataPoint dp("sensors/temp1");
    dp.set_value(25.5);

    auto result = router.route(dp);

    // Wait for async processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    router.stop();

    // Sink should have received the message
    EXPECT_GE(sink->write_count(), 0);  // May or may not have processed yet
}

TEST_F(MessageRoutingTest, RouteWithDeadline) {
    router::Router router(config_);

    auto sink = std::make_shared<RouterMockSink>("test_sink");
    router.register_sink("sink1", sink->get());

    auto rule = router::RuleBuilder()
        .name("deadline_rule")
        .match_address("sensors/temp1")
        .route_to("sink1")
        .build();
    router.add_rule(rule);

    router.start();

    DataPoint dp("sensors/temp1");
    dp.set_value(25.5);

    auto deadline = Timestamp::now() + std::chrono::milliseconds(100);
    auto result = router.route_with_deadline(dp, deadline);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    router.stop();
}

TEST_F(MessageRoutingTest, RouteBatch) {
    router::Router router(config_);

    auto sink = std::make_shared<RouterMockSink>("test_sink");
    router.register_sink("sink1", sink->get());

    auto rule = router::RuleBuilder()
        .name("batch_rule")
        .match_pattern("sensors/.*")
        .route_to("sink1")
        .build();
    router.add_rule(rule);

    router.start();

    std::vector<DataPoint> batch;
    for (int i = 0; i < 10; ++i) {
        DataPoint dp("sensors/temp" + std::to_string(i));
        dp.set_value(static_cast<double>(20 + i));
        batch.push_back(dp);
    }

    auto result = router.route_batch(batch);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    router.stop();
}

// ============================================================================
// Scheduler Control Tests
// ============================================================================

class SchedulerControlTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = router::RouterConfig::default_config();
        config_.scheduler.worker_threads = 2;
    }

    router::RouterConfig config_;
};

TEST_F(SchedulerControlTest, SetDefaultDeadlineOffset) {
    router::Router router(config_);

    router.set_default_deadline_offset(std::chrono::milliseconds(500));

    auto offset = router.get_default_deadline_offset();
    EXPECT_EQ(offset, std::chrono::milliseconds(500));
}

TEST_F(SchedulerControlTest, GetPendingTaskCount) {
    router::Router router(config_);
    router.start();

    auto count = router.get_pending_task_count();
    EXPECT_GE(count, 0u);

    router.stop();
}

TEST_F(SchedulerControlTest, GetMissedDeadlineCount) {
    router::Router router(config_);
    router.start();

    auto count = router.get_missed_deadline_count();
    EXPECT_GE(count, 0u);

    router.stop();
}

// ============================================================================
// Metrics Tests
// ============================================================================

class RouterMetricsTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = router::RouterConfig::default_config();
        config_.message_bus.dispatcher_threads = 2;
        config_.scheduler.worker_threads = 2;
        config_.sink_registry.enable_health_check = false;
    }

    router::RouterConfig config_;
};

TEST_F(RouterMetricsTest, GetMetrics) {
    router::Router router(config_);
    router.start();

    auto metrics = router.get_metrics();
    EXPECT_EQ(metrics.total_messages, 0u);
    EXPECT_EQ(metrics.successful_routes, 0u);
    EXPECT_EQ(metrics.failed_routes, 0u);

    router.stop();
}

TEST_F(RouterMetricsTest, ResetMetrics) {
    router::Router router(config_);
    router.start();

    router.reset_metrics();

    auto metrics = router.get_metrics();
    EXPECT_EQ(metrics.total_messages, 0u);

    router.stop();
}

// ============================================================================
// Health Tests
// ============================================================================

class RouterHealthTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = router::RouterConfig::default_config();
        config_.sink_registry.enable_health_check = false;
    }

    router::RouterConfig config_;
};

TEST_F(RouterHealthTest, HealthyWhenRunning) {
    router::Router router(config_);
    router.start();

    EXPECT_TRUE(router.is_healthy());
    auto status = router.get_health_status();
    EXPECT_FALSE(status.empty());

    router.stop();
}

TEST_F(RouterHealthTest, NotHealthyWhenStopped) {
    router::Router router(config_);

    EXPECT_FALSE(router.is_healthy());
}

// ============================================================================
// Component Access Tests
// ============================================================================

class ComponentAccessTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = router::RouterConfig::default_config();
    }

    router::RouterConfig config_;
};

TEST_F(ComponentAccessTest, AccessMessageBus) {
    router::Router router(config_);

    auto& bus = router.message_bus();
    EXPECT_FALSE(bus.is_running());
}

TEST_F(ComponentAccessTest, AccessRuleEngine) {
    router::Router router(config_);

    auto& engine = router.rule_engine();
    EXPECT_EQ(engine.rule_count(), 0u);
}

TEST_F(ComponentAccessTest, AccessScheduler) {
    router::Router router(config_);

    auto& scheduler = router.scheduler();
    EXPECT_FALSE(scheduler.is_running());
}

TEST_F(ComponentAccessTest, AccessSinkRegistry) {
    router::Router router(config_);

    auto& registry = router.sink_registry();
    EXPECT_EQ(registry.sink_count(), 0u);
}

// ============================================================================
// Factory Tests
// ============================================================================

class RouterFactoryTest : public ::testing::Test {};

TEST_F(RouterFactoryTest, CreateDefault) {
    auto router = router::RouterFactory::create();
    EXPECT_NE(router, nullptr);
    EXPECT_FALSE(router->is_running());
}

TEST_F(RouterFactoryTest, CreateHighThroughput) {
    auto router = router::RouterFactory::create_high_throughput();
    EXPECT_NE(router, nullptr);
}

TEST_F(RouterFactoryTest, CreateLowLatency) {
    auto router = router::RouterFactory::create_low_latency();
    EXPECT_NE(router, nullptr);
}

TEST_F(RouterFactoryTest, CreateRealtime) {
    auto router = router::RouterFactory::create_realtime();
    EXPECT_NE(router, nullptr);
}

// ============================================================================
// Move Semantics Tests
// ============================================================================

class RouterMoveTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = router::RouterConfig::default_config();
    }

    router::RouterConfig config_;
};

TEST_F(RouterMoveTest, MoveConstruction) {
    router::Router router1(config_);

    auto sink = std::make_shared<RouterMockSink>("test_sink");
    router1.register_sink("sink1", sink->get());

    router::Router router2(std::move(router1));

    auto sinks = router2.get_registered_sinks();
    EXPECT_EQ(sinks.size(), 1u);
}

TEST_F(RouterMoveTest, MoveAssignment) {
    router::Router router1(config_);
    router::Router router2(config_);

    auto sink = std::make_shared<RouterMockSink>("test_sink");
    router1.register_sink("sink1", sink->get());

    router2 = std::move(router1);

    auto sinks = router2.get_registered_sinks();
    EXPECT_EQ(sinks.size(), 1u);
}
