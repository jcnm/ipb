/**
 * @file test_registry.cpp
 * @brief Comprehensive tests for registry.hpp
 *
 * Covers: LoadBalanceStrategy, HealthStatus, RegistryItemInfo, SelectionResult,
 *         RegistryStats, RegistryConfig, Registry
 */

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <ipb/common/registry.hpp>

using namespace ipb::common;

//=============================================================================
// Test Item Class
//=============================================================================

class MockItem {
public:
    std::string name;
    bool healthy = true;

    explicit MockItem(std::string n = "test") : name(std::move(n)) {}
};

//=============================================================================
// HealthStatus Tests
//=============================================================================

TEST(HealthStatusTest, StatusNames) {
    EXPECT_EQ(health_status_name(HealthStatus::HEALTHY), "HEALTHY");
    EXPECT_EQ(health_status_name(HealthStatus::DEGRADED), "DEGRADED");
    EXPECT_EQ(health_status_name(HealthStatus::UNHEALTHY), "UNHEALTHY");
    EXPECT_EQ(health_status_name(HealthStatus::UNKNOWN), "UNKNOWN");
}

//=============================================================================
// RegistryItemInfo Tests
//=============================================================================

class RegistryItemInfoTest : public ::testing::Test {
protected:
    using ItemInfo = RegistryItemInfo<MockItem>;
};

TEST_F(RegistryItemInfoTest, DefaultConstruction) {
    ItemInfo info;

    EXPECT_EQ(info.id, "");
    EXPECT_EQ(info.weight, 100);
    EXPECT_TRUE(info.enabled);
    EXPECT_EQ(info.priority, 0);
    EXPECT_EQ(info.health, HealthStatus::UNKNOWN);
}

TEST_F(RegistryItemInfoTest, ConstructWithIdAndItem) {
    auto item = std::make_shared<MockItem>("test_item");
    ItemInfo info("item1", item);

    EXPECT_EQ(info.id, "item1");
    EXPECT_EQ(info.item, item);
    EXPECT_EQ(info.item->name, "test_item");
}

TEST_F(RegistryItemInfoTest, SuccessRate) {
    ItemInfo info;

    // No operations - 100% success rate
    EXPECT_DOUBLE_EQ(info.success_rate(), 100.0);

    // All successful
    info.operations_success.store(10);
    info.operations_failed.store(0);
    EXPECT_DOUBLE_EQ(info.success_rate(), 100.0);

    // 50% success rate
    info.operations_success.store(5);
    info.operations_failed.store(5);
    EXPECT_DOUBLE_EQ(info.success_rate(), 50.0);

    // All failed
    info.operations_success.store(0);
    info.operations_failed.store(10);
    EXPECT_DOUBLE_EQ(info.success_rate(), 0.0);
}

TEST_F(RegistryItemInfoTest, AvgLatency) {
    ItemInfo info;

    // No operations
    EXPECT_DOUBLE_EQ(info.avg_latency_us(), 0.0);

    // 10 operations with 10000ns total = 1us each
    info.operations_success.store(10);
    info.total_latency_ns.store(10000);
    EXPECT_DOUBLE_EQ(info.avg_latency_us(), 1.0);
}

TEST_F(RegistryItemInfoTest, RecordSuccess) {
    ItemInfo info;

    info.record_success(1000, 100);
    EXPECT_EQ(info.operations_success.load(), 1);
    EXPECT_EQ(info.total_latency_ns.load(), 1000);
    EXPECT_EQ(info.bytes_processed.load(), 100);

    info.record_success(2000, 50);
    EXPECT_EQ(info.operations_success.load(), 2);
    EXPECT_EQ(info.total_latency_ns.load(), 3000);
    EXPECT_EQ(info.bytes_processed.load(), 150);
}

TEST_F(RegistryItemInfoTest, RecordFailure) {
    ItemInfo info;

    info.record_failure();
    EXPECT_EQ(info.operations_failed.load(), 1);

    info.record_failure();
    info.record_failure();
    EXPECT_EQ(info.operations_failed.load(), 3);
}

TEST_F(RegistryItemInfoTest, ResetStats) {
    ItemInfo info;

    info.operations_success.store(100);
    info.operations_failed.store(50);
    info.bytes_processed.store(10000);
    info.total_latency_ns.store(500000);
    info.pending_count.store(5);

    info.reset_stats();

    EXPECT_EQ(info.operations_success.load(), 0);
    EXPECT_EQ(info.operations_failed.load(), 0);
    EXPECT_EQ(info.bytes_processed.load(), 0);
    EXPECT_EQ(info.total_latency_ns.load(), 0);
    EXPECT_EQ(info.pending_count.load(), 0);
}

TEST_F(RegistryItemInfoTest, CopyConstruction) {
    auto item = std::make_shared<MockItem>("test");
    ItemInfo original("id1", item);
    original.weight = 150;
    original.operations_success.store(10);

    ItemInfo copy(original);

    EXPECT_EQ(copy.id, "id1");
    EXPECT_EQ(copy.weight, 150);
    EXPECT_EQ(copy.operations_success.load(), 10);
    EXPECT_EQ(copy.item, item);  // Shared pointer is shared
}

TEST_F(RegistryItemInfoTest, MoveConstruction) {
    auto item = std::make_shared<MockItem>("test");
    ItemInfo original("id1", item);
    original.weight = 150;

    ItemInfo moved(std::move(original));

    EXPECT_EQ(moved.id, "id1");
    EXPECT_EQ(moved.weight, 150);
    EXPECT_EQ(moved.item->name, "test");
}

//=============================================================================
// SelectionResult Tests
//=============================================================================

TEST(SelectionResultTest, OkWithVector) {
    auto result = SelectionResult::ok({"id1", "id2", "id3"});

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(result.selected_ids.size(), 3);
    EXPECT_EQ(result.selected_ids[0], "id1");
}

TEST(SelectionResultTest, OkWithSingle) {
    auto result = SelectionResult::ok("single_id");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.selected_ids.size(), 1);
    EXPECT_EQ(result.selected_ids[0], "single_id");
}

TEST(SelectionResultTest, Fail) {
    auto result = SelectionResult::fail("no items available");

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_TRUE(result.selected_ids.empty());
    EXPECT_EQ(result.error_message, "no items available");
}

//=============================================================================
// RegistryStats Tests
//=============================================================================

TEST(RegistryStatsTest, DefaultValues) {
    RegistryStats stats;

    EXPECT_EQ(stats.total_selections.load(), 0);
    EXPECT_EQ(stats.successful_selections.load(), 0);
    EXPECT_EQ(stats.failed_selections.load(), 0);
    EXPECT_EQ(stats.failover_events.load(), 0);
}

TEST(RegistryStatsTest, CopyConstruction) {
    RegistryStats original;
    original.total_selections.store(100);
    original.successful_selections.store(90);

    RegistryStats copy(original);

    EXPECT_EQ(copy.total_selections.load(), 100);
    EXPECT_EQ(copy.successful_selections.load(), 90);
}

TEST(RegistryStatsTest, Reset) {
    RegistryStats stats;
    stats.total_selections.store(100);
    stats.successful_selections.store(90);
    stats.failed_selections.store(10);
    stats.failover_events.store(5);

    stats.reset();

    EXPECT_EQ(stats.total_selections.load(), 0);
    EXPECT_EQ(stats.successful_selections.load(), 0);
    EXPECT_EQ(stats.failed_selections.load(), 0);
    EXPECT_EQ(stats.failover_events.load(), 0);
}

//=============================================================================
// RegistryConfig Tests
//=============================================================================

TEST(RegistryConfigTest, DefaultValues) {
    RegistryConfig config;

    EXPECT_EQ(config.default_strategy, LoadBalanceStrategy::ROUND_ROBIN);
    EXPECT_TRUE(config.enable_health_check);
    EXPECT_EQ(config.health_check_interval, std::chrono::milliseconds(5000));
    EXPECT_EQ(config.unhealthy_threshold, 3);
    EXPECT_TRUE(config.enable_failover);
    EXPECT_EQ(config.failover_timeout, std::chrono::milliseconds(30000));
    EXPECT_EQ(config.max_items, 1000);
}

//=============================================================================
// Registry Tests
//=============================================================================

class RegistryTest : public ::testing::Test {
protected:
    using TestRegistry = Registry<MockItem>;

    TestRegistry registry;

    void SetUp() override {
        // Register some test items
        registry.register_item("item1", std::make_shared<MockItem>("Item 1"));
        registry.register_item("item2", std::make_shared<MockItem>("Item 2"));
        registry.register_item("item3", std::make_shared<MockItem>("Item 3"));
    }
};

TEST_F(RegistryTest, RegisterItem) {
    TestRegistry fresh_registry;

    EXPECT_TRUE(fresh_registry.register_item("new_item", std::make_shared<MockItem>()));
    EXPECT_EQ(fresh_registry.count(), 1);
    EXPECT_TRUE(fresh_registry.has("new_item"));
}

TEST_F(RegistryTest, RegisterItemWithWeight) {
    TestRegistry fresh_registry;

    EXPECT_TRUE(fresh_registry.register_item("weighted", std::make_shared<MockItem>(), 200));

    auto info = fresh_registry.get_info("weighted");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->weight, 200);
}

TEST_F(RegistryTest, RegisterDuplicate) {
    EXPECT_FALSE(registry.register_item("item1", std::make_shared<MockItem>()));
    EXPECT_EQ(registry.count(), 3);  // Still 3
}

TEST_F(RegistryTest, RegisterEmpty) {
    EXPECT_FALSE(registry.register_item("", std::make_shared<MockItem>()));
    EXPECT_FALSE(registry.register_item("valid", nullptr));
}

TEST_F(RegistryTest, UnregisterItem) {
    EXPECT_TRUE(registry.unregister_item("item1"));
    EXPECT_EQ(registry.count(), 2);
    EXPECT_FALSE(registry.has("item1"));
}

TEST_F(RegistryTest, UnregisterNonexistent) {
    EXPECT_FALSE(registry.unregister_item("nonexistent"));
    EXPECT_EQ(registry.count(), 3);
}

TEST_F(RegistryTest, Has) {
    EXPECT_TRUE(registry.has("item1"));
    EXPECT_TRUE(registry.has("item2"));
    EXPECT_TRUE(registry.has("item3"));
    EXPECT_FALSE(registry.has("nonexistent"));
}

TEST_F(RegistryTest, Get) {
    auto item = registry.get("item1");
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->name, "Item 1");
}

TEST_F(RegistryTest, GetNonexistent) {
    auto item = registry.get("nonexistent");
    EXPECT_EQ(item, nullptr);
}

TEST_F(RegistryTest, GetInfo) {
    auto info = registry.get_info("item1");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->id, "item1");
    EXPECT_EQ(info->item->name, "Item 1");
}

TEST_F(RegistryTest, GetInfoNonexistent) {
    auto info = registry.get_info("nonexistent");
    EXPECT_FALSE(info.has_value());
}

TEST_F(RegistryTest, GetIds) {
    auto ids = registry.get_ids();
    EXPECT_EQ(ids.size(), 3);
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), "item1") != ids.end());
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), "item2") != ids.end());
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), "item3") != ids.end());
}

TEST_F(RegistryTest, Count) {
    EXPECT_EQ(registry.count(), 3);
}

TEST_F(RegistryTest, SetEnabled) {
    EXPECT_TRUE(registry.set_enabled("item1", false));

    auto info = registry.get_info("item1");
    ASSERT_TRUE(info.has_value());
    EXPECT_FALSE(info->enabled);
}

TEST_F(RegistryTest, SetWeight) {
    EXPECT_TRUE(registry.set_weight("item1", 200));

    auto info = registry.get_info("item1");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->weight, 200);
}

TEST_F(RegistryTest, SetPriority) {
    EXPECT_TRUE(registry.set_priority("item1", 5));

    auto info = registry.get_info("item1");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->priority, 5);
}

//=============================================================================
// Registry Selection Tests
//=============================================================================

class RegistrySelectionTest : public ::testing::Test {
protected:
    using TestRegistry = Registry<MockItem>;

    TestRegistry registry;

    void SetUp() override {
        // Register items with different weights
        registry.register_item("item1", std::make_shared<MockItem>("Item 1"), 100);
        registry.register_item("item2", std::make_shared<MockItem>("Item 2"), 200);
        registry.register_item("item3", std::make_shared<MockItem>("Item 3"), 100);

        // Mark all as healthy
        registry.mark_healthy("item1");
        registry.mark_healthy("item2");
        registry.mark_healthy("item3");
    }
};

TEST_F(RegistrySelectionTest, RoundRobin) {
    std::vector<std::string> candidates = {"item1", "item2", "item3"};

    // Round robin should cycle through items
    std::vector<std::string> selected;
    for (int i = 0; i < 6; ++i) {
        auto result = registry.select(candidates, LoadBalanceStrategy::ROUND_ROBIN);
        ASSERT_TRUE(result.success);
        selected.push_back(result.selected_ids[0]);
    }

    // Should have seen all items at least once
    EXPECT_TRUE(std::find(selected.begin(), selected.end(), "item1") != selected.end());
    EXPECT_TRUE(std::find(selected.begin(), selected.end(), "item2") != selected.end());
    EXPECT_TRUE(std::find(selected.begin(), selected.end(), "item3") != selected.end());
}

TEST_F(RegistrySelectionTest, WeightedRoundRobin) {
    std::vector<std::string> candidates = {"item1", "item2", "item3"};

    // item2 has weight 200, others have 100
    // So item2 should be selected more often (roughly 50%)
    int item1_count = 0, item2_count = 0, item3_count = 0;

    for (int i = 0; i < 400; ++i) {
        auto result = registry.select(candidates, LoadBalanceStrategy::WEIGHTED_ROUND_ROBIN);
        ASSERT_TRUE(result.success);
        if (result.selected_ids[0] == "item1") item1_count++;
        else if (result.selected_ids[0] == "item2") item2_count++;
        else if (result.selected_ids[0] == "item3") item3_count++;
    }

    // item2 should have roughly twice the selections
    EXPECT_GT(item2_count, item1_count);
    EXPECT_GT(item2_count, item3_count);
}

TEST_F(RegistrySelectionTest, Random) {
    std::vector<std::string> candidates = {"item1", "item2", "item3"};

    // Random should distribute selections
    int item1_count = 0, item2_count = 0, item3_count = 0;

    for (int i = 0; i < 300; ++i) {
        auto result = registry.select(candidates, LoadBalanceStrategy::RANDOM);
        ASSERT_TRUE(result.success);
        if (result.selected_ids[0] == "item1") item1_count++;
        else if (result.selected_ids[0] == "item2") item2_count++;
        else if (result.selected_ids[0] == "item3") item3_count++;
    }

    // All should have some selections
    EXPECT_GT(item1_count, 0);
    EXPECT_GT(item2_count, 0);
    EXPECT_GT(item3_count, 0);
}

TEST_F(RegistrySelectionTest, Broadcast) {
    std::vector<std::string> candidates = {"item1", "item2", "item3"};

    auto result = registry.select(candidates, LoadBalanceStrategy::BROADCAST);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.selected_ids.size(), 3);
}

TEST_F(RegistrySelectionTest, Failover) {
    // Set priorities
    registry.set_priority("item1", 0);  // Highest priority (lowest number)
    registry.set_priority("item2", 1);
    registry.set_priority("item3", 2);

    std::vector<std::string> candidates = {"item1", "item2", "item3"};

    // Should select item1 (highest priority)
    auto result = registry.select(candidates, LoadBalanceStrategy::FAILOVER);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.selected_ids[0], "item1");
}

TEST_F(RegistrySelectionTest, FailoverWithUnhealthy) {
    registry.set_priority("item1", 0);
    registry.set_priority("item2", 1);
    registry.set_priority("item3", 2);

    // Mark item1 as unhealthy
    registry.mark_unhealthy("item1", "down");

    std::vector<std::string> candidates = {"item1", "item2", "item3"};

    // Should skip unhealthy item1 and select item2
    auto result = registry.select(candidates, LoadBalanceStrategy::FAILOVER);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.selected_ids[0], "item2");
}

TEST_F(RegistrySelectionTest, NoEligibleItems) {
    // Mark all unhealthy
    registry.mark_unhealthy("item1");
    registry.mark_unhealthy("item2");
    registry.mark_unhealthy("item3");

    std::vector<std::string> candidates = {"item1", "item2", "item3"};
    auto result = registry.select(candidates);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(RegistrySelectionTest, DisabledItemsSkipped) {
    registry.set_enabled("item1", false);
    registry.set_enabled("item2", false);

    std::vector<std::string> candidates = {"item1", "item2", "item3"};

    // Only item3 should be selectable
    auto result = registry.select(candidates, LoadBalanceStrategy::ROUND_ROBIN);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.selected_ids[0], "item3");
}

TEST_F(RegistrySelectionTest, SelectWithFilter) {
    // Add custom filter
    auto result = registry.select_filtered(
        {"item1", "item2", "item3"},
        [](const RegistryItemInfo<MockItem>& info) {
            // Only select items with name containing "2"
            return info.item->name.find("2") != std::string::npos;
        },
        LoadBalanceStrategy::ROUND_ROBIN
    );

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.selected_ids[0], "item2");
}

//=============================================================================
// Registry Health Tests
//=============================================================================

class RegistryHealthTest : public ::testing::Test {
protected:
    using TestRegistry = Registry<MockItem>;

    TestRegistry registry;

    void SetUp() override {
        registry.register_item("item1", std::make_shared<MockItem>("Item 1"));
        registry.register_item("item2", std::make_shared<MockItem>("Item 2"));
    }
};

TEST_F(RegistryHealthTest, InitialHealth) {
    EXPECT_EQ(registry.get_health("item1"), HealthStatus::UNKNOWN);
}

TEST_F(RegistryHealthTest, MarkHealthy) {
    registry.mark_healthy("item1");
    EXPECT_EQ(registry.get_health("item1"), HealthStatus::HEALTHY);
}

TEST_F(RegistryHealthTest, MarkUnhealthy) {
    registry.mark_unhealthy("item1", "test reason");
    EXPECT_EQ(registry.get_health("item1"), HealthStatus::UNHEALTHY);
}

TEST_F(RegistryHealthTest, GetHealthy) {
    registry.mark_healthy("item1");
    registry.mark_unhealthy("item2");

    auto healthy = registry.get_healthy();
    EXPECT_EQ(healthy.size(), 1);
    EXPECT_EQ(healthy[0], "item1");
}

TEST_F(RegistryHealthTest, GetUnhealthy) {
    registry.mark_healthy("item1");
    registry.mark_unhealthy("item2");

    auto unhealthy = registry.get_unhealthy();
    EXPECT_EQ(unhealthy.size(), 1);
    EXPECT_EQ(unhealthy[0], "item2");
}

TEST_F(RegistryHealthTest, HealthForNonexistent) {
    EXPECT_EQ(registry.get_health("nonexistent"), HealthStatus::UNKNOWN);
}

//=============================================================================
// Registry Statistics Tests
//=============================================================================

TEST(RegistryStatsIntegrationTest, SelectionStats) {
    Registry<MockItem> registry;

    registry.register_item("item1", std::make_shared<MockItem>());
    registry.mark_healthy("item1");

    // Make some selections
    for (int i = 0; i < 10; ++i) {
        registry.select({"item1"});
    }

    const auto& stats = registry.stats();
    EXPECT_EQ(stats.total_selections.load(), 10);
    EXPECT_EQ(stats.successful_selections.load(), 10);
}

TEST(RegistryStatsIntegrationTest, FailedSelectionStats) {
    Registry<MockItem> registry;

    registry.register_item("item1", std::make_shared<MockItem>());
    registry.mark_unhealthy("item1");

    // Selection should fail
    registry.select({"item1"});

    const auto& stats = registry.stats();
    EXPECT_EQ(stats.total_selections.load(), 1);
    EXPECT_EQ(stats.failed_selections.load(), 1);
}

TEST(RegistryStatsIntegrationTest, RecordOperation) {
    Registry<MockItem> registry;

    registry.register_item("item1", std::make_shared<MockItem>());

    registry.record_operation("item1", true, 1000, 100);
    registry.record_operation("item1", true, 2000, 200);
    registry.record_operation("item1", false);

    auto info = registry.get_info("item1");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->operations_success.load(), 2);
    EXPECT_EQ(info->operations_failed.load(), 1);
    EXPECT_EQ(info->bytes_processed.load(), 300);
    EXPECT_EQ(info->total_latency_ns.load(), 3000);
}

TEST(RegistryStatsIntegrationTest, ResetStats) {
    Registry<MockItem> registry;

    registry.register_item("item1", std::make_shared<MockItem>());
    registry.mark_healthy("item1");
    registry.select({"item1"});
    registry.select({"item1"});

    registry.reset_stats();

    const auto& stats = registry.stats();
    EXPECT_EQ(stats.total_selections.load(), 0);
    EXPECT_EQ(stats.successful_selections.load(), 0);
}

//=============================================================================
// Registry Lifecycle Tests
//=============================================================================

TEST(RegistryLifecycleTest, StartStop) {
    RegistryConfig config;
    config.enable_health_check = true;
    config.health_check_interval = std::chrono::milliseconds(100);

    Registry<MockItem> registry(config);

    EXPECT_FALSE(registry.is_running());

    registry.start();
    EXPECT_TRUE(registry.is_running());

    registry.stop();
    EXPECT_FALSE(registry.is_running());
}

TEST(RegistryLifecycleTest, MoveConstruction) {
    Registry<MockItem> original;
    original.register_item("item1", std::make_shared<MockItem>());

    Registry<MockItem> moved(std::move(original));

    EXPECT_TRUE(moved.has("item1"));
    EXPECT_EQ(moved.count(), 1);
}

TEST(RegistryLifecycleTest, MoveAssignment) {
    Registry<MockItem> registry1;
    registry1.register_item("item1", std::make_shared<MockItem>());

    Registry<MockItem> registry2;
    registry2.register_item("item2", std::make_shared<MockItem>());

    registry2 = std::move(registry1);

    EXPECT_TRUE(registry2.has("item1"));
    EXPECT_FALSE(registry2.has("item2"));
}

//=============================================================================
// Registry Concurrent Access Tests
//=============================================================================

TEST(RegistryConcurrencyTest, ConcurrentSelection) {
    Registry<MockItem> registry;

    for (int i = 0; i < 10; ++i) {
        registry.register_item("item" + std::to_string(i), std::make_shared<MockItem>());
        registry.mark_healthy("item" + std::to_string(i));
    }

    std::vector<std::string> candidates;
    for (int i = 0; i < 10; ++i) {
        candidates.push_back("item" + std::to_string(i));
    }

    constexpr int NUM_THREADS = 4;
    constexpr int SELECTIONS_PER_THREAD = 100;

    std::vector<std::thread> threads;
    std::atomic<int> total_success{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&registry, &candidates, &total_success]() {
            for (int i = 0; i < SELECTIONS_PER_THREAD; ++i) {
                auto result = registry.select(candidates, LoadBalanceStrategy::ROUND_ROBIN);
                if (result.success) {
                    total_success++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(total_success.load(), NUM_THREADS * SELECTIONS_PER_THREAD);
}

TEST(RegistryConcurrencyTest, ConcurrentRegistration) {
    Registry<MockItem> registry;

    constexpr int NUM_THREADS = 4;
    constexpr int ITEMS_PER_THREAD = 25;

    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&registry, t]() {
            for (int i = 0; i < ITEMS_PER_THREAD; ++i) {
                std::string id = "t" + std::to_string(t) + "_item" + std::to_string(i);
                registry.register_item(id, std::make_shared<MockItem>(id));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(registry.count(), NUM_THREADS * ITEMS_PER_THREAD);
}
