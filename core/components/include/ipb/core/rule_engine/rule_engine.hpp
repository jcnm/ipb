#pragma once

/**
 * @file rule_engine.hpp
 * @brief High-performance rule engine with compile-time regex support
 *
 * The RuleEngine provides deterministic pattern matching for routing rules:
 * - CTRE (compile-time regex) for maximum performance when available
 * - Fallback to optimized std::regex when CTRE not available
 * - Value-based condition evaluation
 * - Rule prioritization and ordering
 * - Thread-safe rule management
 *
 * Target: Sub-microsecond rule evaluation for deterministic routing
 */

#include <ipb/common/data_point.hpp>
#include <ipb/common/error.hpp>
#include <ipb/common/debug.hpp>
#include <ipb/common/platform.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ipb::core {

// Forward declarations
class RuleEngineImpl;
class PatternMatcher;

/**
 * @brief Priority levels for routing rules
 */
enum class RulePriority : uint8_t {
    LOWEST = 0,
    LOW = 64,
    NORMAL = 128,
    HIGH = 192,
    HIGHEST = 255,
    REALTIME = 254  ///< Special priority for real-time data
};

/**
 * @brief Types of rules supported by the engine
 */
enum class RuleType : uint8_t {
    STATIC,          ///< Exact address match
    PATTERN,         ///< Pattern/regex match
    PROTOCOL,        ///< Match by protocol ID
    QUALITY,         ///< Match by data quality
    VALUE,           ///< Match by value condition
    TIMESTAMP,       ///< Match by timestamp range
    COMPOSITE,       ///< Combination of multiple conditions
    CUSTOM           ///< Custom predicate function
};

/**
 * @brief Comparison operators for value conditions
 */
enum class CompareOp : uint8_t {
    EQ,     ///< Equal
    NE,     ///< Not equal
    LT,     ///< Less than
    LE,     ///< Less than or equal
    GT,     ///< Greater than
    GE,     ///< Greater than or equal
    BETWEEN ///< Between two values (inclusive)
};

/**
 * @brief Value-based condition for rule matching
 */
struct ValueCondition {
    CompareOp op = CompareOp::EQ;

    /// Reference value for comparison
    std::variant<bool, int64_t, uint64_t, double, std::string> reference;

    /// Secondary reference for BETWEEN comparisons
    std::variant<bool, int64_t, uint64_t, double, std::string> reference_high;

    /// Evaluate condition against a DataPoint value
    bool evaluate(const common::Value& value) const noexcept;
};

/**
 * @brief Result of rule evaluation
 */
struct RuleMatchResult {
    bool matched = false;
    uint32_t rule_id = 0;
    RulePriority priority = RulePriority::NORMAL;
    std::vector<std::string> target_ids;

    /// Metadata captured from pattern groups
    std::vector<std::string> captured_groups;

    explicit operator bool() const noexcept { return matched; }
};

/**
 * @brief Routing rule definition
 */
struct RoutingRule {
    /// Unique rule identifier
    uint32_t id = 0;

    /// Human-readable rule name
    std::string name;

    /// Rule type determines evaluation strategy
    RuleType type = RuleType::STATIC;

    /// Rule priority for ordering
    RulePriority priority = RulePriority::NORMAL;

    /// Whether rule is currently active
    bool enabled = true;

    // Static matching criteria
    std::vector<std::string> source_addresses;

    // Pattern matching (CTRE-optimized when available)
    std::string address_pattern;

    // Protocol-based matching
    std::vector<uint16_t> protocol_ids;

    // Quality-based matching
    std::vector<common::Quality> quality_levels;

    // Value-based matching
    std::optional<ValueCondition> value_condition;

    // Timestamp range matching
    common::Timestamp start_time;
    common::Timestamp end_time;

    // Target sinks
    std::vector<std::string> target_sink_ids;

    // Custom predicate (for CUSTOM type)
    std::function<bool(const common::DataPoint&)> custom_predicate;

    // Rule statistics (atomic for thread-safety)
    mutable std::atomic<uint64_t> match_count{0};
    mutable std::atomic<uint64_t> eval_count{0};
    mutable std::atomic<int64_t> total_eval_time_ns{0};

    // Default constructor
    RoutingRule() = default;

    // Copy constructor (atomics need explicit handling)
    RoutingRule(const RoutingRule& other)
        : id(other.id)
        , name(other.name)
        , type(other.type)
        , priority(other.priority)
        , enabled(other.enabled)
        , source_addresses(other.source_addresses)
        , address_pattern(other.address_pattern)
        , protocol_ids(other.protocol_ids)
        , quality_levels(other.quality_levels)
        , value_condition(other.value_condition)
        , start_time(other.start_time)
        , end_time(other.end_time)
        , target_sink_ids(other.target_sink_ids)
        , custom_predicate(other.custom_predicate)
        , match_count(other.match_count.load())
        , eval_count(other.eval_count.load())
        , total_eval_time_ns(other.total_eval_time_ns.load())
    {}

    // Move constructor
    RoutingRule(RoutingRule&& other) noexcept
        : id(other.id)
        , name(std::move(other.name))
        , type(other.type)
        , priority(other.priority)
        , enabled(other.enabled)
        , source_addresses(std::move(other.source_addresses))
        , address_pattern(std::move(other.address_pattern))
        , protocol_ids(std::move(other.protocol_ids))
        , quality_levels(std::move(other.quality_levels))
        , value_condition(std::move(other.value_condition))
        , start_time(other.start_time)
        , end_time(other.end_time)
        , target_sink_ids(std::move(other.target_sink_ids))
        , custom_predicate(std::move(other.custom_predicate))
        , match_count(other.match_count.load())
        , eval_count(other.eval_count.load())
        , total_eval_time_ns(other.total_eval_time_ns.load())
    {}

    // Copy assignment
    RoutingRule& operator=(const RoutingRule& other) {
        if (this != &other) {
            id = other.id;
            name = other.name;
            type = other.type;
            priority = other.priority;
            enabled = other.enabled;
            source_addresses = other.source_addresses;
            address_pattern = other.address_pattern;
            protocol_ids = other.protocol_ids;
            quality_levels = other.quality_levels;
            value_condition = other.value_condition;
            start_time = other.start_time;
            end_time = other.end_time;
            target_sink_ids = other.target_sink_ids;
            custom_predicate = other.custom_predicate;
            match_count.store(other.match_count.load());
            eval_count.store(other.eval_count.load());
            total_eval_time_ns.store(other.total_eval_time_ns.load());
        }
        return *this;
    }

    // Move assignment
    RoutingRule& operator=(RoutingRule&& other) noexcept {
        if (this != &other) {
            id = other.id;
            name = std::move(other.name);
            type = other.type;
            priority = other.priority;
            enabled = other.enabled;
            source_addresses = std::move(other.source_addresses);
            address_pattern = std::move(other.address_pattern);
            protocol_ids = std::move(other.protocol_ids);
            quality_levels = std::move(other.quality_levels);
            value_condition = std::move(other.value_condition);
            start_time = other.start_time;
            end_time = other.end_time;
            target_sink_ids = std::move(other.target_sink_ids);
            custom_predicate = std::move(other.custom_predicate);
            match_count.store(other.match_count.load());
            eval_count.store(other.eval_count.load());
            total_eval_time_ns.store(other.total_eval_time_ns.load());
        }
        return *this;
    }

    /// Check if this rule matches a data point
    RuleMatchResult evaluate(const common::DataPoint& dp) const;

    /// Get average evaluation time in nanoseconds
    double avg_eval_time_ns() const noexcept {
        auto count = eval_count.load(std::memory_order_relaxed);
        return count > 0 ?
            static_cast<double>(total_eval_time_ns.load()) / count : 0.0;
    }
};

/**
 * @brief Statistics for rule engine monitoring
 */
struct RuleEngineStats {
    std::atomic<uint64_t> total_evaluations{0};
    std::atomic<uint64_t> total_matches{0};
    std::atomic<uint64_t> cache_hits{0};
    std::atomic<uint64_t> cache_misses{0};

    std::atomic<int64_t> min_eval_time_ns{INT64_MAX};
    std::atomic<int64_t> max_eval_time_ns{0};
    std::atomic<int64_t> total_eval_time_ns{0};

    double avg_eval_time_ns() const noexcept {
        auto count = total_evaluations.load();
        return count > 0 ? static_cast<double>(total_eval_time_ns) / count : 0.0;
    }

    double match_rate() const noexcept {
        auto evals = total_evaluations.load();
        return evals > 0 ?
            static_cast<double>(total_matches) / evals * 100.0 : 0.0;
    }

    void reset() noexcept {
        total_evaluations.store(0);
        total_matches.store(0);
        cache_hits.store(0);
        cache_misses.store(0);
        min_eval_time_ns.store(INT64_MAX);
        max_eval_time_ns.store(0);
        total_eval_time_ns.store(0);
    }
};

/**
 * @brief Configuration for RuleEngine
 */
struct RuleEngineConfig {
    /// Maximum number of rules
    size_t max_rules = 10000;

    /// Enable rule caching for repeated evaluations
    bool enable_cache = true;

    /// Cache size (number of address -> result mappings)
    size_t cache_size = 65536;

    /// Cache TTL in milliseconds (0 = no expiry)
    uint32_t cache_ttl_ms = 1000;

    /// Use CTRE when available
    bool prefer_ctre = true;

    /// Pre-compile all patterns at rule addition time
    bool precompile_patterns = true;
};

/**
 * @brief High-performance rule evaluation engine
 *
 * The RuleEngine evaluates routing rules against data points with
 * deterministic, sub-microsecond performance.
 *
 * Features:
 * - CTRE compile-time regex for O(n) matching
 * - LRU cache for repeated address evaluations
 * - Priority-ordered rule evaluation
 * - Thread-safe rule management
 *
 * Example usage:
 * @code
 * RuleEngine engine;
 *
 * // Add a pattern rule
 * RoutingRule rule;
 * rule.name = "temperature_sensors";
 * rule.type = RuleType::PATTERN;
 * rule.address_pattern = "sensors/temp.*";
 * rule.target_sink_ids = {"influxdb", "kafka"};
 * engine.add_rule(rule);
 *
 * // Evaluate
 * DataPoint dp("sensors/temp1", Value{25.5});
 * auto results = engine.evaluate(dp);
 * @endcode
 */
class RuleEngine {
public:
    RuleEngine();
    explicit RuleEngine(const RuleEngineConfig& config);
    ~RuleEngine();

    // Non-copyable, movable
    RuleEngine(const RuleEngine&) = delete;
    RuleEngine& operator=(const RuleEngine&) = delete;
    RuleEngine(RuleEngine&&) noexcept;
    RuleEngine& operator=(RuleEngine&&) noexcept;

    // Rule Management

    /// Add a rule, returns assigned rule ID
    uint32_t add_rule(RoutingRule rule);

    /// Update an existing rule
    bool update_rule(uint32_t rule_id, const RoutingRule& rule);

    /// Remove a rule
    bool remove_rule(uint32_t rule_id);

    /// Enable or disable a rule
    bool set_rule_enabled(uint32_t rule_id, bool enabled);

    /// Get a rule by ID
    std::optional<RoutingRule> get_rule(uint32_t rule_id) const;

    /// Get all rules (for serialization)
    std::vector<RoutingRule> get_all_rules() const;

    /// Clear all rules
    void clear_rules();

    /// Get rule count
    size_t rule_count() const noexcept;

    // Evaluation

    /// Evaluate all rules against a data point
    std::vector<RuleMatchResult> evaluate(const common::DataPoint& dp);

    /// Evaluate and return first matching rule (fastest)
    std::optional<RuleMatchResult> evaluate_first(const common::DataPoint& dp);

    /// Evaluate rules of specific priority or higher
    std::vector<RuleMatchResult> evaluate_priority(
        const common::DataPoint& dp,
        RulePriority min_priority);

    /// Batch evaluation for multiple data points
    std::vector<std::vector<RuleMatchResult>> evaluate_batch(
        std::span<const common::DataPoint> data_points);

    // Cache Management

    /// Clear the evaluation cache
    void clear_cache();

    /// Invalidate cache entries for a specific pattern
    void invalidate_cache(std::string_view address_pattern);

    // Statistics

    /// Get current statistics
    const RuleEngineStats& stats() const noexcept;

    /// Reset statistics
    void reset_stats();

    // Configuration

    /// Get current configuration
    const RuleEngineConfig& config() const noexcept;

private:
    std::unique_ptr<RuleEngineImpl> impl_;
};

/**
 * @brief Builder pattern for creating routing rules
 */
class RuleBuilder {
public:
    RuleBuilder() = default;

    RuleBuilder& name(std::string rule_name) {
        rule_.name = std::move(rule_name);
        return *this;
    }

    RuleBuilder& priority(RulePriority prio) {
        rule_.priority = prio;
        return *this;
    }

    RuleBuilder& match_address(std::string address) {
        rule_.type = RuleType::STATIC;
        rule_.source_addresses.push_back(std::move(address));
        return *this;
    }

    RuleBuilder& match_addresses(std::vector<std::string> addresses) {
        rule_.type = RuleType::STATIC;
        rule_.source_addresses = std::move(addresses);
        return *this;
    }

    RuleBuilder& match_pattern(std::string pattern) {
        rule_.type = RuleType::PATTERN;
        rule_.address_pattern = std::move(pattern);
        return *this;
    }

    RuleBuilder& match_protocol(uint16_t protocol_id) {
        rule_.type = RuleType::PROTOCOL;
        rule_.protocol_ids.push_back(protocol_id);
        return *this;
    }

    RuleBuilder& match_protocols(std::vector<uint16_t> protocols) {
        rule_.type = RuleType::PROTOCOL;
        rule_.protocol_ids = std::move(protocols);
        return *this;
    }

    RuleBuilder& match_quality(common::Quality quality) {
        rule_.type = RuleType::QUALITY;
        rule_.quality_levels.push_back(quality);
        return *this;
    }

    RuleBuilder& match_value(ValueCondition condition) {
        rule_.type = RuleType::VALUE;
        rule_.value_condition = std::move(condition);
        return *this;
    }

    RuleBuilder& match_custom(std::function<bool(const common::DataPoint&)> predicate) {
        rule_.type = RuleType::CUSTOM;
        rule_.custom_predicate = std::move(predicate);
        return *this;
    }

    RuleBuilder& route_to(std::string sink_id) {
        rule_.target_sink_ids.push_back(std::move(sink_id));
        return *this;
    }

    RuleBuilder& route_to(std::vector<std::string> sink_ids) {
        rule_.target_sink_ids = std::move(sink_ids);
        return *this;
    }

    RoutingRule build() {
        return std::move(rule_);
    }

private:
    RoutingRule rule_;
};

} // namespace ipb::core
