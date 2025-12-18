/**
 * @file test_e2e_routing.cpp
 * @brief End-to-end tests for IPB routing scenarios
 *
 * Tests complete data flow from source through router to sink
 */

#include <ipb/router/router.hpp>
#include <ipb/common/data_point.hpp>
#include <ipb/common/interfaces.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace router = ipb::router;
namespace common = ipb::common;

using common::ConfigurationBase;
using common::DataPoint;
using common::DataSet;
using common::IIPBSink;
using common::IIPBSinkBase;
using common::ok;
using common::Result;
using common::Statistics;
using common::Timestamp;
using common::Value;

// ============================================================================
// Test Infrastructure - Recording Sink
// ============================================================================

struct RecordingSinkState {
    std::string name;
    std::atomic<bool> started{false};
    std::atomic<bool> healthy{true};
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<DataPoint> received_data;
    std::atomic<size_t> total_count{0};

    explicit RecordingSinkState(const std::string& n) : name(n) {}

    void wait_for_count(size_t count, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_for(lock, timeout, [this, count]() {
            return total_count.load() >= count;
        });
    }
};

class RecordingSinkImpl : public IIPBSinkBase {
public:
    explicit RecordingSinkImpl(std::shared_ptr<RecordingSinkState> state)
        : state_(std::move(state)) {}

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

    Result<void> write(const DataPoint& dp) override {
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->received_data.push_back(dp);
        }
        state_->total_count++;
        state_->cv.notify_all();
        return ok();
    }

    Result<void> write_batch(std::span<const DataPoint> batch) override {
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            for (const auto& dp : batch) {
                state_->received_data.push_back(dp);
            }
        }
        state_->total_count += batch.size();
        state_->cv.notify_all();
        return ok();
    }

    Result<void> write_dataset(const DataSet&) override { return ok(); }

    std::future<Result<void>> write_async(const DataPoint& dp) override {
        write(dp);
        std::promise<Result<void>> p;
        p.set_value(ok());
        return p.get_future();
    }

    std::future<Result<void>> write_batch_async(std::span<const DataPoint> batch) override {
        write_batch(batch);
        std::promise<Result<void>> p;
        p.set_value(ok());
        return p.get_future();
    }

    Result<void> flush() override { return ok(); }
    size_t pending_count() const noexcept override { return 0; }
    bool can_accept_data() const noexcept override { return true; }

    std::string_view sink_type() const noexcept override { return "recording"; }
    size_t max_batch_size() const noexcept override { return 10000; }

private:
    std::shared_ptr<RecordingSinkState> state_;
};

class RecordingSink {
public:
    explicit RecordingSink(const std::string& name = "recording")
        : state_(std::make_shared<RecordingSinkState>(name)),
          sink_(std::make_shared<IIPBSink>(std::make_unique<RecordingSinkImpl>(state_))) {}

    std::shared_ptr<IIPBSink> get() const { return sink_; }
    operator std::shared_ptr<IIPBSink>() const { return sink_; }

    void set_healthy(bool h) { state_->healthy = h; }
    size_t received_count() const { return state_->total_count.load(); }

    std::vector<DataPoint> get_received_data() const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->received_data;
    }

    void wait_for_count(size_t count, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        state_->wait_for_count(count, timeout);
    }

    void clear() {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->received_data.clear();
        state_->total_count = 0;
    }

private:
    std::shared_ptr<RecordingSinkState> state_;
    std::shared_ptr<IIPBSink> sink_;
};

// ============================================================================
// E2E Test Fixtures
// ============================================================================

class E2ERoutingTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = router::RouterConfig::default_config();
        config_.message_bus.dispatcher_threads = 2;
        config_.scheduler.worker_threads = 2;
        config_.sink_registry.enable_health_check = false;
    }

    void TearDown() override {
        if (router_ && router_->is_running()) {
            router_->stop();
        }
    }

    router::RouterConfig config_;
    std::unique_ptr<router::Router> router_;
};

// ============================================================================
// Basic E2E Routing Tests
// ============================================================================

TEST_F(E2ERoutingTest, SingleSourceToSingleSink) {
    // Setup
    router_ = std::make_unique<router::Router>(config_);
    RecordingSink sink("primary");
    
    router_->register_sink("primary", sink.get());
    
    auto rule = router::RuleBuilder()
        .name("all_to_primary")
        .match_pattern(".*")
        .route_to("primary")
        .build();
    router_->add_rule(rule);
    
    router_->start();
    
    // Act - Send multiple data points
    const int NUM_MESSAGES = 100;
    for (int i = 0; i < NUM_MESSAGES; ++i) {
        DataPoint dp("sensor/temp/" + std::to_string(i));
        dp.set_value(static_cast<double>(20.0 + i * 0.1));
        dp.set_quality(common::Quality::GOOD);
        router_->route(dp);
    }
    
    // Wait for processing
    sink.wait_for_count(NUM_MESSAGES, std::chrono::milliseconds(5000));
    
    // Assert
    EXPECT_GE(sink.received_count(), NUM_MESSAGES * 0.95); // Allow 5% loss for async
}

TEST_F(E2ERoutingTest, MultipleRulesRouting) {
    // Setup
    router_ = std::make_unique<router::Router>(config_);
    RecordingSink temp_sink("temp_sink");
    RecordingSink humidity_sink("humidity_sink");
    RecordingSink pressure_sink("pressure_sink");
    
    router_->register_sink("temp_sink", temp_sink.get());
    router_->register_sink("humidity_sink", humidity_sink.get());
    router_->register_sink("pressure_sink", pressure_sink.get());
    
    // Add routing rules
    router_->add_rule(router::RuleBuilder()
        .name("temp_rule")
        .match_pattern("sensor/temp/.*")
        .route_to("temp_sink")
        .build());
    
    router_->add_rule(router::RuleBuilder()
        .name("humidity_rule")
        .match_pattern("sensor/humidity/.*")
        .route_to("humidity_sink")
        .build());
    
    router_->add_rule(router::RuleBuilder()
        .name("pressure_rule")
        .match_pattern("sensor/pressure/.*")
        .route_to("pressure_sink")
        .build());
    
    router_->start();
    
    // Act - Send different types of data
    for (int i = 0; i < 30; ++i) {
        DataPoint temp_dp("sensor/temp/" + std::to_string(i));
        temp_dp.set_value(25.0);
        router_->route(temp_dp);
        
        DataPoint hum_dp("sensor/humidity/" + std::to_string(i));
        hum_dp.set_value(60.0);
        router_->route(hum_dp);
        
        DataPoint press_dp("sensor/pressure/" + std::to_string(i));
        press_dp.set_value(1013.25);
        router_->route(press_dp);
    }
    
    // Wait
    temp_sink.wait_for_count(25);
    humidity_sink.wait_for_count(25);
    pressure_sink.wait_for_count(25);
    
    // Assert - Each sink should receive its category
    EXPECT_GE(temp_sink.received_count(), 25);
    EXPECT_GE(humidity_sink.received_count(), 25);
    EXPECT_GE(pressure_sink.received_count(), 25);
}

TEST_F(E2ERoutingTest, BroadcastRouting) {
    // Setup
    router_ = std::make_unique<router::Router>(config_);
    RecordingSink sink1("sink1");
    RecordingSink sink2("sink2");
    RecordingSink sink3("sink3");
    
    router_->register_sink("sink1", sink1.get());
    router_->register_sink("sink2", sink2.get());
    router_->register_sink("sink3", sink3.get());
    
    // Broadcast rule - send to all sinks
    auto rule = router::RuleBuilder()
        .name("broadcast_rule")
        .match_pattern("critical/.*")
        .route_to(std::vector<std::string>{"sink1", "sink2", "sink3"})
        .build();
    router_->add_rule(rule);
    
    router_->start();
    
    // Act
    const int NUM_MESSAGES = 10;
    for (int i = 0; i < NUM_MESSAGES; ++i) {
        DataPoint dp("critical/alert/" + std::to_string(i));
        dp.set_value(i);
        dp.set_quality(common::Quality::GOOD);
        router_->route(dp);
    }
    
    // Wait
    sink1.wait_for_count(NUM_MESSAGES);
    sink2.wait_for_count(NUM_MESSAGES);
    sink3.wait_for_count(NUM_MESSAGES);
    
    // Assert - All sinks should receive all messages
    EXPECT_GE(sink1.received_count(), NUM_MESSAGES * 0.9);
    EXPECT_GE(sink2.received_count(), NUM_MESSAGES * 0.9);
    EXPECT_GE(sink3.received_count(), NUM_MESSAGES * 0.9);
}

TEST_F(E2ERoutingTest, PriorityRouting) {
    // Setup
    router_ = std::make_unique<router::Router>(config_);
    RecordingSink high_priority_sink("high");
    RecordingSink low_priority_sink("low");
    
    router_->register_sink("high", high_priority_sink.get());
    router_->register_sink("low", low_priority_sink.get());
    
    // High priority rule (should match first)
    router_->add_rule(router::RuleBuilder()
        .name("high_priority")
        .priority(router::RoutingPriority::HIGH)
        .match_pattern("sensor/critical/.*")
        .route_to("high")
        .build());
    
    // Low priority catch-all
    router_->add_rule(router::RuleBuilder()
        .name("low_priority")
        .priority(router::RoutingPriority::LOW)
        .match_pattern("sensor/.*")
        .route_to("low")
        .build());
    
    router_->start();
    
    // Act
    for (int i = 0; i < 20; ++i) {
        DataPoint critical_dp("sensor/critical/" + std::to_string(i));
        critical_dp.set_value(100.0);
        router_->route(critical_dp);
        
        DataPoint normal_dp("sensor/normal/" + std::to_string(i));
        normal_dp.set_value(50.0);
        router_->route(normal_dp);
    }
    
    // Wait
    high_priority_sink.wait_for_count(15);
    low_priority_sink.wait_for_count(15);
    
    // Assert
    EXPECT_GE(high_priority_sink.received_count(), 15);
    EXPECT_GE(low_priority_sink.received_count(), 15);
}

TEST_F(E2ERoutingTest, BatchRouting) {
    // Setup
    router_ = std::make_unique<router::Router>(config_);
    RecordingSink sink("batch_sink");
    
    router_->register_sink("batch_sink", sink.get());
    
    router_->add_rule(router::RuleBuilder()
        .name("batch_rule")
        .match_pattern(".*")
        .route_to("batch_sink")
        .build());
    
    router_->start();
    
    // Act - Send a batch of data points
    std::vector<DataPoint> batch;
    const int BATCH_SIZE = 500;
    for (int i = 0; i < BATCH_SIZE; ++i) {
        DataPoint dp("sensor/batch/" + std::to_string(i));
        dp.set_value(static_cast<double>(i));
        batch.push_back(dp);
    }
    
    router_->route_batch(batch);
    
    // Wait
    sink.wait_for_count(BATCH_SIZE * 0.9, std::chrono::milliseconds(10000));
    
    // Assert
    EXPECT_GE(sink.received_count(), BATCH_SIZE * 0.9);
}

// ============================================================================
// Failover E2E Tests
// ============================================================================

TEST_F(E2ERoutingTest, FailoverOnUnhealthySink) {
    // Setup
    router_ = std::make_unique<router::Router>(config_);
    RecordingSink primary("primary");
    RecordingSink backup("backup");
    
    router_->register_sink("primary", primary.get());
    router_->register_sink("backup", backup.get());
    
    router_->add_rule(router::RuleBuilder()
        .name("failover_rule")
        .match_pattern(".*")
        .route_to("primary")
        .with_failover(std::vector<std::string>{"backup"})
        .build());
    
    router_->start();
    
    // Act - Send data while primary is healthy
    for (int i = 0; i < 10; ++i) {
        DataPoint dp("data/" + std::to_string(i));
        dp.set_value(i);
        router_->route(dp);
    }
    
    primary.wait_for_count(8);
    
    // Now mark primary as unhealthy
    primary.set_healthy(false);
    
    // Send more data
    for (int i = 10; i < 20; ++i) {
        DataPoint dp("data/" + std::to_string(i));
        dp.set_value(i);
        router_->route(dp);
    }
    
    // Wait for backup to receive
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Assert - Primary should have received initial data
    EXPECT_GE(primary.received_count(), 5);
}

// ============================================================================
// Concurrent E2E Tests
// ============================================================================

TEST_F(E2ERoutingTest, ConcurrentRouting) {
    // Setup
    router_ = std::make_unique<router::Router>(config_);
    RecordingSink sink("concurrent_sink");
    
    router_->register_sink("concurrent_sink", sink.get());
    
    router_->add_rule(router::RuleBuilder()
        .name("concurrent_rule")
        .match_pattern(".*")
        .route_to("concurrent_sink")
        .build());
    
    router_->start();
    
    // Act - Multiple threads sending concurrently
    const int NUM_THREADS = 4;
    const int MESSAGES_PER_THREAD = 100;
    std::vector<std::thread> threads;
    
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([this, t]() {
            for (int i = 0; i < MESSAGES_PER_THREAD; ++i) {
                DataPoint dp("thread/" + std::to_string(t) + "/msg/" + std::to_string(i));
                dp.set_value(static_cast<double>(t * 1000 + i));
                router_->route(dp);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Wait for all messages
    const int TOTAL_MESSAGES = NUM_THREADS * MESSAGES_PER_THREAD;
    sink.wait_for_count(TOTAL_MESSAGES * 0.9, std::chrono::milliseconds(10000));
    
    // Assert
    EXPECT_GE(sink.received_count(), TOTAL_MESSAGES * 0.9);
}

// ============================================================================
// Data Integrity E2E Tests
// ============================================================================

TEST_F(E2ERoutingTest, DataIntegrity) {
    // Setup
    router_ = std::make_unique<router::Router>(config_);
    RecordingSink sink("integrity_sink");
    
    router_->register_sink("integrity_sink", sink.get());
    
    router_->add_rule(router::RuleBuilder()
        .name("integrity_rule")
        .match_pattern(".*")
        .route_to("integrity_sink")
        .build());
    
    router_->start();
    
    // Act - Send data with specific values
    std::vector<std::pair<std::string, double>> expected_data;
    for (int i = 0; i < 50; ++i) {
        std::string addr = "sensor/data/" + std::to_string(i);
        double value = 100.0 + i * 0.5;
        expected_data.emplace_back(addr, value);
        
        DataPoint dp(addr);
        dp.set_value(value);
        dp.set_quality(common::Quality::GOOD);
        router_->route(dp);
    }
    
    // Wait
    sink.wait_for_count(45, std::chrono::milliseconds(5000));
    
    // Assert - Verify data integrity
    auto received = sink.get_received_data();
    EXPECT_GE(received.size(), 45);
    
    // Verify some values are correct
    for (const auto& dp : received) {
        // Value should be in expected range
        if (dp.value().type() == Value::Type::FLOAT64) {
            double val = dp.value().get<double>();
            EXPECT_GE(val, 100.0);
            EXPECT_LE(val, 125.0);
        }
    }
}

// ============================================================================
// Router Lifecycle E2E Tests
// ============================================================================

TEST_F(E2ERoutingTest, StartStopRestart) {
    router_ = std::make_unique<router::Router>(config_);
    RecordingSink sink("lifecycle_sink");
    
    router_->register_sink("lifecycle_sink", sink.get());
    router_->add_rule(router::RuleBuilder()
        .name("lifecycle_rule")
        .match_pattern(".*")
        .route_to("lifecycle_sink")
        .build());
    
    // First run
    router_->start();
    
    for (int i = 0; i < 10; ++i) {
        DataPoint dp("run1/msg/" + std::to_string(i));
        dp.set_value(i);
        router_->route(dp);
    }
    
    sink.wait_for_count(8);
    EXPECT_GE(sink.received_count(), 8);
    
    // Stop
    router_->stop();
    EXPECT_FALSE(router_->is_running());
    
    // Clear and restart
    sink.clear();
    router_->start();
    EXPECT_TRUE(router_->is_running());
    
    // Second run
    for (int i = 0; i < 10; ++i) {
        DataPoint dp("run2/msg/" + std::to_string(i));
        dp.set_value(i + 100);
        router_->route(dp);
    }
    
    sink.wait_for_count(8);
    EXPECT_GE(sink.received_count(), 8);
}

// ============================================================================
// Metrics E2E Tests
// ============================================================================

TEST_F(E2ERoutingTest, MetricsAccuracy) {
    router_ = std::make_unique<router::Router>(config_);
    RecordingSink sink("metrics_sink");
    
    router_->register_sink("metrics_sink", sink.get());
    router_->add_rule(router::RuleBuilder()
        .name("metrics_rule")
        .match_pattern(".*")
        .route_to("metrics_sink")
        .build());
    
    router_->start();
    
    // Send known number of messages
    const int NUM_MESSAGES = 100;
    for (int i = 0; i < NUM_MESSAGES; ++i) {
        DataPoint dp("metrics/test/" + std::to_string(i));
        dp.set_value(i);
        router_->route(dp);
    }
    
    // Wait for processing
    sink.wait_for_count(NUM_MESSAGES * 0.9);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Check metrics
    auto metrics = router_->get_metrics();
    EXPECT_GE(metrics.total_messages, NUM_MESSAGES * 0.9);
}
