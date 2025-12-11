#include <ipb/router/router_v2.hpp>
#include <algorithm>

namespace ipb::router {

// ============================================================================
// RouterV2Config
// ============================================================================

RouterV2Config RouterV2Config::from_legacy(const RouterConfig& legacy) {
    RouterV2Config config;

    // MessageBus config
    config.message_bus.dispatcher_threads = legacy.worker_thread_count;
    config.message_bus.default_buffer_size = legacy.input_queue_size;
    config.message_bus.lock_free_mode = legacy.enable_lock_free_queues;

    // RuleEngine config
    config.rule_engine.enable_cache = true;
    config.rule_engine.prefer_ctre = true;

    // Scheduler config
    config.scheduler.worker_threads = legacy.edf_scheduler_thread_count;
    config.scheduler.default_deadline_offset = legacy.default_deadline_offset;
    config.scheduler.enable_realtime = legacy.enable_realtime_scheduling;
    config.scheduler.realtime_priority = legacy.realtime_priority;

    if (legacy.enable_thread_affinity && !legacy.thread_cpu_affinity.empty()) {
        config.scheduler.cpu_affinity_start = legacy.thread_cpu_affinity[0];
    }

    // SinkRegistry config
    config.sink_registry.enable_health_check = true;
    config.sink_registry.enable_failover = true;

    // Router-specific
    config.enable_dead_letter_queue = legacy.enable_dead_letter_queue;
    config.dead_letter_sink_id = legacy.dead_letter_sink_id;

    return config;
}

// ============================================================================
// RouterV2 Implementation
// ============================================================================

RouterV2::RouterV2()
    : RouterV2(RouterV2Config{}) {}

RouterV2::RouterV2(const RouterV2Config& config)
    : config_(config)
    , message_bus_(std::make_unique<core::MessageBus>(config.message_bus))
    , rule_engine_(std::make_unique<core::RuleEngine>(config.rule_engine))
    , scheduler_(std::make_unique<core::EDFScheduler>(config.scheduler))
    , sink_registry_(std::make_unique<core::SinkRegistry>(config.sink_registry)) {}

RouterV2::~RouterV2() {
    stop();
}

RouterV2::RouterV2(RouterV2&&) noexcept = default;
RouterV2& RouterV2::operator=(RouterV2&&) noexcept = default;

// ============================================================================
// IIPBComponent Interface
// ============================================================================

common::Result<> RouterV2::start() {
    if (running_.exchange(true)) {
        return common::Result<>();  // Already running
    }

    // Start all components
    if (!message_bus_->start()) {
        running_.store(false);
        return common::Result<>(common::Result<>::ErrorCode::INTERNAL_ERROR,
                               "Failed to start MessageBus");
    }

    if (!scheduler_->start()) {
        message_bus_->stop();
        running_.store(false);
        return common::Result<>(common::Result<>::ErrorCode::INTERNAL_ERROR,
                               "Failed to start EDFScheduler");
    }

    if (!sink_registry_->start()) {
        scheduler_->stop();
        message_bus_->stop();
        running_.store(false);
        return common::Result<>(common::Result<>::ErrorCode::INTERNAL_ERROR,
                               "Failed to start SinkRegistry");
    }

    // Subscribe to routing topic
    routing_subscription_ = message_bus_->subscribe("routing/#",
        [this](const core::Message& msg) {
            handle_message(msg);
        });

    return common::Result<>();
}

common::Result<> RouterV2::stop() {
    if (!running_.exchange(false)) {
        return common::Result<>();  // Not running
    }

    // Cancel subscription
    routing_subscription_.cancel();

    // Stop components in reverse order
    sink_registry_->stop();
    scheduler_->stop();
    message_bus_->stop();

    return common::Result<>();
}

bool RouterV2::is_running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

common::Result<> RouterV2::configure(const common::ConfigurationBase& config) {
    // TODO: Implement runtime configuration
    return common::Result<>(common::Result<>::ErrorCode::INVALID_ARGUMENT,
                           "Runtime configuration not supported");
}

std::unique_ptr<common::ConfigurationBase> RouterV2::get_configuration() const {
    // TODO: Implement configuration export
    return nullptr;
}

common::Statistics RouterV2::get_statistics() const noexcept {
    common::Statistics stats;

    auto metrics = get_metrics();
    stats.total_messages = metrics.total_messages;
    stats.successful_messages = metrics.successful_routes;
    stats.failed_messages = metrics.failed_routes;

    return stats;
}

void RouterV2::reset_statistics() noexcept {
    reset_metrics();
}

bool RouterV2::is_healthy() const noexcept {
    return running_.load() &&
           message_bus_->is_running() &&
           scheduler_->is_running() &&
           sink_registry_->is_running();
}

std::string RouterV2::get_health_status() const {
    if (!running_.load()) {
        return "Router not running";
    }

    std::string status = "Healthy";

    if (!message_bus_->is_running()) {
        status = "MessageBus not running";
    } else if (!scheduler_->is_running()) {
        status = "Scheduler not running";
    } else if (!sink_registry_->is_running()) {
        status = "SinkRegistry not running";
    }

    return status;
}

// ============================================================================
// Sink Management
// ============================================================================

common::Result<> RouterV2::register_sink(std::string_view sink_id,
                                         std::shared_ptr<common::IIPBSink> sink) {
    if (sink_registry_->register_sink(sink_id, std::move(sink))) {
        return common::Result<>();
    }
    return common::Result<>(common::Result<>::ErrorCode::INVALID_ARGUMENT,
                           "Failed to register sink");
}

common::Result<> RouterV2::register_sink(std::string_view sink_id,
                                         std::shared_ptr<common::IIPBSink> sink,
                                         uint32_t weight) {
    if (sink_registry_->register_sink(sink_id, std::move(sink), weight)) {
        return common::Result<>();
    }
    return common::Result<>(common::Result<>::ErrorCode::INVALID_ARGUMENT,
                           "Failed to register sink");
}

common::Result<> RouterV2::unregister_sink(std::string_view sink_id) {
    if (sink_registry_->unregister_sink(sink_id)) {
        return common::Result<>();
    }
    return common::Result<>(common::Result<>::ErrorCode::INVALID_ARGUMENT,
                           "Sink not found");
}

std::vector<std::string> RouterV2::get_registered_sinks() const {
    return sink_registry_->get_sink_ids();
}

common::Result<> RouterV2::set_sink_weight(std::string_view sink_id, uint32_t weight) {
    if (sink_registry_->set_sink_weight(sink_id, weight)) {
        return common::Result<>();
    }
    return common::Result<>(common::Result<>::ErrorCode::INVALID_ARGUMENT,
                           "Sink not found");
}

common::Result<> RouterV2::enable_sink(std::string_view sink_id, bool enabled) {
    if (sink_registry_->set_sink_enabled(sink_id, enabled)) {
        return common::Result<>();
    }
    return common::Result<>(common::Result<>::ErrorCode::INVALID_ARGUMENT,
                           "Sink not found");
}

// ============================================================================
// Rule Management
// ============================================================================

common::Result<uint32_t> RouterV2::add_rule(const RoutingRule& rule) {
    auto core_rule = convert_rule(rule);
    uint32_t id = rule_engine_->add_rule(std::move(core_rule));
    return common::Result<uint32_t>(id);
}

uint32_t RouterV2::add_rule(core::RoutingRule rule) {
    return rule_engine_->add_rule(std::move(rule));
}

common::Result<> RouterV2::update_rule(uint32_t rule_id, const RoutingRule& rule) {
    auto core_rule = convert_rule(rule);
    if (rule_engine_->update_rule(rule_id, core_rule)) {
        return common::Result<>();
    }
    return common::Result<>(common::Result<>::ErrorCode::INVALID_ARGUMENT,
                           "Rule not found");
}

common::Result<> RouterV2::remove_rule(uint32_t rule_id) {
    if (rule_engine_->remove_rule(rule_id)) {
        return common::Result<>();
    }
    return common::Result<>(common::Result<>::ErrorCode::INVALID_ARGUMENT,
                           "Rule not found");
}

common::Result<> RouterV2::enable_rule(uint32_t rule_id, bool enabled) {
    if (rule_engine_->set_rule_enabled(rule_id, enabled)) {
        return common::Result<>();
    }
    return common::Result<>(common::Result<>::ErrorCode::INVALID_ARGUMENT,
                           "Rule not found");
}

std::vector<RoutingRule> RouterV2::get_routing_rules() const {
    auto core_rules = rule_engine_->get_all_rules();
    std::vector<RoutingRule> result;
    result.reserve(core_rules.size());

    for (const auto& rule : core_rules) {
        result.push_back(convert_rule_back(rule));
    }

    return result;
}

std::optional<RoutingRule> RouterV2::get_rule(uint32_t rule_id) const {
    auto core_rule = rule_engine_->get_rule(rule_id);
    if (core_rule) {
        return convert_rule_back(*core_rule);
    }
    return std::nullopt;
}

// ============================================================================
// Message Routing
// ============================================================================

common::Result<> RouterV2::route(const common::DataPoint& data_point) {
    if (!running_.load(std::memory_order_acquire)) {
        return common::Result<>(common::Result<>::ErrorCode::INVALID_ARGUMENT,
                               "Router not running");
    }

    // Evaluate rules
    auto matches = rule_engine_->evaluate(data_point);

    if (matches.empty()) {
        // No matching rules - check for dead letter queue
        if (config_.enable_dead_letter_queue) {
            return sink_registry_->write_to_sink(config_.dead_letter_sink_id, data_point);
        }
        return common::Result<>();  // Silently drop
    }

    return dispatch_to_sinks(data_point, matches);
}

common::Result<> RouterV2::route_with_deadline(const common::DataPoint& data_point,
                                               common::Timestamp deadline) {
    if (!running_.load(std::memory_order_acquire)) {
        return common::Result<>(common::Result<>::ErrorCode::INVALID_ARGUMENT,
                               "Router not running");
    }

    // Schedule via EDF scheduler
    auto result = scheduler_->submit(
        [this, dp = data_point]() {
            route(dp);
        },
        deadline
    );

    if (result.success) {
        return common::Result<>();
    }

    return common::Result<>(common::Result<>::ErrorCode::INTERNAL_ERROR,
                           result.error_message);
}

common::Result<> RouterV2::route_batch(std::span<const common::DataPoint> batch) {
    if (!running_.load(std::memory_order_acquire)) {
        return common::Result<>(common::Result<>::ErrorCode::INVALID_ARGUMENT,
                               "Router not running");
    }

    // Batch evaluate all rules
    auto all_matches = rule_engine_->evaluate_batch(batch);

    bool any_failed = false;

    for (size_t i = 0; i < batch.size(); ++i) {
        if (all_matches[i].empty()) {
            if (config_.enable_dead_letter_queue) {
                sink_registry_->write_to_sink(config_.dead_letter_sink_id, batch[i]);
            }
            continue;
        }

        auto result = dispatch_to_sinks(batch[i], all_matches[i]);
        if (!result.is_success()) {
            any_failed = true;
        }
    }

    if (any_failed) {
        return common::Result<>(common::Result<>::ErrorCode::INTERNAL_ERROR,
                               "Some messages failed to route");
    }

    return common::Result<>();
}

std::future<common::Result<>> RouterV2::route_async(const common::DataPoint& data_point) {
    return std::async(std::launch::async, [this, dp = data_point]() {
        return route(dp);
    });
}

// ============================================================================
// Scheduler Control
// ============================================================================

void RouterV2::set_default_deadline_offset(std::chrono::nanoseconds offset) {
    scheduler_->set_default_deadline_offset(offset);
}

std::chrono::nanoseconds RouterV2::get_default_deadline_offset() const {
    return scheduler_->config().default_deadline_offset;
}

size_t RouterV2::get_pending_task_count() const {
    return scheduler_->pending_count();
}

uint64_t RouterV2::get_missed_deadline_count() const {
    return scheduler_->missed_deadline_count();
}

// ============================================================================
// Metrics
// ============================================================================

RouterV2::Metrics RouterV2::get_metrics() const {
    Metrics metrics;

    // From scheduler
    const auto& sched_stats = scheduler_->stats();
    metrics.deadlines_met = sched_stats.deadlines_met.load();
    metrics.deadlines_missed = sched_stats.deadlines_missed.load();
    metrics.deadline_compliance_rate = sched_stats.deadline_compliance_rate();
    metrics.total_messages = sched_stats.tasks_completed.load();
    metrics.successful_routes = sched_stats.tasks_completed.load();
    metrics.failed_routes = sched_stats.tasks_failed.load();

    // From rule engine
    const auto& rule_stats = rule_engine_->stats();
    metrics.rule_evaluations = rule_stats.total_evaluations.load();
    metrics.avg_rule_eval_time_ns = rule_stats.avg_eval_time_ns();
    auto cache_total = rule_stats.cache_hits.load() + rule_stats.cache_misses.load();
    metrics.cache_hit_rate = cache_total > 0 ?
        static_cast<double>(rule_stats.cache_hits) / cache_total * 100.0 : 0.0;

    // From sink registry
    const auto& sink_stats = sink_registry_->stats();
    metrics.sink_selections = sink_stats.total_selections.load();
    metrics.failover_events = sink_stats.failover_events.load();

    // From message bus
    const auto& bus_stats = message_bus_->stats();
    metrics.messages_published = bus_stats.messages_published.load();
    metrics.messages_delivered = bus_stats.messages_delivered.load();
    metrics.queue_overflows = bus_stats.queue_overflows.load();
    metrics.avg_routing_time_us = bus_stats.avg_latency_us();

    return metrics;
}

void RouterV2::reset_metrics() {
    message_bus_->reset_stats();
    rule_engine_->reset_stats();
    scheduler_->reset_stats();
    sink_registry_->reset_stats();
}

// ============================================================================
// Internal Methods
// ============================================================================

void RouterV2::handle_message(const core::Message& msg) {
    if (msg.type == core::Message::Type::DATA_POINT) {
        route(msg.payload);
    } else if (msg.type == core::Message::Type::DATA_BATCH) {
        route_batch(msg.batch_payload);
    } else if (msg.type == core::Message::Type::DEADLINE_TASK) {
        common::Timestamp deadline(std::chrono::nanoseconds(msg.deadline_ns));
        route_with_deadline(msg.payload, deadline);
    }
}

common::Result<> RouterV2::dispatch_to_sinks(
        const common::DataPoint& dp,
        const std::vector<core::RuleMatchResult>& matches) {

    bool any_success = false;
    bool any_failed = false;

    for (const auto& match : matches) {
        if (!match.matched || match.target_ids.empty()) {
            continue;
        }

        // Determine load balance strategy from rule priority
        // Higher priority rules use failover, others use round-robin
        auto strategy = (match.priority >= core::RulePriority::HIGH) ?
            core::LoadBalanceStrategy::FAILOVER :
            core::LoadBalanceStrategy::ROUND_ROBIN;

        auto result = sink_registry_->write_with_load_balancing(
            match.target_ids, dp, strategy);

        if (result.is_success()) {
            any_success = true;
        } else {
            any_failed = true;
        }
    }

    if (any_success) {
        return common::Result<>();
    }

    if (any_failed) {
        // Send to dead letter queue
        if (config_.enable_dead_letter_queue) {
            sink_registry_->write_to_sink(config_.dead_letter_sink_id, dp);
        }
        return common::Result<>(common::Result<>::ErrorCode::INTERNAL_ERROR,
                               "Failed to dispatch to any sink");
    }

    return common::Result<>();
}

core::RoutingRule RouterV2::convert_rule(const RoutingRule& legacy) {
    core::RoutingRule rule;

    rule.name = legacy.name;
    rule.enabled = legacy.enabled;

    // Convert priority
    rule.priority = static_cast<core::RulePriority>(static_cast<uint8_t>(legacy.priority));

    // Convert rule type and conditions
    switch (legacy.type) {
        case RuleType::STATIC:
            rule.type = core::RuleType::STATIC;
            rule.source_addresses = legacy.source_addresses;
            break;

        case RuleType::REGEX_PATTERN:
            rule.type = core::RuleType::PATTERN;
            rule.address_pattern = legacy.address_pattern;
            break;

        case RuleType::PROTOCOL_BASED:
            rule.type = core::RuleType::PROTOCOL;
            rule.protocol_ids = legacy.protocol_ids;
            break;

        case RuleType::QUALITY_BASED:
            rule.type = core::RuleType::QUALITY;
            rule.quality_levels = legacy.quality_levels;
            break;

        case RuleType::VALUE_BASED:
            rule.type = core::RuleType::VALUE;
            if (!legacy.value_conditions.empty()) {
                const auto& vc = legacy.value_conditions[0];
                core::ValueCondition cond;
                cond.op = static_cast<core::CompareOp>(static_cast<uint8_t>(vc.op));
                // TODO: Convert reference value properly
                rule.value_condition = cond;
            }
            break;

        case RuleType::TIMESTAMP_BASED:
            rule.type = core::RuleType::TIMESTAMP;
            rule.start_time = legacy.start_time;
            rule.end_time = legacy.end_time;
            break;

        case RuleType::CUSTOM_LOGIC:
            rule.type = core::RuleType::CUSTOM;
            rule.custom_predicate = legacy.custom_condition;
            break;

        default:
            rule.type = core::RuleType::STATIC;
            break;
    }

    rule.target_sink_ids = legacy.target_sink_ids;

    return rule;
}

RoutingRule RouterV2::convert_rule_back(const core::RoutingRule& rule) {
    RoutingRule legacy;

    legacy.rule_id = rule.id;
    legacy.name = rule.name;
    legacy.enabled = rule.enabled;
    legacy.priority = static_cast<RoutingPriority>(static_cast<uint8_t>(rule.priority));

    switch (rule.type) {
        case core::RuleType::STATIC:
            legacy.type = RuleType::STATIC;
            legacy.source_addresses = rule.source_addresses;
            break;

        case core::RuleType::PATTERN:
            legacy.type = RuleType::REGEX_PATTERN;
            legacy.address_pattern = rule.address_pattern;
            break;

        case core::RuleType::PROTOCOL:
            legacy.type = RuleType::PROTOCOL_BASED;
            legacy.protocol_ids = rule.protocol_ids;
            break;

        case core::RuleType::QUALITY:
            legacy.type = RuleType::QUALITY_BASED;
            legacy.quality_levels = rule.quality_levels;
            break;

        case core::RuleType::VALUE:
            legacy.type = RuleType::VALUE_BASED;
            break;

        case core::RuleType::TIMESTAMP:
            legacy.type = RuleType::TIMESTAMP_BASED;
            legacy.start_time = rule.start_time;
            legacy.end_time = rule.end_time;
            break;

        case core::RuleType::CUSTOM:
            legacy.type = RuleType::CUSTOM_LOGIC;
            legacy.custom_condition = rule.custom_predicate;
            break;

        default:
            legacy.type = RuleType::STATIC;
            break;
    }

    legacy.target_sink_ids = rule.target_sink_ids;

    return legacy;
}

// ============================================================================
// RouterV2Factory
// ============================================================================

std::unique_ptr<RouterV2> RouterV2Factory::create() {
    return std::make_unique<RouterV2>();
}

std::unique_ptr<RouterV2> RouterV2Factory::create(const RouterV2Config& config) {
    return std::make_unique<RouterV2>(config);
}

std::unique_ptr<RouterV2> RouterV2Factory::create_high_throughput() {
    RouterV2Config config;

    // Maximize throughput
    config.message_bus.dispatcher_threads = std::thread::hardware_concurrency();
    config.message_bus.default_buffer_size = 131072;  // 128K
    config.message_bus.lock_free_mode = true;
    config.message_bus.priority_dispatch = false;  // Skip priority for speed

    config.rule_engine.enable_cache = true;
    config.rule_engine.cache_size = 131072;
    config.rule_engine.prefer_ctre = true;

    config.scheduler.worker_threads = std::thread::hardware_concurrency();
    config.scheduler.enable_realtime = false;

    config.sink_registry.default_strategy = core::LoadBalanceStrategy::ROUND_ROBIN;

    return std::make_unique<RouterV2>(config);
}

std::unique_ptr<RouterV2> RouterV2Factory::create_low_latency() {
    RouterV2Config config;

    // Minimize latency
    config.message_bus.dispatcher_threads = 2;
    config.message_bus.default_buffer_size = 4096;
    config.message_bus.lock_free_mode = true;

    config.rule_engine.enable_cache = true;
    config.rule_engine.cache_size = 16384;
    config.rule_engine.prefer_ctre = true;

    config.scheduler.worker_threads = 2;
    config.scheduler.default_deadline_offset = std::chrono::microseconds(100);
    config.scheduler.check_interval = std::chrono::microseconds(10);

    config.sink_registry.default_strategy = core::LoadBalanceStrategy::LEAST_LATENCY;

    return std::make_unique<RouterV2>(config);
}

std::unique_ptr<RouterV2> RouterV2Factory::create_realtime() {
    RouterV2Config config;

    // Real-time guarantees
    config.message_bus.dispatcher_threads = 4;
    config.message_bus.default_buffer_size = 16384;
    config.message_bus.lock_free_mode = true;
    config.message_bus.priority_dispatch = true;

    config.rule_engine.enable_cache = true;
    config.rule_engine.prefer_ctre = true;
    config.rule_engine.precompile_patterns = true;

    config.scheduler.worker_threads = 4;
    config.scheduler.enable_realtime = true;
    config.scheduler.realtime_priority = 80;
    config.scheduler.default_deadline_offset = std::chrono::microseconds(500);

    config.sink_registry.default_strategy = core::LoadBalanceStrategy::FAILOVER;
    config.sink_registry.enable_failover = true;

    return std::make_unique<RouterV2>(config);
}

} // namespace ipb::router
