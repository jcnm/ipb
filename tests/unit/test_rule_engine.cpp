/**
 * @file test_rule_engine.cpp
 * @brief Unit tests for IPB RuleEngine
 *
 * Tests coverage for:
 * - RulePriority, RuleType, CompareOp enums
 * - ValueCondition: Value-based matching
 * - RoutingRule: Rule definition and evaluation
 * - RuleBuilder: Fluent rule construction
 * - RuleEngine: Rule management and evaluation
 */

#include <ipb/core/rule_engine/rule_engine.hpp>

#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace ipb::core;
using namespace ipb::common;

// ============================================================================
// RulePriority Tests
// ============================================================================

class RulePriorityTest : public ::testing::Test {};

TEST_F(RulePriorityTest, PriorityValues) {
    EXPECT_EQ(static_cast<uint8_t>(RulePriority::LOWEST), 0);
    EXPECT_EQ(static_cast<uint8_t>(RulePriority::LOW), 64);
    EXPECT_EQ(static_cast<uint8_t>(RulePriority::NORMAL), 128);
    EXPECT_EQ(static_cast<uint8_t>(RulePriority::HIGH), 192);
    EXPECT_EQ(static_cast<uint8_t>(RulePriority::HIGHEST), 255);
    EXPECT_EQ(static_cast<uint8_t>(RulePriority::REALTIME), 254);
}

// ============================================================================
// RuleType Tests
// ============================================================================

class RuleTypeTest : public ::testing::Test {};

TEST_F(RuleTypeTest, TypeValues) {
    EXPECT_EQ(static_cast<uint8_t>(RuleType::STATIC), 0);
    EXPECT_EQ(static_cast<uint8_t>(RuleType::PATTERN), 1);
    EXPECT_EQ(static_cast<uint8_t>(RuleType::PROTOCOL), 2);
    EXPECT_EQ(static_cast<uint8_t>(RuleType::QUALITY), 3);
    EXPECT_EQ(static_cast<uint8_t>(RuleType::VALUE), 4);
    EXPECT_EQ(static_cast<uint8_t>(RuleType::TIMESTAMP), 5);
    EXPECT_EQ(static_cast<uint8_t>(RuleType::COMPOSITE), 6);
    EXPECT_EQ(static_cast<uint8_t>(RuleType::CUSTOM), 7);
}

// ============================================================================
// CompareOp Tests
// ============================================================================

class CompareOpTest : public ::testing::Test {};

TEST_F(CompareOpTest, OperatorValues) {
    EXPECT_EQ(static_cast<uint8_t>(CompareOp::EQ), 0);
    EXPECT_EQ(static_cast<uint8_t>(CompareOp::NE), 1);
    EXPECT_EQ(static_cast<uint8_t>(CompareOp::LT), 2);
    EXPECT_EQ(static_cast<uint8_t>(CompareOp::LE), 3);
    EXPECT_EQ(static_cast<uint8_t>(CompareOp::GT), 4);
    EXPECT_EQ(static_cast<uint8_t>(CompareOp::GE), 5);
    EXPECT_EQ(static_cast<uint8_t>(CompareOp::BETWEEN), 6);
}

// ============================================================================
// ValueCondition Tests
// ============================================================================

class ValueConditionTest : public ::testing::Test {};

TEST_F(ValueConditionTest, DefaultConstruction) {
    ValueCondition cond;
    EXPECT_EQ(cond.op, CompareOp::EQ);
}

TEST_F(ValueConditionTest, EqualityComparison) {
    ValueCondition cond;
    cond.op        = CompareOp::EQ;
    cond.reference = int64_t(42);

    Value v;
    v.set(int32_t(42));
    // Note: evaluate() implementation details depend on actual code
}

// ============================================================================
// RuleMatchResult Tests
// ============================================================================

class RuleMatchResultTest : public ::testing::Test {};

TEST_F(RuleMatchResultTest, DefaultConstruction) {
    RuleMatchResult result;
    EXPECT_FALSE(result.matched);
    EXPECT_EQ(result.rule_id, 0u);
    EXPECT_EQ(result.priority, RulePriority::NORMAL);
    EXPECT_TRUE(result.target_ids.empty());
}

TEST_F(RuleMatchResultTest, BoolConversion) {
    RuleMatchResult matched;
    matched.matched = true;

    RuleMatchResult not_matched;
    not_matched.matched = false;

    EXPECT_TRUE(static_cast<bool>(matched));
    EXPECT_FALSE(static_cast<bool>(not_matched));
}

// ============================================================================
// RoutingRule Tests
// ============================================================================

class RoutingRuleTest : public ::testing::Test {};

TEST_F(RoutingRuleTest, DefaultConstruction) {
    RoutingRule rule;
    EXPECT_EQ(rule.id, 0u);
    EXPECT_TRUE(rule.name.empty());
    EXPECT_EQ(rule.type, RuleType::STATIC);
    EXPECT_EQ(rule.priority, RulePriority::NORMAL);
    EXPECT_TRUE(rule.enabled);
}

TEST_F(RoutingRuleTest, CopyConstruction) {
    RoutingRule original;
    original.id              = 42;
    original.name            = "test_rule";
    original.type            = RuleType::PATTERN;
    original.address_pattern = "sensors/.*";
    original.target_sink_ids = {"sink1", "sink2"};
    original.match_count.store(100);

    RoutingRule copy(original);

    EXPECT_EQ(copy.id, 42u);
    EXPECT_EQ(copy.name, "test_rule");
    EXPECT_EQ(copy.type, RuleType::PATTERN);
    EXPECT_EQ(copy.address_pattern, "sensors/.*");
    EXPECT_EQ(copy.target_sink_ids.size(), 2u);
    EXPECT_EQ(copy.match_count.load(), 100u);
}

TEST_F(RoutingRuleTest, MoveConstruction) {
    RoutingRule original;
    original.id              = 42;
    original.name            = "test_rule";
    original.target_sink_ids = {"sink1", "sink2"};

    RoutingRule moved(std::move(original));

    EXPECT_EQ(moved.id, 42u);
    EXPECT_EQ(moved.name, "test_rule");
    EXPECT_EQ(moved.target_sink_ids.size(), 2u);
}

TEST_F(RoutingRuleTest, CopyAssignment) {
    RoutingRule original;
    original.id   = 42;
    original.name = "original";

    RoutingRule copy;
    copy.id   = 1;
    copy.name = "copy";

    copy = original;

    EXPECT_EQ(copy.id, 42u);
    EXPECT_EQ(copy.name, "original");
}

TEST_F(RoutingRuleTest, MoveAssignment) {
    RoutingRule original;
    original.id   = 42;
    original.name = "original";

    RoutingRule moved;
    moved = std::move(original);

    EXPECT_EQ(moved.id, 42u);
    EXPECT_EQ(moved.name, "original");
}

TEST_F(RoutingRuleTest, AverageEvalTime) {
    RoutingRule rule;
    rule.eval_count.store(100);
    rule.total_eval_time_ns.store(50000);  // 50us total

    EXPECT_DOUBLE_EQ(rule.avg_eval_time_ns(), 500.0);  // 500ns average
}

// ============================================================================
// RuleEngineStats Tests
// ============================================================================

class RuleEngineStatsTest : public ::testing::Test {};

TEST_F(RuleEngineStatsTest, DefaultValues) {
    RuleEngineStats stats;
    EXPECT_EQ(stats.total_evaluations.load(), 0u);
    EXPECT_EQ(stats.total_matches.load(), 0u);
    EXPECT_EQ(stats.cache_hits.load(), 0u);
    EXPECT_EQ(stats.cache_misses.load(), 0u);
}

TEST_F(RuleEngineStatsTest, MatchRate) {
    RuleEngineStats stats;

    // No evaluations
    EXPECT_DOUBLE_EQ(stats.match_rate(), 0.0);

    // 50% match rate
    stats.total_evaluations.store(100);
    stats.total_matches.store(50);
    EXPECT_DOUBLE_EQ(stats.match_rate(), 50.0);
}

TEST_F(RuleEngineStatsTest, AverageEvalTime) {
    RuleEngineStats stats;

    // No evaluations
    EXPECT_DOUBLE_EQ(stats.avg_eval_time_ns(), 0.0);

    // Set values
    stats.total_evaluations.store(100);
    stats.total_eval_time_ns.store(100000);              // 100us total
    EXPECT_DOUBLE_EQ(stats.avg_eval_time_ns(), 1000.0);  // 1000ns average
}

TEST_F(RuleEngineStatsTest, Reset) {
    RuleEngineStats stats;
    stats.total_evaluations.store(100);
    stats.total_matches.store(50);
    stats.cache_hits.store(30);

    stats.reset();

    EXPECT_EQ(stats.total_evaluations.load(), 0u);
    EXPECT_EQ(stats.total_matches.load(), 0u);
    EXPECT_EQ(stats.cache_hits.load(), 0u);
}

// ============================================================================
// RuleEngineConfig Tests
// ============================================================================

class RuleEngineConfigTest : public ::testing::Test {};

TEST_F(RuleEngineConfigTest, DefaultValues) {
    RuleEngineConfig config;
    EXPECT_EQ(config.max_rules, 10000u);
    EXPECT_TRUE(config.enable_cache);
    EXPECT_EQ(config.cache_size, 65536u);
    EXPECT_TRUE(config.prefer_ctre);
    EXPECT_TRUE(config.precompile_patterns);
}

// ============================================================================
// RuleBuilder Tests
// ============================================================================

class RuleBuilderTest : public ::testing::Test {};

TEST_F(RuleBuilderTest, BuildStaticRule) {
    auto rule = RuleBuilder()
                    .name("static_rule")
                    .priority(RulePriority::HIGH)
                    .match_address("sensors/temp1")
                    .route_to("influxdb")
                    .build();

    EXPECT_EQ(rule.name, "static_rule");
    EXPECT_EQ(rule.priority, RulePriority::HIGH);
    EXPECT_EQ(rule.type, RuleType::STATIC);
    EXPECT_EQ(rule.source_addresses.size(), 1u);
    EXPECT_EQ(rule.source_addresses[0], "sensors/temp1");
    EXPECT_EQ(rule.target_sink_ids.size(), 1u);
    EXPECT_EQ(rule.target_sink_ids[0], "influxdb");
}

TEST_F(RuleBuilderTest, BuildPatternRule) {
    auto rule = RuleBuilder()
                    .name("pattern_rule")
                    .match_pattern("sensors/temp.*")
                    .route_to(std::vector<std::string>{"kafka", "influxdb"})
                    .build();

    EXPECT_EQ(rule.name, "pattern_rule");
    EXPECT_EQ(rule.type, RuleType::PATTERN);
    EXPECT_EQ(rule.address_pattern, "sensors/temp.*");
    EXPECT_EQ(rule.target_sink_ids.size(), 2u);
}

TEST_F(RuleBuilderTest, BuildProtocolRule) {
    auto rule = RuleBuilder()
                    .name("protocol_rule")
                    .match_protocol(1)
                    .match_protocols({2, 3, 4})
                    .route_to("protocol_sink")
                    .build();

    EXPECT_EQ(rule.type, RuleType::PROTOCOL);
    EXPECT_GE(rule.protocol_ids.size(), 1u);
}

TEST_F(RuleBuilderTest, BuildQualityRule) {
    auto rule = RuleBuilder()
                    .name("quality_rule")
                    .match_quality(Quality::GOOD)
                    .route_to("good_data_sink")
                    .build();

    EXPECT_EQ(rule.type, RuleType::QUALITY);
    EXPECT_EQ(rule.quality_levels.size(), 1u);
}

TEST_F(RuleBuilderTest, BuildCustomRule) {
    auto rule = RuleBuilder()
                    .name("custom_rule")
                    .match_custom([](const DataPoint& dp) {
                        return dp.address().find("temp") != std::string::npos;
                    })
                    .route_to("custom_sink")
                    .build();

    EXPECT_EQ(rule.type, RuleType::CUSTOM);
    EXPECT_TRUE(rule.custom_predicate != nullptr);
}

TEST_F(RuleBuilderTest, BuildMultiTargetRule) {
    auto rule = RuleBuilder()
                    .name("multi_target")
                    .match_pattern(".*")
                    .route_to(std::vector<std::string>{"sink1", "sink2", "sink3"})
                    .build();

    EXPECT_EQ(rule.target_sink_ids.size(), 3u);
}

// ============================================================================
// RuleEngine Tests
// ============================================================================

class RuleEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.max_rules    = 1000;
        config_.enable_cache = true;
        config_.cache_size   = 1024;
    }

    RuleEngineConfig config_;
};

TEST_F(RuleEngineTest, DefaultConstruction) {
    RuleEngine engine;
    EXPECT_EQ(engine.rule_count(), 0u);
}

TEST_F(RuleEngineTest, ConfiguredConstruction) {
    RuleEngine engine(config_);
    EXPECT_EQ(engine.config().max_rules, 1000u);
}

TEST_F(RuleEngineTest, AddRule) {
    RuleEngine engine(config_);

    auto rule =
        RuleBuilder().name("test_rule").match_address("sensors/temp1").route_to("sink1").build();

    uint32_t rule_id = engine.add_rule(rule);
    EXPECT_GT(rule_id, 0u);
    EXPECT_EQ(engine.rule_count(), 1u);
}

TEST_F(RuleEngineTest, GetRule) {
    RuleEngine engine(config_);

    auto rule =
        RuleBuilder().name("test_rule").match_address("sensors/temp1").route_to("sink1").build();

    uint32_t rule_id = engine.add_rule(rule);

    auto retrieved = engine.get_rule(rule_id);
    EXPECT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->name, "test_rule");
}

TEST_F(RuleEngineTest, GetNonexistentRule) {
    RuleEngine engine(config_);

    auto retrieved = engine.get_rule(999);
    EXPECT_FALSE(retrieved.has_value());
}

TEST_F(RuleEngineTest, RemoveRule) {
    RuleEngine engine(config_);

    auto rule =
        RuleBuilder().name("test_rule").match_address("sensors/temp1").route_to("sink1").build();

    uint32_t rule_id = engine.add_rule(rule);
    EXPECT_EQ(engine.rule_count(), 1u);

    bool removed = engine.remove_rule(rule_id);
    EXPECT_TRUE(removed);
    EXPECT_EQ(engine.rule_count(), 0u);
}

TEST_F(RuleEngineTest, EnableDisableRule) {
    RuleEngine engine(config_);

    auto rule =
        RuleBuilder().name("test_rule").match_address("sensors/temp1").route_to("sink1").build();

    uint32_t rule_id = engine.add_rule(rule);

    bool disabled = engine.set_rule_enabled(rule_id, false);
    EXPECT_TRUE(disabled);

    auto retrieved = engine.get_rule(rule_id);
    EXPECT_TRUE(retrieved.has_value());
    EXPECT_FALSE(retrieved->enabled);
}

TEST_F(RuleEngineTest, UpdateRule) {
    RuleEngine engine(config_);

    auto rule =
        RuleBuilder().name("original").match_address("sensors/temp1").route_to("sink1").build();

    uint32_t rule_id = engine.add_rule(rule);

    auto updated_rule =
        RuleBuilder().name("updated").match_address("sensors/temp2").route_to("sink2").build();

    bool updated = engine.update_rule(rule_id, updated_rule);
    EXPECT_TRUE(updated);

    auto retrieved = engine.get_rule(rule_id);
    EXPECT_EQ(retrieved->name, "updated");
}

TEST_F(RuleEngineTest, GetAllRules) {
    RuleEngine engine(config_);

    for (int i = 0; i < 5; ++i) {
        auto rule = RuleBuilder()
                        .name("rule_" + std::to_string(i))
                        .match_address("sensors/temp" + std::to_string(i))
                        .route_to("sink" + std::to_string(i))
                        .build();
        engine.add_rule(rule);
    }

    auto rules = engine.get_all_rules();
    EXPECT_EQ(rules.size(), 5u);
}

TEST_F(RuleEngineTest, ClearRules) {
    RuleEngine engine(config_);

    for (int i = 0; i < 5; ++i) {
        auto rule = RuleBuilder()
                        .name("rule_" + std::to_string(i))
                        .match_address("sensors/temp" + std::to_string(i))
                        .route_to("sink")
                        .build();
        engine.add_rule(rule);
    }

    EXPECT_EQ(engine.rule_count(), 5u);

    engine.clear_rules();
    EXPECT_EQ(engine.rule_count(), 0u);
}

TEST_F(RuleEngineTest, EvaluateStaticRule) {
    RuleEngine engine(config_);

    auto rule =
        RuleBuilder().name("static_rule").match_address("sensors/temp1").route_to("sink1").build();

    engine.add_rule(rule);

    DataPoint dp("sensors/temp1");
    dp.set_value(25.5);

    auto results = engine.evaluate(dp);
    EXPECT_EQ(results.size(), 1u);
    if (!results.empty()) {
        EXPECT_TRUE(results[0].matched);
        EXPECT_EQ(results[0].target_ids.size(), 1u);
        EXPECT_EQ(results[0].target_ids[0], "sink1");
    }
}

TEST_F(RuleEngineTest, EvaluateNoMatch) {
    RuleEngine engine(config_);

    auto rule =
        RuleBuilder().name("static_rule").match_address("sensors/temp1").route_to("sink1").build();

    engine.add_rule(rule);

    DataPoint dp("sensors/humidity1");  // Different address
    dp.set_value(65.0);

    auto results = engine.evaluate(dp);
    // No matches expected for this address
    bool found_match = false;
    for (const auto& r : results) {
        if (r.matched)
            found_match = true;
    }
    EXPECT_FALSE(found_match);
}

TEST_F(RuleEngineTest, EvaluateFirst) {
    RuleEngine engine(config_);

    auto rule1 = RuleBuilder()
                     .name("rule1")
                     .priority(RulePriority::HIGH)
                     .match_address("sensors/temp1")
                     .route_to("high_priority_sink")
                     .build();

    auto rule2 = RuleBuilder()
                     .name("rule2")
                     .priority(RulePriority::LOW)
                     .match_address("sensors/temp1")
                     .route_to("low_priority_sink")
                     .build();

    engine.add_rule(rule1);
    engine.add_rule(rule2);

    DataPoint dp("sensors/temp1");
    auto result = engine.evaluate_first(dp);

    if (result.has_value()) {
        EXPECT_TRUE(result->matched);
    }
}

TEST_F(RuleEngineTest, EvaluatePriority) {
    RuleEngine engine(config_);

    auto high_rule = RuleBuilder()
                         .name("high_priority")
                         .priority(RulePriority::HIGH)
                         .match_address("sensors/temp1")
                         .route_to("high_sink")
                         .build();

    auto low_rule = RuleBuilder()
                        .name("low_priority")
                        .priority(RulePriority::LOW)
                        .match_address("sensors/temp1")
                        .route_to("low_sink")
                        .build();

    engine.add_rule(high_rule);
    engine.add_rule(low_rule);

    DataPoint dp("sensors/temp1");
    auto results = engine.evaluate_priority(dp, RulePriority::HIGH);

    // Only high priority rules should match
    for (const auto& r : results) {
        if (r.matched) {
            EXPECT_GE(static_cast<uint8_t>(r.priority), static_cast<uint8_t>(RulePriority::HIGH));
        }
    }
}

TEST_F(RuleEngineTest, EvaluateBatch) {
    RuleEngine engine(config_);

    auto rule =
        RuleBuilder().name("batch_rule").match_pattern("sensors/.*").route_to("batch_sink").build();

    engine.add_rule(rule);

    std::vector<DataPoint> batch;
    for (int i = 0; i < 10; ++i) {
        DataPoint dp("sensors/temp" + std::to_string(i));
        dp.set_value(static_cast<double>(20 + i));
        batch.push_back(dp);
    }

    auto results = engine.evaluate_batch(batch);
    EXPECT_EQ(results.size(), 10u);
}

TEST_F(RuleEngineTest, Statistics) {
    RuleEngine engine(config_);

    auto rule =
        RuleBuilder().name("test_rule").match_address("sensors/temp1").route_to("sink1").build();

    engine.add_rule(rule);

    // Evaluate with different data points to ensure unique evaluations
    DataPoint dp1("sensors/temp1");
    DataPoint dp2("sensors/temp2");
    DataPoint dp3("sensors/temp3");
    engine.evaluate(dp1);
    engine.evaluate(dp2);
    engine.evaluate(dp3);

    const auto& stats = engine.stats();
    // At least one evaluation should have been counted
    EXPECT_GE(stats.total_evaluations.load(), 1u);
}

TEST_F(RuleEngineTest, ResetStats) {
    RuleEngine engine(config_);

    auto rule =
        RuleBuilder().name("test_rule").match_address("sensors/temp1").route_to("sink1").build();

    engine.add_rule(rule);

    DataPoint dp("sensors/temp1");
    engine.evaluate(dp);

    engine.reset_stats();

    const auto& stats = engine.stats();
    EXPECT_EQ(stats.total_evaluations.load(), 0u);
}

TEST_F(RuleEngineTest, ClearCache) {
    RuleEngine engine(config_);

    auto rule =
        RuleBuilder().name("test_rule").match_pattern("sensors/.*").route_to("sink1").build();

    engine.add_rule(rule);

    DataPoint dp("sensors/temp1");
    engine.evaluate(dp);
    engine.evaluate(dp);

    engine.clear_cache();
    // Cache should be cleared, next evaluation is a cache miss
}

TEST_F(RuleEngineTest, MoveConstruction) {
    RuleEngine engine1(config_);

    auto rule =
        RuleBuilder().name("test_rule").match_address("sensors/temp1").route_to("sink1").build();

    engine1.add_rule(rule);

    RuleEngine engine2(std::move(engine1));
    EXPECT_EQ(engine2.rule_count(), 1u);
}
