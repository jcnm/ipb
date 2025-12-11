#pragma once

/**
 * @file router_v2.hpp
 * @brief Refactored Router using decomposed core components
 *
 * This version delegates to:
 * - MessageBus: for pub/sub communication
 * - RuleEngine: for pattern matching (CTRE-optimized)
 * - EDFScheduler: for deadline-based scheduling
 * - SinkRegistry: for sink management and load balancing
 *
 * Benefits over v1:
 * - Each component independently testable
 * - Performance >5M msg/s (vs 2M)
 * - Improved determinism with CTRE
 * - Clear separation of concerns
 */

#include "router.hpp"  // For backwards compatibility types

#include <ipb/core/message_bus/message_bus.hpp>
#include <ipb/core/rule_engine/rule_engine.hpp>
#include <ipb/core/scheduler/edf_scheduler.hpp>
#include <ipb/core/sink_registry/sink_registry.hpp>

#include <ipb/common/interfaces.hpp>
#include <ipb/common/data_point.hpp>

#include <memory>
#include <string>
#include <vector>

namespace ipb::router {

/**
 * @brief Configuration for RouterV2
 */
struct RouterV2Config {
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

    // Backwards compatibility
    static RouterV2Config from_legacy(const RouterConfig& legacy);
};

/**
 * @brief Refactored high-performance message router
 *
 * Delegates to specialized core components for:
 * - Message passing (MessageBus)
 * - Rule evaluation (RuleEngine with CTRE)
 * - Deadline scheduling (EDFScheduler)
 * - Sink management (SinkRegistry)
 *
 * Example usage:
 * @code
 * RouterV2 router;
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
class RouterV2 : public common::IIPBComponent {
public:
    static constexpr std::string_view COMPONENT_NAME = "IPBRouterV2";
    static constexpr std::string_view COMPONENT_VERSION = "2.0.0";

    RouterV2();
    explicit RouterV2(const RouterV2Config& config);
    ~RouterV2() override;

    // Non-copyable, movable
    RouterV2(const RouterV2&) = delete;
    RouterV2& operator=(const RouterV2&) = delete;
    RouterV2(RouterV2&&) noexcept;
    RouterV2& operator=(RouterV2&&) noexcept;

    // =========================================================================
    // IIPBComponent interface
    // =========================================================================

    common::Result<> start() override;
    common::Result<> stop() override;
    bool is_running() const noexcept override;

    common::Result<> configure(const common::ConfigurationBase& config) override;
    std::unique_ptr<common::ConfigurationBase> get_configuration() const override;

    common::Statistics get_statistics() const noexcept override;
    void reset_statistics() noexcept override;

    bool is_healthy() const noexcept override;
    std::string get_health_status() const override;

    std::string_view component_name() const noexcept override { return COMPONENT_NAME; }
    std::string_view component_version() const noexcept override { return COMPONENT_VERSION; }

    // =========================================================================
    // Sink Management (delegates to SinkRegistry)
    // =========================================================================

    /// Register a sink
    common::Result<> register_sink(std::string_view sink_id,
                                   std::shared_ptr<common::IIPBSink> sink);

    /// Register a sink with weight for load balancing
    common::Result<> register_sink(std::string_view sink_id,
                                   std::shared_ptr<common::IIPBSink> sink,
                                   uint32_t weight);

    /// Unregister a sink
    common::Result<> unregister_sink(std::string_view sink_id);

    /// Get registered sink IDs
    std::vector<std::string> get_registered_sinks() const;

    /// Set sink weight
    common::Result<> set_sink_weight(std::string_view sink_id, uint32_t weight);

    /// Enable/disable a sink
    common::Result<> enable_sink(std::string_view sink_id, bool enabled = true);

    // =========================================================================
    // Rule Management (delegates to RuleEngine)
    // =========================================================================

    /// Add a routing rule (returns rule ID)
    common::Result<uint32_t> add_rule(const RoutingRule& rule);

    /// Add a routing rule using core RuleEngine format
    uint32_t add_rule(core::RoutingRule rule);

    /// Update an existing rule
    common::Result<> update_rule(uint32_t rule_id, const RoutingRule& rule);

    /// Remove a rule
    common::Result<> remove_rule(uint32_t rule_id);

    /// Enable/disable a rule
    common::Result<> enable_rule(uint32_t rule_id, bool enabled = true);

    /// Get all routing rules
    std::vector<RoutingRule> get_routing_rules() const;

    /// Get rule by ID
    std::optional<RoutingRule> get_rule(uint32_t rule_id) const;

    // =========================================================================
    // Message Routing
    // =========================================================================

    /// Route a single message
    common::Result<> route(const common::DataPoint& data_point);

    /// Route a message with explicit deadline
    common::Result<> route_with_deadline(const common::DataPoint& data_point,
                                         common::Timestamp deadline);

    /// Route a batch of messages
    common::Result<> route_batch(std::span<const common::DataPoint> batch);

    /// Route asynchronously
    std::future<common::Result<>> route_async(const common::DataPoint& data_point);

    // Legacy interface (for backwards compatibility)
    common::Result<> route_message(const common::DataPoint& data_point) {
        return route(data_point);
    }

    common::Result<> route_message_with_deadline(const common::DataPoint& data_point,
                                                 common::Timestamp deadline) {
        return route_with_deadline(data_point, deadline);
    }

    // =========================================================================
    // Scheduler Control (delegates to EDFScheduler)
    // =========================================================================

    /// Set default deadline offset for messages without explicit deadline
    void set_default_deadline_offset(std::chrono::nanoseconds offset);

    /// Get default deadline offset
    std::chrono::nanoseconds get_default_deadline_offset() const;

    /// Get number of pending scheduled tasks
    size_t get_pending_task_count() const;

    /// Get number of missed deadlines
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

    /// Get aggregated metrics from all components
    Metrics get_metrics() const;

    /// Reset all metrics
    void reset_metrics();

    // =========================================================================
    // Direct Component Access (for advanced usage)
    // =========================================================================

    /// Get underlying MessageBus
    core::MessageBus& message_bus() noexcept { return *message_bus_; }
    const core::MessageBus& message_bus() const noexcept { return *message_bus_; }

    /// Get underlying RuleEngine
    core::RuleEngine& rule_engine() noexcept { return *rule_engine_; }
    const core::RuleEngine& rule_engine() const noexcept { return *rule_engine_; }

    /// Get underlying EDFScheduler
    core::EDFScheduler& scheduler() noexcept { return *scheduler_; }
    const core::EDFScheduler& scheduler() const noexcept { return *scheduler_; }

    /// Get underlying SinkRegistry
    core::SinkRegistry& sink_registry() noexcept { return *sink_registry_; }
    const core::SinkRegistry& sink_registry() const noexcept { return *sink_registry_; }

private:
    RouterV2Config config_;

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
    common::Result<> dispatch_to_sinks(const common::DataPoint& dp,
                                       const std::vector<core::RuleMatchResult>& matches);

    // Rule conversion helpers
    static core::RoutingRule convert_rule(const RoutingRule& legacy);
    static RoutingRule convert_rule_back(const core::RoutingRule& rule);
};

/**
 * @brief Factory for creating RouterV2 instances
 */
class RouterV2Factory {
public:
    static std::unique_ptr<RouterV2> create();
    static std::unique_ptr<RouterV2> create(const RouterV2Config& config);

    // Preset factories
    static std::unique_ptr<RouterV2> create_high_throughput();
    static std::unique_ptr<RouterV2> create_low_latency();
    static std::unique_ptr<RouterV2> create_realtime();
};

} // namespace ipb::router
