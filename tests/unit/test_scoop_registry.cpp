/**
 * @file test_scoop_registry.cpp
 * @brief Unit tests for IPB ScoopRegistry
 *
 * Tests coverage for:
 * - ReadStrategy, ScoopHealth enums
 * - ScoopInfo: Scoop metadata and statistics
 * - ScoopSelectionResult: Selection results
 * - ScoopRegistryStats: Registry statistics
 * - ScoopRegistryConfig: Configuration
 * - AggregatedSubscription: Multi-source subscriptions
 * - ScoopRegistry: Scoop management and read strategies
 */

#include <gtest/gtest.h>
#include <ipb/core/scoop_registry/scoop_registry.hpp>
#include <atomic>
#include <future>
#include <memory>
#include <thread>

using namespace ipb::core;
using namespace ipb::common;

// ============================================================================
// Mock Scoop for Testing
// ============================================================================

// Shared state for mock scoop (allows tracking reads even through type-erased wrapper)
struct MockScoopState {
    std::string name;
    std::atomic<bool> started{false};
    std::atomic<bool> healthy{true};
    std::atomic<bool> connected{false};
    std::atomic<int> read_count{0};
    std::atomic<bool> should_fail{false};
    std::vector<std::string> addresses;
    mutable std::mutex addresses_mutex;

    explicit MockScoopState(const std::string& n) : name(n) {}
};

// Mock scoop implementation that inherits from IProtocolSourceBase
class MockScoopImpl : public IProtocolSourceBase {
public:
    explicit MockScoopImpl(std::shared_ptr<MockScoopState> state) : state_(std::move(state)) {}

    // IIPBComponent interface
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

    // IProtocolSourceBase interface
    Result<DataSet> read() override {
        state_->read_count++;
        if (state_->should_fail) {
            return err<DataSet>(ErrorCode::UNKNOWN_ERROR, "Simulated failure");
        }
        DataSet data;
        DataPoint dp("test/address");
        dp.set_value(42.0);
        data.push_back(std::move(dp));
        return Result<DataSet>(std::move(data));
    }

    Result<DataSet> read_async() override {
        return read();
    }

    Result<void> subscribe(DataCallback data_cb, ErrorCallback) override {
        // Store callback for potential triggering
        (void)data_cb;
        return ok();
    }

    Result<void> unsubscribe() override {
        return ok();
    }

    Result<void> add_address(std::string_view address) override {
        std::lock_guard lock(state_->addresses_mutex);
        state_->addresses.push_back(std::string(address));
        return ok();
    }

    Result<void> remove_address(std::string_view address) override {
        std::lock_guard lock(state_->addresses_mutex);
        auto it = std::find(state_->addresses.begin(), state_->addresses.end(), address);
        if (it != state_->addresses.end()) {
            state_->addresses.erase(it);
        }
        return ok();
    }

    std::vector<std::string> get_addresses() const override {
        std::lock_guard lock(state_->addresses_mutex);
        return state_->addresses;
    }

    // Connection interface
    Result<void> connect() override {
        state_->connected = true;
        return ok();
    }

    Result<void> disconnect() override {
        state_->connected = false;
        return ok();
    }

    bool is_connected() const noexcept override {
        return state_->connected;
    }

    // Protocol interface
    std::string_view protocol_name() const noexcept override { return "mock"; }
    uint16_t protocol_id() const noexcept override { return 999; }

private:
    std::shared_ptr<MockScoopState> state_;
};

// Test helper that provides both IProtocolSource wrapper and access to mock state
class MockScoop {
public:
    explicit MockScoop(const std::string& name = "mock")
        : state_(std::make_shared<MockScoopState>(name))
        , scoop_(std::shared_ptr<IProtocolSource>(
              new IProtocolSource(std::make_unique<MockScoopImpl>(state_)))) {}

    // Get the IProtocolSource to pass to registry
    std::shared_ptr<IProtocolSource> get() const { return scoop_; }

    // Access mock state for test assertions
    int read_count() const { return state_->read_count.load(); }
    void set_healthy(bool h) { state_->healthy = h; }
    void set_should_fail(bool f) { state_->should_fail = f; }
    bool is_started() const { return state_->started; }
    bool is_connected() const { return state_->connected; }

private:
    std::shared_ptr<MockScoopState> state_;
    std::shared_ptr<IProtocolSource> scoop_;
};

// ============================================================================
// ReadStrategy Tests
// ============================================================================

class ReadStrategyTest : public ::testing::Test {};

TEST_F(ReadStrategyTest, StrategyValues) {
    EXPECT_EQ(static_cast<uint8_t>(ReadStrategy::PRIMARY_ONLY), 0);
    EXPECT_EQ(static_cast<uint8_t>(ReadStrategy::FAILOVER), 1);
    EXPECT_EQ(static_cast<uint8_t>(ReadStrategy::ROUND_ROBIN), 2);
    EXPECT_EQ(static_cast<uint8_t>(ReadStrategy::BROADCAST_MERGE), 3);
    EXPECT_EQ(static_cast<uint8_t>(ReadStrategy::FASTEST_RESPONSE), 4);
    EXPECT_EQ(static_cast<uint8_t>(ReadStrategy::QUORUM), 5);
}

// ============================================================================
// ScoopHealth Tests
// ============================================================================

class ScoopHealthTest : public ::testing::Test {};

TEST_F(ScoopHealthTest, HealthValues) {
    EXPECT_EQ(static_cast<uint8_t>(ScoopHealth::HEALTHY), 0);
    EXPECT_EQ(static_cast<uint8_t>(ScoopHealth::DEGRADED), 1);
    EXPECT_EQ(static_cast<uint8_t>(ScoopHealth::UNHEALTHY), 2);
    EXPECT_EQ(static_cast<uint8_t>(ScoopHealth::DISCONNECTED), 3);
    EXPECT_EQ(static_cast<uint8_t>(ScoopHealth::UNKNOWN), 4);
}

// ============================================================================
// ScoopInfo Tests
// ============================================================================

class ScoopInfoTest : public ::testing::Test {};

TEST_F(ScoopInfoTest, DefaultConstruction) {
    ScoopInfo info;
    EXPECT_TRUE(info.id.empty());
    EXPECT_TRUE(info.type.empty());
    EXPECT_EQ(info.priority, 0u);
    EXPECT_TRUE(info.enabled);
    EXPECT_FALSE(info.is_primary);
    EXPECT_EQ(info.health, ScoopHealth::UNKNOWN);
    EXPECT_FALSE(info.connected);
}

TEST_F(ScoopInfoTest, SuccessRate) {
    ScoopInfo info;

    // No reads yet
    EXPECT_DOUBLE_EQ(info.success_rate(), 100.0);

    // 90% success rate
    info.reads_successful.store(90);
    info.reads_failed.store(10);
    EXPECT_DOUBLE_EQ(info.success_rate(), 90.0);
}

TEST_F(ScoopInfoTest, AverageLatency) {
    ScoopInfo info;

    // No reads yet
    EXPECT_DOUBLE_EQ(info.avg_latency_us(), 0.0);

    // With some reads
    info.reads_successful.store(100);
    info.total_latency_ns.store(1000000);  // 1ms total
    EXPECT_DOUBLE_EQ(info.avg_latency_us(), 10.0);  // 10us average
}

TEST_F(ScoopInfoTest, CopyConstruction) {
    ScoopInfo original;
    original.id = "test";
    original.type = "mock";
    original.priority = 5;
    original.reads_successful.store(100);

    ScoopInfo copy(original);
    EXPECT_EQ(copy.id, "test");
    EXPECT_EQ(copy.type, "mock");
    EXPECT_EQ(copy.priority, 5u);
    EXPECT_EQ(copy.reads_successful.load(), 100u);
}

TEST_F(ScoopInfoTest, MoveConstruction) {
    ScoopInfo original;
    original.id = "test";
    original.type = "mock";
    original.reads_successful.store(100);

    ScoopInfo moved(std::move(original));
    EXPECT_EQ(moved.id, "test");
    EXPECT_EQ(moved.reads_successful.load(), 100u);
}

// ============================================================================
// ScoopSelectionResult Tests
// ============================================================================

class ScoopSelectionResultTest : public ::testing::Test {};

TEST_F(ScoopSelectionResultTest, DefaultConstruction) {
    ScoopSelectionResult result;
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.selected_scoop_ids.empty());
    EXPECT_TRUE(result.error_message.empty());
}

TEST_F(ScoopSelectionResultTest, BoolConversion) {
    ScoopSelectionResult result;
    EXPECT_FALSE(result);

    result.success = true;
    EXPECT_TRUE(result);
}

// ============================================================================
// ScoopRegistryStats Tests
// ============================================================================

class ScoopRegistryStatsTest : public ::testing::Test {};

TEST_F(ScoopRegistryStatsTest, DefaultValues) {
    ScoopRegistryStats stats;
    EXPECT_EQ(stats.total_reads.load(), 0u);
    EXPECT_EQ(stats.successful_reads.load(), 0u);
    EXPECT_EQ(stats.failed_reads.load(), 0u);
    EXPECT_EQ(stats.failover_events.load(), 0u);
    EXPECT_EQ(stats.active_scoops.load(), 0u);
    EXPECT_EQ(stats.active_subscriptions.load(), 0u);
}

TEST_F(ScoopRegistryStatsTest, CopyConstruction) {
    ScoopRegistryStats original;
    original.total_reads.store(100);
    original.failover_events.store(5);

    ScoopRegistryStats copy(original);
    EXPECT_EQ(copy.total_reads.load(), 100u);
    EXPECT_EQ(copy.failover_events.load(), 5u);
}

TEST_F(ScoopRegistryStatsTest, Reset) {
    ScoopRegistryStats stats;
    stats.total_reads.store(100);
    stats.successful_reads.store(90);
    stats.failed_reads.store(10);
    stats.failover_events.store(5);

    stats.reset();

    EXPECT_EQ(stats.total_reads.load(), 0u);
    EXPECT_EQ(stats.successful_reads.load(), 0u);
    EXPECT_EQ(stats.failed_reads.load(), 0u);
    EXPECT_EQ(stats.failover_events.load(), 0u);
}

// ============================================================================
// ScoopRegistryConfig Tests
// ============================================================================

class ScoopRegistryConfigTest : public ::testing::Test {};

TEST_F(ScoopRegistryConfigTest, DefaultValues) {
    ScoopRegistryConfig config;
    EXPECT_EQ(config.default_strategy, ReadStrategy::FAILOVER);
    EXPECT_TRUE(config.enable_health_check);
    EXPECT_EQ(config.health_check_interval, std::chrono::milliseconds(5000));
    EXPECT_EQ(config.unhealthy_threshold, 3u);
    EXPECT_TRUE(config.enable_auto_reconnect);
    EXPECT_EQ(config.reconnect_interval, std::chrono::milliseconds(10000));
    EXPECT_TRUE(config.enable_failover);
    EXPECT_EQ(config.quorum_size, 2u);
    EXPECT_EQ(config.read_timeout, std::chrono::milliseconds(5000));
}

// ============================================================================
// ScoopRegistry Tests
// ============================================================================

class ScoopRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.enable_health_check = false;    // Disable for faster tests
        config_.enable_auto_reconnect = false;  // Disable for predictable tests
    }

    ScoopRegistryConfig config_;
};

TEST_F(ScoopRegistryTest, DefaultConstruction) {
    ScoopRegistry registry;
    EXPECT_FALSE(registry.is_running());
    EXPECT_EQ(registry.scoop_count(), 0u);
}

TEST_F(ScoopRegistryTest, ConfiguredConstruction) {
    ScoopRegistryConfig custom_config;
    custom_config.default_strategy = ReadStrategy::ROUND_ROBIN;
    custom_config.unhealthy_threshold = 5;

    ScoopRegistry registry(custom_config);

    EXPECT_EQ(registry.config().default_strategy, ReadStrategy::ROUND_ROBIN);
    EXPECT_EQ(registry.config().unhealthy_threshold, 5u);
}

TEST_F(ScoopRegistryTest, StartStop) {
    ScoopRegistry registry(config_);

    EXPECT_FALSE(registry.is_running());
    EXPECT_TRUE(registry.start());
    EXPECT_TRUE(registry.is_running());

    registry.stop();
    EXPECT_FALSE(registry.is_running());
}

TEST_F(ScoopRegistryTest, RegisterScoop) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test_scoop");
    bool registered = registry.register_scoop("scoop1", scoop->get());

    EXPECT_TRUE(registered);
    EXPECT_EQ(registry.scoop_count(), 1u);
    EXPECT_TRUE(registry.has_scoop("scoop1"));
}

TEST_F(ScoopRegistryTest, RegisterScoopWithPrimary) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test_scoop");
    bool registered = registry.register_scoop("scoop1", scoop->get(), true);

    EXPECT_TRUE(registered);

    auto info = registry.get_scoop_info("scoop1");
    EXPECT_TRUE(info.has_value());
    EXPECT_TRUE(info->is_primary);
}

TEST_F(ScoopRegistryTest, RegisterScoopWithPriority) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test_scoop");
    bool registered = registry.register_scoop("scoop1", scoop->get(), false, 10);

    EXPECT_TRUE(registered);

    auto info = registry.get_scoop_info("scoop1");
    EXPECT_TRUE(info.has_value());
    EXPECT_EQ(info->priority, 10u);
}

TEST_F(ScoopRegistryTest, RegisterDuplicateScoop) {
    ScoopRegistry registry(config_);

    auto scoop1 = std::make_shared<MockScoop>("scoop1");
    auto scoop2 = std::make_shared<MockScoop>("scoop2");

    EXPECT_TRUE(registry.register_scoop("scoop1", scoop1->get()));
    EXPECT_FALSE(registry.register_scoop("scoop1", scoop2->get()));  // Duplicate

    EXPECT_EQ(registry.scoop_count(), 1u);
}

TEST_F(ScoopRegistryTest, UnregisterScoop) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test_scoop");
    registry.register_scoop("scoop1", scoop->get());

    EXPECT_TRUE(registry.unregister_scoop("scoop1"));
    EXPECT_FALSE(registry.has_scoop("scoop1"));
    EXPECT_EQ(registry.scoop_count(), 0u);
}

TEST_F(ScoopRegistryTest, UnregisterNonexistent) {
    ScoopRegistry registry(config_);

    EXPECT_FALSE(registry.unregister_scoop("nonexistent"));
}

TEST_F(ScoopRegistryTest, GetScoop) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test_scoop");
    registry.register_scoop("scoop1", scoop->get());

    auto retrieved = registry.get_scoop("scoop1");
    EXPECT_NE(retrieved, nullptr);
}

TEST_F(ScoopRegistryTest, GetNonexistentScoop) {
    ScoopRegistry registry(config_);

    auto retrieved = registry.get_scoop("nonexistent");
    EXPECT_EQ(retrieved, nullptr);
}

TEST_F(ScoopRegistryTest, GetScoopIds) {
    ScoopRegistry registry(config_);

    auto scoop1 = std::make_shared<MockScoop>("scoop1");
    auto scoop2 = std::make_shared<MockScoop>("scoop2");
    auto scoop3 = std::make_shared<MockScoop>("scoop3");

    registry.register_scoop("scoop1", scoop1->get());
    registry.register_scoop("scoop2", scoop2->get());
    registry.register_scoop("scoop3", scoop3->get());

    auto ids = registry.get_scoop_ids();
    EXPECT_EQ(ids.size(), 3u);
}

// ============================================================================
// Scoop Configuration Tests
// ============================================================================

class ScoopConfigurationTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.enable_health_check = false;
        config_.enable_auto_reconnect = false;
    }

    ScoopRegistryConfig config_;
};

TEST_F(ScoopConfigurationTest, SetScoopEnabled) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test");
    registry.register_scoop("scoop1", scoop->get());

    EXPECT_TRUE(registry.set_scoop_enabled("scoop1", false));

    auto info = registry.get_scoop_info("scoop1");
    EXPECT_FALSE(info->enabled);
}

TEST_F(ScoopConfigurationTest, SetScoopPrimary) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test");
    registry.register_scoop("scoop1", scoop->get(), false);

    EXPECT_TRUE(registry.set_scoop_primary("scoop1", true));

    auto info = registry.get_scoop_info("scoop1");
    EXPECT_TRUE(info->is_primary);
}

TEST_F(ScoopConfigurationTest, SetScoopPriority) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test");
    registry.register_scoop("scoop1", scoop->get());

    EXPECT_TRUE(registry.set_scoop_priority("scoop1", 100));

    auto info = registry.get_scoop_info("scoop1");
    EXPECT_EQ(info->priority, 100u);
}

TEST_F(ScoopConfigurationTest, SetConfigNonexistent) {
    ScoopRegistry registry(config_);

    EXPECT_FALSE(registry.set_scoop_enabled("nonexistent", false));
    EXPECT_FALSE(registry.set_scoop_primary("nonexistent", true));
    EXPECT_FALSE(registry.set_scoop_priority("nonexistent", 100));
}

// ============================================================================
// Scoop Selection Tests
// ============================================================================

class ScoopSelectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.enable_health_check = false;
        config_.enable_auto_reconnect = false;
    }

    ScoopRegistryConfig config_;
};

TEST_F(ScoopSelectionTest, SelectPrimaryOnly) {
    ScoopRegistry registry(config_);

    auto scoop1 = std::make_shared<MockScoop>("scoop1");
    auto scoop2 = std::make_shared<MockScoop>("scoop2");

    registry.register_scoop("scoop1", scoop1->get(), true);   // Primary
    registry.register_scoop("scoop2", scoop2->get(), false);  // Backup

    auto result = registry.select_scoop({"scoop1", "scoop2"}, ReadStrategy::PRIMARY_ONLY);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.selected_scoop_ids.size(), 1u);
    EXPECT_EQ(result.selected_scoop_ids[0], "scoop1");
}

TEST_F(ScoopSelectionTest, SelectRoundRobin) {
    ScoopRegistry registry(config_);

    auto scoop1 = std::make_shared<MockScoop>("scoop1");
    auto scoop2 = std::make_shared<MockScoop>("scoop2");

    registry.register_scoop("scoop1", scoop1->get());
    registry.register_scoop("scoop2", scoop2->get());

    // Multiple selections should distribute across scoops
    std::set<std::string> selected_ids;
    for (int i = 0; i < 10; ++i) {
        auto result = registry.select_scoop({"scoop1", "scoop2"}, ReadStrategy::ROUND_ROBIN);
        EXPECT_TRUE(result.success);
        selected_ids.insert(result.selected_scoop_ids[0]);
    }

    // Both scoops should have been selected at some point
    EXPECT_EQ(selected_ids.size(), 2u);
}

TEST_F(ScoopSelectionTest, SelectBroadcast) {
    ScoopRegistry registry(config_);

    auto scoop1 = std::make_shared<MockScoop>("scoop1");
    auto scoop2 = std::make_shared<MockScoop>("scoop2");
    auto scoop3 = std::make_shared<MockScoop>("scoop3");

    registry.register_scoop("scoop1", scoop1->get());
    registry.register_scoop("scoop2", scoop2->get());
    registry.register_scoop("scoop3", scoop3->get());

    auto result = registry.select_scoop(
        {"scoop1", "scoop2", "scoop3"}, ReadStrategy::BROADCAST_MERGE);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.selected_scoop_ids.size(), 3u);
}

TEST_F(ScoopSelectionTest, SelectNoHealthyScoops) {
    ScoopRegistry registry(config_);

    // No scoops registered
    auto result = registry.select_scoop({"scoop1", "scoop2"}, ReadStrategy::FAILOVER);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

// ============================================================================
// Data Reading Tests
// ============================================================================

class DataReadingTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.enable_health_check = false;
        config_.enable_auto_reconnect = false;
    }

    ScoopRegistryConfig config_;
};

TEST_F(DataReadingTest, ReadFromScoop) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test_scoop");
    registry.register_scoop("scoop1", scoop->get());

    auto result = registry.read_from_scoop("scoop1");

    EXPECT_TRUE(result.is_success());
    EXPECT_GT(result.value().size(), 0u);
    EXPECT_EQ(scoop->read_count(), 1);
}

TEST_F(DataReadingTest, ReadFromNonexistentScoop) {
    ScoopRegistry registry(config_);

    auto result = registry.read_from_scoop("nonexistent");

    EXPECT_FALSE(result.is_success());
}

TEST_F(DataReadingTest, ReadMerged) {
    ScoopRegistry registry(config_);

    auto scoop1 = std::make_shared<MockScoop>("scoop1");
    auto scoop2 = std::make_shared<MockScoop>("scoop2");

    registry.register_scoop("scoop1", scoop1->get());
    registry.register_scoop("scoop2", scoop2->get());

    auto result = registry.read_merged({"scoop1", "scoop2"});

    EXPECT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 2u);  // One DataPoint from each scoop
    EXPECT_EQ(scoop1->read_count(), 1);
    EXPECT_EQ(scoop2->read_count(), 1);
}

TEST_F(DataReadingTest, ReadWithFailover) {
    ScoopRegistry registry(config_);

    auto scoop1 = std::make_shared<MockScoop>("scoop1");
    auto scoop2 = std::make_shared<MockScoop>("scoop2");

    registry.register_scoop("scoop1", scoop1->get(), true);   // Primary
    registry.register_scoop("scoop2", scoop2->get(), false);  // Backup

    // Without connected/healthy scoops, read_from uses first available
    auto result = registry.read_from({"scoop1", "scoop2"}, ReadStrategy::FAILOVER);

    // Should succeed with either scoop
    EXPECT_TRUE(result.is_success());
    EXPECT_GT(scoop1->read_count() + scoop2->read_count(), 0);
}

// ============================================================================
// Connection Management Tests
// ============================================================================

class ConnectionManagementTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.enable_health_check = false;
        config_.enable_auto_reconnect = false;
    }

    ScoopRegistryConfig config_;
};

TEST_F(ConnectionManagementTest, ConnectScoop) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test");
    registry.register_scoop("scoop1", scoop->get());

    auto result = registry.connect_scoop("scoop1");

    EXPECT_TRUE(result.is_success());
    EXPECT_TRUE(scoop->is_connected());
}

TEST_F(ConnectionManagementTest, DisconnectScoop) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test");
    registry.register_scoop("scoop1", scoop->get());
    registry.connect_scoop("scoop1");

    auto result = registry.disconnect_scoop("scoop1");

    EXPECT_TRUE(result.is_success());
    EXPECT_FALSE(scoop->is_connected());
}

TEST_F(ConnectionManagementTest, ConnectNonexistent) {
    ScoopRegistry registry(config_);

    auto result = registry.connect_scoop("nonexistent");
    EXPECT_FALSE(result.is_success());
}

TEST_F(ConnectionManagementTest, GetConnectedScoops) {
    ScoopRegistry registry(config_);

    auto scoop1 = std::make_shared<MockScoop>("scoop1");
    auto scoop2 = std::make_shared<MockScoop>("scoop2");

    registry.register_scoop("scoop1", scoop1->get());
    registry.register_scoop("scoop2", scoop2->get());

    registry.connect_scoop("scoop1");

    auto connected = registry.get_connected_scoops();
    EXPECT_EQ(connected.size(), 1u);
    EXPECT_EQ(connected[0], "scoop1");
}

TEST_F(ConnectionManagementTest, ConnectAll) {
    ScoopRegistry registry(config_);

    auto scoop1 = std::make_shared<MockScoop>("scoop1");
    auto scoop2 = std::make_shared<MockScoop>("scoop2");

    registry.register_scoop("scoop1", scoop1->get());
    registry.register_scoop("scoop2", scoop2->get());

    registry.connect_all();

    EXPECT_TRUE(scoop1->is_connected());
    EXPECT_TRUE(scoop2->is_connected());
}

TEST_F(ConnectionManagementTest, DisconnectAll) {
    ScoopRegistry registry(config_);

    auto scoop1 = std::make_shared<MockScoop>("scoop1");
    auto scoop2 = std::make_shared<MockScoop>("scoop2");

    registry.register_scoop("scoop1", scoop1->get());
    registry.register_scoop("scoop2", scoop2->get());

    registry.connect_all();
    registry.disconnect_all();

    EXPECT_FALSE(scoop1->is_connected());
    EXPECT_FALSE(scoop2->is_connected());
}

// ============================================================================
// Health Management Tests
// ============================================================================

class HealthManagementTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.enable_health_check = false;
        config_.enable_auto_reconnect = false;
    }

    ScoopRegistryConfig config_;
};

TEST_F(HealthManagementTest, GetScoopHealth) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test");
    registry.register_scoop("scoop1", scoop->get());

    auto health = registry.get_scoop_health("scoop1");
    EXPECT_EQ(health, ScoopHealth::UNKNOWN);
}

TEST_F(HealthManagementTest, MarkScoopUnhealthy) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test");
    registry.register_scoop("scoop1", scoop->get());

    registry.mark_scoop_unhealthy("scoop1", "Test reason");

    auto health = registry.get_scoop_health("scoop1");
    EXPECT_EQ(health, ScoopHealth::UNHEALTHY);
}

TEST_F(HealthManagementTest, MarkScoopHealthy) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test");
    registry.register_scoop("scoop1", scoop->get());

    registry.mark_scoop_unhealthy("scoop1", "Test");
    registry.mark_scoop_healthy("scoop1");

    auto health = registry.get_scoop_health("scoop1");
    EXPECT_EQ(health, ScoopHealth::HEALTHY);
}

TEST_F(HealthManagementTest, GetHealthyScoops) {
    ScoopRegistry registry(config_);

    auto scoop1 = std::make_shared<MockScoop>("scoop1");
    auto scoop2 = std::make_shared<MockScoop>("scoop2");

    registry.register_scoop("scoop1", scoop1->get());
    registry.register_scoop("scoop2", scoop2->get());

    registry.mark_scoop_healthy("scoop1");
    registry.mark_scoop_unhealthy("scoop2", "Test");

    auto healthy = registry.get_healthy_scoops();
    EXPECT_EQ(healthy.size(), 1u);
    EXPECT_EQ(healthy[0], "scoop1");
}

TEST_F(HealthManagementTest, GetUnhealthyScoops) {
    ScoopRegistry registry(config_);

    auto scoop1 = std::make_shared<MockScoop>("scoop1");
    auto scoop2 = std::make_shared<MockScoop>("scoop2");

    registry.register_scoop("scoop1", scoop1->get());
    registry.register_scoop("scoop2", scoop2->get());

    registry.mark_scoop_healthy("scoop1");
    registry.mark_scoop_unhealthy("scoop2", "Test");

    auto unhealthy = registry.get_unhealthy_scoops();
    EXPECT_EQ(unhealthy.size(), 1u);
    EXPECT_EQ(unhealthy[0], "scoop2");
}

// ============================================================================
// Address Space Tests
// ============================================================================

class AddressSpaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.enable_health_check = false;
        config_.enable_auto_reconnect = false;
    }

    ScoopRegistryConfig config_;
};

TEST_F(AddressSpaceTest, AddAddress) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test");
    registry.register_scoop("scoop1", scoop->get());

    auto result = registry.add_address({"scoop1"}, "sensors/temp1");

    EXPECT_TRUE(result.is_success());

    auto addresses = registry.get_addresses("scoop1");
    EXPECT_EQ(addresses.size(), 1u);
    EXPECT_EQ(addresses[0], "sensors/temp1");
}

TEST_F(AddressSpaceTest, RemoveAddress) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test");
    registry.register_scoop("scoop1", scoop->get());

    registry.add_address({"scoop1"}, "sensors/temp1");
    auto result = registry.remove_address({"scoop1"}, "sensors/temp1");

    EXPECT_TRUE(result.is_success());

    auto addresses = registry.get_addresses("scoop1");
    EXPECT_TRUE(addresses.empty());
}

TEST_F(AddressSpaceTest, AddAddressToMultipleScoops) {
    ScoopRegistry registry(config_);

    auto scoop1 = std::make_shared<MockScoop>("scoop1");
    auto scoop2 = std::make_shared<MockScoop>("scoop2");

    registry.register_scoop("scoop1", scoop1->get());
    registry.register_scoop("scoop2", scoop2->get());

    registry.add_address({"scoop1", "scoop2"}, "sensors/temp1");

    EXPECT_EQ(registry.get_addresses("scoop1").size(), 1u);
    EXPECT_EQ(registry.get_addresses("scoop2").size(), 1u);
}

// ============================================================================
// Statistics Tests
// ============================================================================

class ScoopRegistryStatsIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.enable_health_check = false;
        config_.enable_auto_reconnect = false;
    }

    ScoopRegistryConfig config_;
};

TEST_F(ScoopRegistryStatsIntegrationTest, ReadStatistics) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test");
    registry.register_scoop("scoop1", scoop->get());

    // Perform some reads
    for (int i = 0; i < 5; ++i) {
        registry.read_from_scoop("scoop1");
    }

    EXPECT_GE(registry.stats().successful_reads.load(), 5u);
}

TEST_F(ScoopRegistryStatsIntegrationTest, ResetStatistics) {
    ScoopRegistry registry(config_);

    auto scoop = std::make_shared<MockScoop>("test");
    registry.register_scoop("scoop1", scoop->get());

    registry.read_from_scoop("scoop1");
    EXPECT_GE(registry.stats().successful_reads.load(), 1u);

    registry.reset_stats();
    EXPECT_EQ(registry.stats().successful_reads.load(), 0u);
}

TEST_F(ScoopRegistryStatsIntegrationTest, GetAllScoopStats) {
    ScoopRegistry registry(config_);

    for (int i = 0; i < 3; ++i) {
        auto scoop = std::make_shared<MockScoop>("scoop" + std::to_string(i));
        registry.register_scoop("scoop" + std::to_string(i), scoop->get());
    }

    auto all_stats = registry.get_all_scoop_stats();
    EXPECT_EQ(all_stats.size(), 3u);
}

TEST_F(ScoopRegistryStatsIntegrationTest, MoveConstruction) {
    ScoopRegistry registry1(config_);

    auto scoop = std::make_shared<MockScoop>("test");
    registry1.register_scoop("scoop1", scoop->get());

    ScoopRegistry registry2(std::move(registry1));

    EXPECT_TRUE(registry2.has_scoop("scoop1"));
    EXPECT_EQ(registry2.scoop_count(), 1u);
}

// ============================================================================
// AggregatedSubscription Tests
// ============================================================================

class AggregatedSubscriptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.enable_health_check = false;
        config_.enable_auto_reconnect = false;
    }

    ScoopRegistryConfig config_;
};

TEST_F(AggregatedSubscriptionTest, DefaultConstruction) {
    AggregatedSubscription sub;
    EXPECT_FALSE(sub.is_active());
    EXPECT_EQ(sub.source_count(), 0u);
}

TEST_F(AggregatedSubscriptionTest, MoveConstruction) {
    AggregatedSubscription sub1;
    AggregatedSubscription sub2(std::move(sub1));
    EXPECT_FALSE(sub2.is_active());
}

TEST_F(AggregatedSubscriptionTest, Cancel) {
    AggregatedSubscription sub;
    sub.cancel();  // Should not crash
    EXPECT_FALSE(sub.is_active());
}

