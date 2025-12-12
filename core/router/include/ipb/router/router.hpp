#pragma once

/**
 * @file router.hpp
 * @brief High-performance message router using decomposed core components
 *
 * The Router delegates to specialized components:
 * - MessageBus: for pub/sub communication
 * - RuleEngine: for pattern matching (CTRE-optimized)
 * - EDFScheduler: for deadline-based scheduling
 * - SinkRegistry: for sink management and load balancing
 *
 * Features:
 * - Each component independently testable
 * - Performance >5M msg/s
 * - Improved determinism with CTRE
 * - Clear separation of concerns
 * - Comprehensive error handling with hierarchical codes
 * - Full tracing and logging support
 */

#include <ipb/common/interfaces.hpp>
#include <ipb/common/data_point.hpp>
#include <ipb/common/dataset.hpp>
#include <ipb/common/endpoint.hpp>
#include <ipb/common/error.hpp>
#include <ipb/common/debug.hpp>
#include <ipb/common/platform.hpp>

#include <ipb/core/message_bus/message_bus.hpp>
#include <ipb/core/rule_engine/rule_engine.hpp>
#include <ipb/core/scheduler/edf_scheduler.hpp>
#include <ipb/core/sink_registry/sink_registry.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <functional>
#include <future>
#include <optional>

namespace ipb::router {

// ============================================================================
// ROUTING TYPES
// ============================================================================

/**
 * @brief Routing rule types
 */
enum class RuleType : uint8_t {
    STATIC = 0,         ///< Static address-based routing
    PROTOCOL_BASED,     ///< Route based on protocol ID
    REGEX_PATTERN,      ///< Route based on regex pattern matching
    QUALITY_BASED,      ///< Route based on data quality
    TIMESTAMP_BASED,    ///< Route based on timestamp ranges
    VALUE_BASED,        ///< Route based on data value conditions
    CUSTOM_LOGIC,       ///< Custom logic function
    LOAD_BALANCING,     ///< Load balancing across multiple sinks
    FAILOVER,           ///< Failover to backup sinks
    BROADCAST           ///< Broadcast to all matching sinks
};

/**
 * @brief Get rule type name
 */
constexpr std::string_view rule_type_name(RuleType type) noexcept {
    switch (type) {
        case RuleType::STATIC:         return "STATIC";
        case RuleType::PROTOCOL_BASED: return "PROTOCOL_BASED";
        case RuleType::REGEX_PATTERN:  return "REGEX_PATTERN";
        case RuleType::QUALITY_BASED:  return "QUALITY_BASED";
        case RuleType::TIMESTAMP_BASED: return "TIMESTAMP_BASED";
        case RuleType::VALUE_BASED:    return "VALUE_BASED";
        case RuleType::CUSTOM_LOGIC:   return "CUSTOM_LOGIC";
        case RuleType::LOAD_BALANCING: return "LOAD_BALANCING";
        case RuleType::FAILOVER:       return "FAILOVER";
        case RuleType::BROADCAST:      return "BROADCAST";
        default:                       return "UNKNOWN";
    }
}

/**
 * @brief Routing priority levels
 */
enum class RoutingPriority : uint8_t {
    LOWEST = 0,
    LOW = 64,
    NORMAL = 128,
    HIGH = 192,
    HIGHEST = 255,
    REALTIME = 254     ///< Special priority for real-time data
};

/**
 * @brief Load balancing strategies
 */
enum class LoadBalanceStrategy : uint8_t {
    ROUND_ROBIN = 0,
    WEIGHTED_ROUND_ROBIN,
    LEAST_CONNECTIONS,
    LEAST_LATENCY,
    HASH_BASED,
    RANDOM,
    FAILOVER,
    CUSTOM
};

/**
 * @brief Value comparison operators for value-based routing
 */
enum class ValueOperator : uint8_t {
    EQUAL,
    NOT_EQUAL,
    LESS_THAN,
    LESS_EQUAL,
    GREATER_THAN,
    GREATER_EQUAL,
    CONTAINS,
    REGEX_MATCH
};

/**
 * @brief Routing condition for value-based routing
 */
struct ValueCondition {
    ValueOperator op = ValueOperator::EQUAL;
    common::Value reference_value;
    std::string regex_pattern;

    bool evaluate(const common::Value& value) const;
};

/**
 * @brief Routing rule definition
 */
struct RoutingRule {
    uint32_t rule_id = 0;
    std::string name;
    RuleType type = RuleType::STATIC;
    RoutingPriority priority = RoutingPriority::NORMAL;
    bool enabled = true;

    // Rule conditions
    std::vector<std::string> source_addresses;        ///< Static addresses
    std::vector<uint16_t> protocol_ids;               ///< Protocol IDs
    std::string address_pattern;                      ///< Regex pattern for addresses
    std::vector<common::Quality> quality_levels;      ///< Quality conditions
    common::Timestamp start_time;                     ///< Timestamp range start
    common::Timestamp end_time;                       ///< Timestamp range end
    std::vector<ValueCondition> value_conditions;     ///< Value-based conditions

    // Target sinks
    std::vector<std::string> target_sink_ids;
    LoadBalanceStrategy load_balance_strategy = LoadBalanceStrategy::ROUND_ROBIN;
    std::vector<uint32_t> sink_weights;               ///< For weighted load balancing

    // Failover settings
    bool enable_failover = false;
    std::vector<std::string> backup_sink_ids;
    std::chrono::milliseconds failover_timeout{5000};

    // Custom logic function
    std::function<bool(const common::DataPoint&)> custom_condition;
    std::function<std::vector<std::string>(const common::DataPoint&)> custom_target_selector;

    // Performance settings
    bool enable_batching = false;
    uint32_t batch_size = 100;
    std::chrono::milliseconds batch_timeout{10};

    // Statistics (atomic for thread safety)
    mutable std::atomic<uint64_t> match_count{0};
    mutable std::atomic<uint64_t> success_count{0};
    mutable std::atomic<uint64_t> failure_count{0};
    mutable std::atomic<int64_t> total_processing_time_ns{0};

    // Default constructor
    RoutingRule() = default;

    // Copy constructor (atomics need explicit handling)
    RoutingRule(const RoutingRule& other)
        : rule_id(other.rule_id)
        , name(other.name)
        , type(other.type)
        , priority(other.priority)
        , enabled(other.enabled)
        , source_addresses(other.source_addresses)
        , protocol_ids(other.protocol_ids)
        , address_pattern(other.address_pattern)
        , quality_levels(other.quality_levels)
        , start_time(other.start_time)
        , end_time(other.end_time)
        , value_conditions(other.value_conditions)
        , target_sink_ids(other.target_sink_ids)
        , load_balance_strategy(other.load_balance_strategy)
        , sink_weights(other.sink_weights)
        , enable_failover(other.enable_failover)
        , backup_sink_ids(other.backup_sink_ids)
        , failover_timeout(other.failover_timeout)
        , custom_condition(other.custom_condition)
        , custom_target_selector(other.custom_target_selector)
        , enable_batching(other.enable_batching)
        , batch_size(other.batch_size)
        , batch_timeout(other.batch_timeout)
        , match_count(other.match_count.load())
        , success_count(other.success_count.load())
        , failure_count(other.failure_count.load())
        , total_processing_time_ns(other.total_processing_time_ns.load())
    {}

    // Move constructor
    RoutingRule(RoutingRule&& other) noexcept
        : rule_id(other.rule_id)
        , name(std::move(other.name))
        , type(other.type)
        , priority(other.priority)
        , enabled(other.enabled)
        , source_addresses(std::move(other.source_addresses))
        , protocol_ids(std::move(other.protocol_ids))
        , address_pattern(std::move(other.address_pattern))
        , quality_levels(std::move(other.quality_levels))
        , start_time(other.start_time)
        , end_time(other.end_time)
        , value_conditions(std::move(other.value_conditions))
        , target_sink_ids(std::move(other.target_sink_ids))
        , load_balance_strategy(other.load_balance_strategy)
        , sink_weights(std::move(other.sink_weights))
        , enable_failover(other.enable_failover)
        , backup_sink_ids(std::move(other.backup_sink_ids))
        , failover_timeout(other.failover_timeout)
        , custom_condition(std::move(other.custom_condition))
        , custom_target_selector(std::move(other.custom_target_selector))
        , enable_batching(other.enable_batching)
        , batch_size(other.batch_size)
        , batch_timeout(other.batch_timeout)
        , match_count(other.match_count.load())
        , success_count(other.success_count.load())
        , failure_count(other.failure_count.load())
        , total_processing_time_ns(other.total_processing_time_ns.load())
    {}

    // Copy assignment
    RoutingRule& operator=(const RoutingRule& other) {
        if (this != &other) {
            rule_id = other.rule_id;
            name = other.name;
            type = other.type;
            priority = other.priority;
            enabled = other.enabled;
            source_addresses = other.source_addresses;
            protocol_ids = other.protocol_ids;
            address_pattern = other.address_pattern;
            quality_levels = other.quality_levels;
            start_time = other.start_time;
            end_time = other.end_time;
            value_conditions = other.value_conditions;
            target_sink_ids = other.target_sink_ids;
            load_balance_strategy = other.load_balance_strategy;
            sink_weights = other.sink_weights;
            enable_failover = other.enable_failover;
            backup_sink_ids = other.backup_sink_ids;
            failover_timeout = other.failover_timeout;
            custom_condition = other.custom_condition;
            custom_target_selector = other.custom_target_selector;
            enable_batching = other.enable_batching;
            batch_size = other.batch_size;
            batch_timeout = other.batch_timeout;
            match_count.store(other.match_count.load());
            success_count.store(other.success_count.load());
            failure_count.store(other.failure_count.load());
            total_processing_time_ns.store(other.total_processing_time_ns.load());
        }
        return *this;
    }

    // Move assignment
    RoutingRule& operator=(RoutingRule&& other) noexcept {
        if (this != &other) {
            rule_id = other.rule_id;
            name = std::move(other.name);
            type = other.type;
            priority = other.priority;
            enabled = other.enabled;
            source_addresses = std::move(other.source_addresses);
            protocol_ids = std::move(other.protocol_ids);
            address_pattern = std::move(other.address_pattern);
            quality_levels = std::move(other.quality_levels);
            start_time = other.start_time;
            end_time = other.end_time;
            value_conditions = std::move(other.value_conditions);
            target_sink_ids = std::move(other.target_sink_ids);
            load_balance_strategy = other.load_balance_strategy;
            sink_weights = std::move(other.sink_weights);
            enable_failover = other.enable_failover;
            backup_sink_ids = std::move(other.backup_sink_ids);
            failover_timeout = other.failover_timeout;
            custom_condition = std::move(other.custom_condition);
            custom_target_selector = std::move(other.custom_target_selector);
            enable_batching = other.enable_batching;
            batch_size = other.batch_size;
            batch_timeout = other.batch_timeout;
            match_count.store(other.match_count.load());
            success_count.store(other.success_count.load());
            failure_count.store(other.failure_count.load());
            total_processing_time_ns.store(other.total_processing_time_ns.load());
        }
        return *this;
    }

    // Validation
    IPB_NODISCARD bool is_valid() const noexcept;

    // Evaluation
    bool matches(const common::DataPoint& data_point) const;
    std::vector<std::string> get_target_sinks(const common::DataPoint& data_point) const;
};

// ============================================================================
// ROUTER CONFIGURATION
// ============================================================================

/**
 * @brief Router configuration combining all component configs
 */
struct RouterConfig {
    // MessageBus settings
    core::MessageBusConfig message_bus;

    // RuleEngine settings
    core::RuleEngineConfig rule_engine;

    // EDFScheduler settings
    core::EDFSchedulerConfig scheduler;

    // SinkRegistry settings
    core::SinkRegistryConfig sink_registry;

    // Router-specific settings
    bool enable_dead_letter_queue = true;
    std::string dead_letter_sink_id = "dead_letter";

    // Debug/logging settings
    common::debug::LogLevel log_level = common::debug::LogLevel::INFO;
    bool enable_tracing = true;

    // Validation
    IPB_NODISCARD common::Result<> validate() const;

    // Factory methods for common configurations
    static RouterConfig default_config();
    static RouterConfig high_throughput();
    static RouterConfig low_latency();
    static RouterConfig realtime();
};

// ============================================================================
// ROUTER CLASS
// ============================================================================

/**
 * @brief High-performance message router
 *
 * Delegates to specialized core components for:
 * - Message passing (MessageBus)
 * - Rule evaluation (RuleEngine with CTRE)
 * - Deadline scheduling (EDFScheduler)
 * - Sink management (SinkRegistry)
 *
 * Example usage:
 * @code
 * Router router;
 * router.start();
 *
 * // Register sinks
 * router.register_sink("kafka", kafka_sink);
 * router.register_sink("influx", influx_sink);
 *
 * // Add routing rules
 * router.add_rule(RuleBuilder()
 *     .name("temperature_sensors")
 *     .match_pattern("sensors/temp.*")
 *     .route_to({"kafka", "influx"})
 *     .build());
 *
 * // Route messages
 * router.route(DataPoint("sensors/temp1", Value{25.5}));
 * @endcode
 */
class Router : public common::IIPBComponent {
public:
    static constexpr std::string_view COMPONENT_NAME = "IPBRouter";
    static constexpr std::string_view COMPONENT_VERSION = "2.0.0";

    Router();
    explicit Router(const RouterConfig& config);
    ~Router() override;

    // Non-copyable, movable
    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;
    Router(Router&&) noexcept;
    Router& operator=(Router&&) noexcept;

    // =========================================================================
    // IIPBComponent Interface
    // =========================================================================

    IPB_NODISCARD common::Result<> start() override;
    IPB_NODISCARD common::Result<> stop() override;
    bool is_running() const noexcept override;

    IPB_NODISCARD common::Result<> configure(const common::ConfigurationBase& config) override;
    std::unique_ptr<common::ConfigurationBase> get_configuration() const override;

    common::Statistics get_statistics() const noexcept override;
    void reset_statistics() noexcept override;

    bool is_healthy() const noexcept override;
    std::string get_health_status() const override;

    std::string_view component_name() const noexcept override { return COMPONENT_NAME; }
    std::string_view component_version() const noexcept override { return COMPONENT_VERSION; }

    // =========================================================================
    // Sink Management
    // =========================================================================

    /**
     * @brief Register a sink for routing
     * @param sink_id Unique identifier for the sink
     * @param sink The sink implementation
     * @return Success or error with code INVALID_ARGUMENT, ALREADY_EXISTS
     */
    IPB_NODISCARD common::Result<> register_sink(
        std::string_view sink_id,
        std::shared_ptr<common::IIPBSink> sink);

    /**
     * @brief Register a sink with weight for load balancing
     */
    IPB_NODISCARD common::Result<> register_sink(
        std::string_view sink_id,
        std::shared_ptr<common::IIPBSink> sink,
        uint32_t weight);

    /**
     * @brief Unregister a sink
     * @return Success or SINK_NOT_FOUND
     */
    IPB_NODISCARD common::Result<> unregister_sink(std::string_view sink_id);

    /**
     * @brief Get all registered sink IDs
     */
    std::vector<std::string> get_registered_sinks() const;

    /**
     * @brief Set sink weight for load balancing
     */
    IPB_NODISCARD common::Result<> set_sink_weight(std::string_view sink_id, uint32_t weight);

    /**
     * @brief Enable/disable a sink
     */
    IPB_NODISCARD common::Result<> enable_sink(std::string_view sink_id, bool enabled = true);

    // =========================================================================
    // Rule Management
    // =========================================================================

    /**
     * @brief Add a routing rule
     * @return Rule ID on success or error with RULE_INVALID, RULE_CONFLICT
     */
    IPB_NODISCARD common::Result<uint32_t> add_rule(const RoutingRule& rule);

    /**
     * @brief Add a routing rule using core format
     */
    uint32_t add_rule(core::RoutingRule rule);

    /**
     * @brief Update an existing rule
     * @return Success or RULE_NOT_FOUND, RULE_INVALID
     */
    IPB_NODISCARD common::Result<> update_rule(uint32_t rule_id, const RoutingRule& rule);

    /**
     * @brief Remove a rule
     * @return Success or RULE_NOT_FOUND
     */
    IPB_NODISCARD common::Result<> remove_rule(uint32_t rule_id);

    /**
     * @brief Enable/disable a rule
     */
    IPB_NODISCARD common::Result<> enable_rule(uint32_t rule_id, bool enabled = true);

    /**
     * @brief Get all routing rules
     */
    std::vector<RoutingRule> get_routing_rules() const;

    /**
     * @brief Get rule by ID
     */
    std::optional<RoutingRule> get_rule(uint32_t rule_id) const;

    // =========================================================================
    // Message Routing
    // =========================================================================

    /**
     * @brief Route a single message
     * @return Success or error with context
     *
     * Possible errors:
     * - INVALID_STATE: Router not running
     * - NO_MATCHING_RULE: No rule matched (if dead letter disabled)
     * - ALL_SINKS_FAILED: All target sinks failed
     */
    IPB_NODISCARD common::Result<> route(const common::DataPoint& data_point);

    /**
     * @brief Route a message with explicit deadline
     */
    IPB_NODISCARD common::Result<> route_with_deadline(
        const common::DataPoint& data_point,
        common::Timestamp deadline);

    /**
     * @brief Route a batch of messages
     */
    IPB_NODISCARD common::Result<> route_batch(std::span<const common::DataPoint> batch);

    /**
     * @brief Route asynchronously
     */
    std::future<common::Result<>> route_async(const common::DataPoint& data_point);

    // =========================================================================
    // Scheduler Control
    // =========================================================================

    /**
     * @brief Set default deadline offset for messages without explicit deadline
     */
    void set_default_deadline_offset(std::chrono::nanoseconds offset);

    /**
     * @brief Get default deadline offset
     */
    std::chrono::nanoseconds get_default_deadline_offset() const;

    /**
     * @brief Get number of pending scheduled tasks
     */
    size_t get_pending_task_count() const;

    /**
     * @brief Get number of missed deadlines
     */
    uint64_t get_missed_deadline_count() const;

    // =========================================================================
    // Performance Metrics
    // =========================================================================

    struct Metrics {
        // Throughput
        uint64_t total_messages = 0;
        uint64_t successful_routes = 0;
        uint64_t failed_routes = 0;
        double messages_per_second = 0.0;

        // Latency
        double avg_routing_time_us = 0.0;
        double min_routing_time_us = 0.0;
        double max_routing_time_us = 0.0;

        // Deadlines
        uint64_t deadlines_met = 0;
        uint64_t deadlines_missed = 0;
        double deadline_compliance_rate = 100.0;

        // Rule engine
        uint64_t rule_evaluations = 0;
        double avg_rule_eval_time_ns = 0.0;
        double cache_hit_rate = 0.0;

        // Sink registry
        uint64_t sink_selections = 0;
        uint64_t failover_events = 0;

        // Message bus
        uint64_t messages_published = 0;
        uint64_t messages_delivered = 0;
        uint64_t queue_overflows = 0;
    };

    /**
     * @brief Get aggregated metrics from all components
     */
    Metrics get_metrics() const;

    /**
     * @brief Reset all metrics
     */
    void reset_metrics();

    // =========================================================================
    // Direct Component Access (for advanced usage)
    // =========================================================================

    core::MessageBus& message_bus() noexcept { return *message_bus_; }
    const core::MessageBus& message_bus() const noexcept { return *message_bus_; }

    core::RuleEngine& rule_engine() noexcept { return *rule_engine_; }
    const core::RuleEngine& rule_engine() const noexcept { return *rule_engine_; }

    core::EDFScheduler& scheduler() noexcept { return *scheduler_; }
    const core::EDFScheduler& scheduler() const noexcept { return *scheduler_; }

    core::SinkRegistry& sink_registry() noexcept { return *sink_registry_; }
    const core::SinkRegistry& sink_registry() const noexcept { return *sink_registry_; }

private:
    RouterConfig config_;

    // Core components
    std::unique_ptr<core::MessageBus> message_bus_;
    std::unique_ptr<core::RuleEngine> rule_engine_;
    std::unique_ptr<core::EDFScheduler> scheduler_;
    std::unique_ptr<core::SinkRegistry> sink_registry_;

    // State
    std::atomic<bool> running_{false};

    // Subscriptions
    core::Subscription routing_subscription_;

    // Internal routing logic
    void handle_message(const core::Message& msg);
    common::Result<> dispatch_to_sinks(
        const common::DataPoint& dp,
        const std::vector<core::RuleMatchResult>& matches);

    // Rule conversion helpers
    static core::RoutingRule convert_rule(const RoutingRule& legacy);
    static RoutingRule convert_rule_back(const core::RoutingRule& rule);

    // Validation helpers
    common::Result<> validate_sink_id(std::string_view sink_id) const;
    common::Result<> validate_rule(const RoutingRule& rule) const;
};

// ============================================================================
// ROUTER FACTORY
// ============================================================================

/**
 * @brief Factory for creating Router instances with preset configurations
 */
class RouterFactory {
public:
    /**
     * @brief Create router with default configuration
     */
    static std::unique_ptr<Router> create();

    /**
     * @brief Create router with custom configuration
     */
    static std::unique_ptr<Router> create(const RouterConfig& config);

    // Preset factories
    static std::unique_ptr<Router> create_high_throughput();
    static std::unique_ptr<Router> create_low_latency();
    static std::unique_ptr<Router> create_realtime();
};

// ============================================================================
// ROUTING RULE BUILDER
// ============================================================================

/**
 * @brief Fluent builder for constructing routing rules
 */
class RuleBuilder {
public:
    RuleBuilder() = default;

    RuleBuilder& name(std::string rule_name);
    RuleBuilder& priority(RoutingPriority prio);
    RuleBuilder& enabled(bool is_enabled = true);

    // Condition builders
    RuleBuilder& match_address(const std::string& address);
    RuleBuilder& match_addresses(const std::vector<std::string>& addresses);
    RuleBuilder& match_protocol(uint16_t protocol_id);
    RuleBuilder& match_protocols(const std::vector<uint16_t>& protocol_ids);
    RuleBuilder& match_pattern(const std::string& regex_pattern);
    RuleBuilder& match_quality(common::Quality quality);
    RuleBuilder& match_time_range(common::Timestamp start, common::Timestamp end);
    RuleBuilder& match_value_condition(const ValueCondition& condition);
    RuleBuilder& match_custom(std::function<bool(const common::DataPoint&)> condition);

    // Target builders
    RuleBuilder& route_to(const std::string& sink_id);
    RuleBuilder& route_to(const std::vector<std::string>& sink_ids);
    RuleBuilder& load_balance(LoadBalanceStrategy strategy);
    RuleBuilder& with_weights(const std::vector<uint32_t>& weights);
    RuleBuilder& with_failover(const std::vector<std::string>& backup_sinks);
    RuleBuilder& custom_target_selector(
        std::function<std::vector<std::string>(const common::DataPoint&)> selector);

    // Performance builders
    RuleBuilder& enable_batching(uint32_t batch_size, std::chrono::milliseconds timeout);

    /**
     * @brief Build the routing rule
     * @throws std::invalid_argument if rule is not valid
     */
    RoutingRule build();

    /**
     * @brief Try to build the routing rule
     * @return Result with rule or error
     */
    common::Result<RoutingRule> try_build();

private:
    RoutingRule rule_;
    uint32_t rule_id_counter_ = 1;
};

} // namespace ipb::router
