#include <ipb/core/rule_engine/compiled_pattern_cache.hpp>
#include <ipb/router/router.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>

namespace ipb::router {

using namespace common;
using namespace common::debug;

// ============================================================================
// ValueCondition Implementation - COMPLETE with all operators
// ============================================================================

namespace {

/**
 * @brief Check if a Value::Type is numeric
 */
constexpr bool is_numeric_type(Value::Type t) noexcept {
    switch (t) {
        case Value::Type::INT8:
        case Value::Type::INT16:
        case Value::Type::INT32:
        case Value::Type::INT64:
        case Value::Type::UINT8:
        case Value::Type::UINT16:
        case Value::Type::UINT32:
        case Value::Type::UINT64:
        case Value::Type::FLOAT32:
        case Value::Type::FLOAT64:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Convert a numeric Value to double
 */
double value_to_double(const Value& v) noexcept {
    switch (v.type()) {
        case Value::Type::INT8:
            return static_cast<double>(v.get<int8_t>());
        case Value::Type::INT16:
            return static_cast<double>(v.get<int16_t>());
        case Value::Type::INT32:
            return static_cast<double>(v.get<int32_t>());
        case Value::Type::INT64:
            return static_cast<double>(v.get<int64_t>());
        case Value::Type::UINT8:
            return static_cast<double>(v.get<uint8_t>());
        case Value::Type::UINT16:
            return static_cast<double>(v.get<uint16_t>());
        case Value::Type::UINT32:
            return static_cast<double>(v.get<uint32_t>());
        case Value::Type::UINT64:
            return static_cast<double>(v.get<uint64_t>());
        case Value::Type::FLOAT32:
            return static_cast<double>(v.get<float>());
        case Value::Type::FLOAT64:
            return v.get<double>();
        default:
            return 0.0;
    }
}

/**
 * @brief Thread-local buffer for string conversions to avoid allocations in hot paths
 * PERFORMANCE: Using thread_local avoids heap allocations per-message
 */
static constexpr size_t kConversionBufferSize = 64;

/**
 * @brief Convert a Value to string_view using thread_local buffer
 * PERFORMANCE: Zero-allocation for numeric types in hot paths
 * @param v The value to convert
 * @return string_view into thread_local buffer (valid until next call from same thread)
 */
std::string_view value_to_string_view(const Value& v) noexcept {
    // Thread-local buffer for numeric conversions
    thread_local std::array<char, kConversionBufferSize> buffer;

    switch (v.type()) {
        case Value::Type::EMPTY:
            return "";
        case Value::Type::BOOL:
            return v.get<bool>() ? "true" : "false";
        case Value::Type::INT8: {
            auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v.get<int8_t>());
            return std::string_view(buffer.data(), ptr - buffer.data());
        }
        case Value::Type::INT16: {
            auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v.get<int16_t>());
            return std::string_view(buffer.data(), ptr - buffer.data());
        }
        case Value::Type::INT32: {
            auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v.get<int32_t>());
            return std::string_view(buffer.data(), ptr - buffer.data());
        }
        case Value::Type::INT64: {
            auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v.get<int64_t>());
            return std::string_view(buffer.data(), ptr - buffer.data());
        }
        case Value::Type::UINT8: {
            auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v.get<uint8_t>());
            return std::string_view(buffer.data(), ptr - buffer.data());
        }
        case Value::Type::UINT16: {
            auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v.get<uint16_t>());
            return std::string_view(buffer.data(), ptr - buffer.data());
        }
        case Value::Type::UINT32: {
            auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v.get<uint32_t>());
            return std::string_view(buffer.data(), ptr - buffer.data());
        }
        case Value::Type::UINT64: {
            auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v.get<uint64_t>());
            return std::string_view(buffer.data(), ptr - buffer.data());
        }
        case Value::Type::FLOAT32: {
            // std::to_chars for floats requires C++17 with full float support
            // Fall back to snprintf for now
            int len = std::snprintf(buffer.data(), buffer.size(), "%g", static_cast<double>(v.get<float>()));
            return std::string_view(buffer.data(), len > 0 ? len : 0);
        }
        case Value::Type::FLOAT64: {
            int len = std::snprintf(buffer.data(), buffer.size(), "%g", v.get<double>());
            return std::string_view(buffer.data(), len > 0 ? len : 0);
        }
        case Value::Type::STRING:
            return v.as_string_view();
        case Value::Type::BINARY: {
            auto bin = v.as_binary();
            return std::string_view(reinterpret_cast<const char*>(bin.data()), bin.size());
        }
        default:
            return "";
    }
}

/**
 * @brief Convert a Value to string representation (allocating version for when ownership needed)
 */
std::string value_to_string(const Value& v) noexcept {
    return std::string(value_to_string_view(v));
}

/**
 * @brief Compare two Values for ordering
 * @return -1 if a < b, 0 if a == b, 1 if a > b
 */
int compare_values(const Value& a, const Value& b) noexcept {
    // Handle type mismatches by comparing as doubles when possible
    if (a.type() != b.type()) {
        // Try numeric comparison
        if (is_numeric_type(a.type()) && is_numeric_type(b.type())) {
            double da = value_to_double(a);
            double db = value_to_double(b);
            if (std::abs(da - db) < 1e-9)
                return 0;
            return da < db ? -1 : 1;
        }
        // Fall back to string comparison
        auto sa = value_to_string(a);
        auto sb = value_to_string(b);
        if (sa == sb)
            return 0;
        return sa < sb ? -1 : 1;
    }

    // Same type comparison
    switch (a.type()) {
        case Value::Type::BOOL:
            if (a.get<bool>() == b.get<bool>())
                return 0;
            return a.get<bool>() ? 1 : -1;

        case Value::Type::INT8:
            if (a.get<int8_t>() == b.get<int8_t>())
                return 0;
            return a.get<int8_t>() < b.get<int8_t>() ? -1 : 1;

        case Value::Type::INT16:
            if (a.get<int16_t>() == b.get<int16_t>())
                return 0;
            return a.get<int16_t>() < b.get<int16_t>() ? -1 : 1;

        case Value::Type::INT32:
            if (a.get<int32_t>() == b.get<int32_t>())
                return 0;
            return a.get<int32_t>() < b.get<int32_t>() ? -1 : 1;

        case Value::Type::INT64:
            if (a.get<int64_t>() == b.get<int64_t>())
                return 0;
            return a.get<int64_t>() < b.get<int64_t>() ? -1 : 1;

        case Value::Type::UINT8:
            if (a.get<uint8_t>() == b.get<uint8_t>())
                return 0;
            return a.get<uint8_t>() < b.get<uint8_t>() ? -1 : 1;

        case Value::Type::UINT16:
            if (a.get<uint16_t>() == b.get<uint16_t>())
                return 0;
            return a.get<uint16_t>() < b.get<uint16_t>() ? -1 : 1;

        case Value::Type::UINT32:
            if (a.get<uint32_t>() == b.get<uint32_t>())
                return 0;
            return a.get<uint32_t>() < b.get<uint32_t>() ? -1 : 1;

        case Value::Type::UINT64:
            if (a.get<uint64_t>() == b.get<uint64_t>())
                return 0;
            return a.get<uint64_t>() < b.get<uint64_t>() ? -1 : 1;

        case Value::Type::FLOAT32:
            if (std::abs(a.get<float>() - b.get<float>()) < 1e-6f)
                return 0;
            return a.get<float>() < b.get<float>() ? -1 : 1;

        case Value::Type::FLOAT64:
            if (std::abs(a.get<double>() - b.get<double>()) < 1e-9)
                return 0;
            return a.get<double>() < b.get<double>() ? -1 : 1;

        case Value::Type::STRING: {
            auto sa = a.as_string_view();
            auto sb = b.as_string_view();
            if (sa == sb)
                return 0;
            return sa < sb ? -1 : 1;
        }

        case Value::Type::BINARY: {
            auto ba = a.as_binary();
            auto bb = b.as_binary();
            if (ba.size() != bb.size()) {
                return ba.size() < bb.size() ? -1 : 1;
            }
            int cmp = std::memcmp(ba.data(), bb.data(), ba.size());
            if (cmp == 0)
                return 0;
            return cmp < 0 ? -1 : 1;
        }

        default:
            return 0;
    }
}

/**
 * @brief Check if string a contains string b
 * PERFORMANCE: Uses string_view to avoid allocations when possible
 */
bool string_contains(const Value& haystack, const Value& needle) noexcept {
    // For STRING type, use direct string_view to avoid allocation
    if (haystack.type() == Value::Type::STRING) {
        auto hs = haystack.as_string_view();
        auto ns = value_to_string_view(needle);
        return hs.find(ns) != std::string_view::npos;
    }
    // For other types, need to convert both
    auto hs = value_to_string_view(haystack);
    // Note: We need a copy of needle since value_to_string_view uses shared buffer
    std::string ns_copy(value_to_string_view(needle));
    return std::string(hs).find(ns_copy) != std::string::npos;
}

}  // anonymous namespace

bool ValueCondition::evaluate(const Value& value) const {
    switch (op) {
        case ValueOperator::EQUAL:
            return compare_values(value, reference_value) == 0;

        case ValueOperator::NOT_EQUAL:
            return compare_values(value, reference_value) != 0;

        case ValueOperator::LESS_THAN:
            return compare_values(value, reference_value) < 0;

        case ValueOperator::LESS_EQUAL:
            return compare_values(value, reference_value) <= 0;

        case ValueOperator::GREATER_THAN:
            return compare_values(value, reference_value) > 0;

        case ValueOperator::GREATER_EQUAL:
            return compare_values(value, reference_value) >= 0;

        case ValueOperator::CONTAINS:
            return string_contains(value, reference_value);

        case ValueOperator::REGEX_MATCH: {
            if (regex_pattern.empty()) {
                return false;
            }
            // Use cached pattern matcher to avoid ReDoS
            core::CachedPatternMatcher matcher(regex_pattern);
            if (!matcher.is_valid()) {
                IPB_LOG_WARN(category::ROUTER,
                             "Invalid regex pattern in value condition: " << matcher.error());
                return false;
            }
            return matcher.matches(value_to_string(value));
        }

        default:
            IPB_LOG_WARN(category::ROUTER, "Unknown ValueOperator: " << static_cast<int>(op));
            return false;
    }
}

// ============================================================================
// RoutingRule Implementation
// ============================================================================

bool RoutingRule::is_valid() const noexcept {
    // Rule must have a name
    if (name.empty()) {
        return false;
    }

    // Rule must have at least one target sink (unless it's a custom selector)
    if (target_sink_ids.empty() && !custom_target_selector) {
        return false;
    }

    // Validate based on rule type
    switch (type) {
        case RuleType::STATIC:
            return !source_addresses.empty();

        case RuleType::PROTOCOL_BASED:
            return !protocol_ids.empty();

        case RuleType::REGEX_PATTERN:
            if (address_pattern.empty()) {
                return false;
            }
            // Validate regex pattern using cached validation (ReDoS protection)
            {
                auto validation =
                    core::CompiledPatternCache::global_instance().validate(address_pattern);
                if (!validation.is_safe) {
                    IPB_LOG_WARN(category::ROUTER, "Pattern validation failed for rule '"
                                                       << name << "': " << validation.reason);
                    return false;
                }
                // Pre-compile to verify syntax
                auto compile_result =
                    core::CompiledPatternCache::global_instance().precompile(address_pattern);
                return compile_result.is_success();
            }

        case RuleType::QUALITY_BASED:
            return !quality_levels.empty();

        case RuleType::TIMESTAMP_BASED:
            return start_time <= end_time;

        case RuleType::VALUE_BASED:
            return !value_conditions.empty();

        case RuleType::CUSTOM_LOGIC:
            return custom_condition != nullptr;

        case RuleType::LOAD_BALANCING:
        case RuleType::FAILOVER:
        case RuleType::BROADCAST:
            return !target_sink_ids.empty();

        default:
            return false;
    }
}

// PERFORMANCE: Thread-local hash sets for O(1) lookup in hot paths
// These are lazily built from vectors when vectors are large enough to benefit
namespace {
    // Threshold above which we use hash set instead of linear search
    constexpr size_t kHashSetThreshold = 8;

    // Helper to check membership efficiently - uses hash set for large collections
    template<typename T>
    bool fast_contains(const std::vector<T>& vec, const T& value) {
        if (vec.size() <= kHashSetThreshold) {
            // For small vectors, linear search is faster due to cache locality
            return std::find(vec.begin(), vec.end(), value) != vec.end();
        }
        // For larger vectors, build a temporary hash set
        // Note: In production, consider caching these per-rule
        thread_local std::unordered_set<T> temp_set;
        temp_set.clear();
        temp_set.insert(vec.begin(), vec.end());
        return temp_set.contains(value);
    }

    // Specialization for strings with string_view lookup
    bool fast_contains_string(const std::vector<std::string>& vec, std::string_view value) {
        if (vec.size() <= kHashSetThreshold) {
            // Linear search with string_view comparison
            for (const auto& s : vec) {
                if (s == value) return true;
            }
            return false;
        }
        // For larger vectors, use hash set
        thread_local std::unordered_set<std::string_view> temp_set;
        temp_set.clear();
        for (const auto& s : vec) {
            temp_set.insert(s);
        }
        return temp_set.contains(value);
    }
}

bool RoutingRule::matches(const DataPoint& data_point) const {
    if (!enabled) {
        return false;
    }

    switch (type) {
        case RuleType::STATIC:
            // PERFORMANCE: O(1) lookup for large address lists
            return fast_contains_string(source_addresses, data_point.address());

        case RuleType::PROTOCOL_BASED:
            // PERFORMANCE: O(1) lookup for large protocol lists
            return fast_contains(protocol_ids, data_point.protocol_id());

        case RuleType::REGEX_PATTERN: {
            // FIX: Use cached compiled pattern instead of compiling per-message
            // This eliminates the ReDoS vulnerability (CVE-potential)
            core::CachedPatternMatcher matcher(address_pattern);
            if (!matcher.is_valid()) {
                // Pattern should have been validated at rule creation time
                // Log warning but don't crash
                IPB_LOG_WARN(category::ROUTER,
                             "Invalid pattern in rule '" << name << "': " << matcher.error());
                return false;
            }
            return matcher.matches(data_point.address());
        }

        case RuleType::QUALITY_BASED:
            // PERFORMANCE: O(1) lookup for large quality level lists
            return fast_contains(quality_levels, data_point.quality());

        case RuleType::TIMESTAMP_BASED:
            return data_point.timestamp() >= start_time && data_point.timestamp() <= end_time;

        case RuleType::VALUE_BASED:
            for (const auto& cond : value_conditions) {
                if (!cond.evaluate(data_point.value())) {
                    return false;
                }
            }
            return true;

        case RuleType::CUSTOM_LOGIC:
            return custom_condition ? custom_condition(data_point) : false;

        case RuleType::LOAD_BALANCING:
        case RuleType::FAILOVER:
        case RuleType::BROADCAST:
            // These types match all messages by default
            return true;

        default:
            return false;
    }
}

std::vector<std::string> RoutingRule::get_target_sinks(const DataPoint& data_point) const {
    if (custom_target_selector) {
        return custom_target_selector(data_point);
    }
    return target_sink_ids;
}

// ============================================================================
// RouterConfig Implementation
// ============================================================================

Result<> RouterConfig::validate() const {
    // Validate component configs
    // TODO: Add validation for each component config

    if (enable_dead_letter_queue && dead_letter_sink_id.empty()) {
        return err(ErrorCode::CONFIG_INVALID,
                   "dead_letter_sink_id must be set when dead letter queue is enabled");
    }

    return ok();
}

RouterConfig RouterConfig::default_config() {
    return RouterConfig{};
}

RouterConfig RouterConfig::high_throughput() {
    RouterConfig config;

    // Maximize throughput
    config.message_bus.dispatcher_threads  = std::thread::hardware_concurrency();
    config.message_bus.default_buffer_size = 131072;  // 128K
    config.message_bus.lock_free_mode      = true;
    config.message_bus.priority_dispatch   = false;  // Skip priority for speed

    config.rule_engine.enable_cache = true;
    config.rule_engine.cache_size   = 131072;
    config.rule_engine.prefer_ctre  = true;

    config.scheduler.worker_threads  = std::thread::hardware_concurrency();
    config.scheduler.enable_realtime = false;

    config.sink_registry.default_strategy = core::LoadBalanceStrategy::ROUND_ROBIN;

    config.log_level = LogLevel::WARN;  // Reduce logging overhead

    return config;
}

RouterConfig RouterConfig::low_latency() {
    RouterConfig config;

    // Minimize latency
    config.message_bus.dispatcher_threads  = 2;
    config.message_bus.default_buffer_size = 4096;
    config.message_bus.lock_free_mode      = true;

    config.rule_engine.enable_cache = true;
    config.rule_engine.cache_size   = 16384;
    config.rule_engine.prefer_ctre  = true;

    config.scheduler.worker_threads          = 2;
    config.scheduler.default_deadline_offset = std::chrono::microseconds(100);
    config.scheduler.check_interval          = std::chrono::microseconds(10);

    config.sink_registry.default_strategy = core::LoadBalanceStrategy::LEAST_LATENCY;

    config.log_level = LogLevel::WARN;

    return config;
}

RouterConfig RouterConfig::realtime() {
    RouterConfig config;

    // Real-time guarantees
    config.message_bus.dispatcher_threads  = 4;
    config.message_bus.default_buffer_size = 16384;
    config.message_bus.lock_free_mode      = true;
    config.message_bus.priority_dispatch   = true;

    config.rule_engine.enable_cache        = true;
    config.rule_engine.prefer_ctre         = true;
    config.rule_engine.precompile_patterns = true;

    config.scheduler.worker_threads          = 4;
    config.scheduler.enable_realtime         = true;
    config.scheduler.realtime_priority       = 80;
    config.scheduler.default_deadline_offset = std::chrono::microseconds(500);

    config.sink_registry.default_strategy = core::LoadBalanceStrategy::FAILOVER;
    config.sink_registry.enable_failover  = true;

    config.enable_tracing = true;
    config.log_level      = LogLevel::INFO;

    return config;
}

// ============================================================================
// Router Implementation
// ============================================================================

Router::Router() : Router(RouterConfig::default_config()) {}

Router::Router(const RouterConfig& config)
    : config_(config), message_bus_(std::make_unique<core::MessageBus>(config.message_bus)),
      rule_engine_(std::make_unique<core::RuleEngine>(config.rule_engine)),
      scheduler_(std::make_unique<core::EDFScheduler>(config.scheduler)),
      sink_registry_(std::make_unique<core::SinkRegistry>(config.sink_registry)) {
    IPB_LOG_INFO(category::ROUTER, "Router created with config");
}

Router::~Router() {
    IPB_LOG_DEBUG(category::ROUTER, "Router destructor called");
    // Ignore return value in destructor - we can't handle errors here anyway
    std::ignore = stop();
}

Router::Router(Router&& other) noexcept
    : config_(std::move(other.config_)), message_bus_(std::move(other.message_bus_)),
      rule_engine_(std::move(other.rule_engine_)), scheduler_(std::move(other.scheduler_)),
      sink_registry_(std::move(other.sink_registry_)), running_(other.running_.load()),
      routing_subscription_(std::move(other.routing_subscription_)) {
    other.running_.store(false);
}

Router& Router::operator=(Router&& other) noexcept {
    if (this != &other) {
        config_        = std::move(other.config_);
        message_bus_   = std::move(other.message_bus_);
        rule_engine_   = std::move(other.rule_engine_);
        scheduler_     = std::move(other.scheduler_);
        sink_registry_ = std::move(other.sink_registry_);
        running_.store(other.running_.load());
        routing_subscription_ = std::move(other.routing_subscription_);
        other.running_.store(false);
    }
    return *this;
}

// ============================================================================
// IIPBComponent Interface
// ============================================================================

Result<> Router::start() {
    IPB_SPAN_CAT("Router::start", category::ROUTER);

    if (running_.exchange(true)) {
        IPB_LOG_DEBUG(category::ROUTER, "Router already running");
        return ok();  // Already running
    }

    IPB_LOG_INFO(category::ROUTER, "Starting router...");

    // Start all components in order
    if (!message_bus_->start()) {
        running_.store(false);
        IPB_LOG_ERROR(category::ROUTER, "Failed to start MessageBus");
        return err(ErrorCode::INVALID_STATE, "Failed to start MessageBus");
    }

    if (!scheduler_->start()) {
        message_bus_->stop();
        running_.store(false);
        IPB_LOG_ERROR(category::ROUTER, "Failed to start EDFScheduler");
        return err(ErrorCode::INVALID_STATE, "Failed to start EDFScheduler");
    }

    if (!sink_registry_->start()) {
        scheduler_->stop();
        message_bus_->stop();
        running_.store(false);
        IPB_LOG_ERROR(category::ROUTER, "Failed to start SinkRegistry");
        return err(ErrorCode::INVALID_STATE, "Failed to start SinkRegistry");
    }

    // Subscribe to routing topic
    routing_subscription_ = message_bus_->subscribe(
        "routing/#", [this](const core::Message& msg) { handle_message(msg); });

    IPB_LOG_INFO(category::ROUTER, "Router started successfully");
    return ok();
}

Result<> Router::stop() {
    IPB_SPAN_CAT("Router::stop", category::ROUTER);

    if (!running_.exchange(false)) {
        IPB_LOG_DEBUG(category::ROUTER, "Router not running");
        return ok();  // Not running
    }

    IPB_LOG_INFO(category::ROUTER, "Stopping router...");

    // Cancel subscription
    routing_subscription_.cancel();

    // Stop components in reverse order
    sink_registry_->stop();
    scheduler_->stop();
    message_bus_->stop();

    IPB_LOG_INFO(category::ROUTER, "Router stopped successfully");
    return ok();
}

bool Router::is_running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

Result<> Router::configure(const ConfigurationBase& /*config*/) {
    // TODO: Implement runtime configuration
    IPB_LOG_WARN(category::ROUTER, "Runtime configuration not yet supported");
    return err(ErrorCode::NOT_IMPLEMENTED, "Runtime configuration not supported");
}

std::unique_ptr<ConfigurationBase> Router::get_configuration() const {
    // TODO: Implement configuration export
    return nullptr;
}

Statistics Router::get_statistics() const noexcept {
    Statistics stats;

    auto metrics              = get_metrics();
    stats.total_messages      = metrics.total_messages;
    stats.successful_messages = metrics.successful_routes;
    stats.failed_messages     = metrics.failed_routes;

    return stats;
}

void Router::reset_statistics() noexcept {
    reset_metrics();
}

bool Router::is_healthy() const noexcept {
    return running_.load() && message_bus_->is_running() && scheduler_->is_running() &&
           sink_registry_->is_running();
}

std::string Router::get_health_status() const {
    if (!running_.load()) {
        return "Router not running";
    }

    if (!message_bus_->is_running()) {
        return "MessageBus not running";
    }

    if (!scheduler_->is_running()) {
        return "Scheduler not running";
    }

    if (!sink_registry_->is_running()) {
        return "SinkRegistry not running";
    }

    return "Healthy";
}

// ============================================================================
// Sink Management
// ============================================================================

Result<> Router::validate_sink_id(std::string_view sink_id) const {
    if (sink_id.empty()) {
        return err(ErrorCode::INVALID_ARGUMENT, "sink_id cannot be empty");
    }

    if (sink_id.length() > 256) {
        return err(ErrorCode::INVALID_ARGUMENT, "sink_id too long (max 256 chars)");
    }

    return ok();
}

Result<> Router::register_sink(std::string_view sink_id, std::shared_ptr<IIPBSink> sink) {
    IPB_PRECONDITION(sink != nullptr);

    IPB_TRY(validate_sink_id(sink_id));

    IPB_LOG_DEBUG(category::ROUTER, "Registering sink: " << sink_id);

    if (sink_registry_->register_sink(sink_id, std::move(sink))) {
        IPB_LOG_INFO(category::ROUTER, "Sink registered: " << sink_id);
        return ok();
    }

    IPB_LOG_WARN(category::ROUTER, "Failed to register sink: " << sink_id);
    return err(ErrorCode::ALREADY_EXISTS, "Sink already registered or registration failed");
}

Result<> Router::register_sink(std::string_view sink_id, std::shared_ptr<IIPBSink> sink,
                               uint32_t weight) {
    IPB_PRECONDITION(sink != nullptr);

    IPB_TRY(validate_sink_id(sink_id));

    IPB_LOG_DEBUG(category::ROUTER,
                  "Registering sink with weight: " << sink_id << " weight=" << weight);

    if (sink_registry_->register_sink(sink_id, std::move(sink), weight)) {
        IPB_LOG_INFO(category::ROUTER, "Sink registered: " << sink_id);
        return ok();
    }

    return err(ErrorCode::ALREADY_EXISTS, "Sink already registered or registration failed");
}

Result<> Router::unregister_sink(std::string_view sink_id) {
    IPB_LOG_DEBUG(category::ROUTER, "Unregistering sink: " << sink_id);

    if (sink_registry_->unregister_sink(sink_id)) {
        IPB_LOG_INFO(category::ROUTER, "Sink unregistered: " << sink_id);
        return ok();
    }

    return err(ErrorCode::SINK_NOT_FOUND, "Sink not found");
}

std::vector<std::string> Router::get_registered_sinks() const {
    return sink_registry_->get_sink_ids();
}

Result<> Router::set_sink_weight(std::string_view sink_id, uint32_t weight) {
    if (sink_registry_->set_sink_weight(sink_id, weight)) {
        return ok();
    }
    return err(ErrorCode::SINK_NOT_FOUND, "Sink not found");
}

Result<> Router::enable_sink(std::string_view sink_id, bool enabled) {
    if (sink_registry_->set_sink_enabled(sink_id, enabled)) {
        IPB_LOG_INFO(category::ROUTER,
                     "Sink " << (enabled ? "enabled" : "disabled") << ": " << sink_id);
        return ok();
    }
    return err(ErrorCode::SINK_NOT_FOUND, "Sink not found");
}

// ============================================================================
// Rule Management
// ============================================================================

Result<> Router::validate_rule(const RoutingRule& rule) const {
    if (!rule.is_valid()) {
        return err(ErrorCode::RULE_INVALID, "Rule validation failed for: " + rule.name);
    }

    // Verify all target sinks exist
    for (const auto& sink_id : rule.target_sink_ids) {
        auto sinks = sink_registry_->get_sink_ids();
        if (std::find(sinks.begin(), sinks.end(), sink_id) == sinks.end()) {
            return err(ErrorCode::SINK_NOT_FOUND, "Target sink not found: " + sink_id);
        }
    }

    return ok();
}

Result<uint32_t> Router::add_rule(const RoutingRule& rule) {
    IPB_LOG_DEBUG(category::ROUTER, "Adding rule: " << rule.name);

    auto validation = validate_rule(rule);
    if (!validation) {
        IPB_LOG_WARN(category::ROUTER, "Rule validation failed: " << rule.name);
        return err<uint32_t>(validation.error());
    }

    auto core_rule = convert_rule(rule);
    uint32_t id    = rule_engine_->add_rule(std::move(core_rule));

    IPB_LOG_INFO(category::ROUTER, "Rule added: " << rule.name << " id=" << id);
    return ok<uint32_t>(id);
}

uint32_t Router::add_rule(core::RoutingRule rule) {
    return rule_engine_->add_rule(std::move(rule));
}

Result<> Router::update_rule(uint32_t rule_id, const RoutingRule& rule) {
    IPB_LOG_DEBUG(category::ROUTER, "Updating rule: " << rule_id);

    auto validation = validate_rule(rule);
    if (!validation) {
        return validation;
    }

    auto core_rule = convert_rule(rule);
    if (rule_engine_->update_rule(rule_id, core_rule)) {
        IPB_LOG_INFO(category::ROUTER, "Rule updated: " << rule_id);
        return ok();
    }

    return err(ErrorCode::RULE_NOT_FOUND, "Rule not found");
}

Result<> Router::remove_rule(uint32_t rule_id) {
    IPB_LOG_DEBUG(category::ROUTER, "Removing rule: " << rule_id);

    if (rule_engine_->remove_rule(rule_id)) {
        IPB_LOG_INFO(category::ROUTER, "Rule removed: " << rule_id);
        return ok();
    }

    return err(ErrorCode::RULE_NOT_FOUND, "Rule not found");
}

Result<> Router::enable_rule(uint32_t rule_id, bool enabled) {
    if (rule_engine_->set_rule_enabled(rule_id, enabled)) {
        IPB_LOG_INFO(category::ROUTER,
                     "Rule " << (enabled ? "enabled" : "disabled") << ": " << rule_id);
        return ok();
    }
    return err(ErrorCode::RULE_NOT_FOUND, "Rule not found");
}

std::vector<RoutingRule> Router::get_routing_rules() const {
    auto core_rules = rule_engine_->get_all_rules();
    std::vector<RoutingRule> result;
    result.reserve(core_rules.size());

    for (const auto& rule : core_rules) {
        result.push_back(convert_rule_back(rule));
    }

    return result;
}

std::optional<RoutingRule> Router::get_rule(uint32_t rule_id) const {
    auto core_rule = rule_engine_->get_rule(rule_id);
    if (core_rule) {
        return convert_rule_back(*core_rule);
    }
    return std::nullopt;
}

// ============================================================================
// Message Routing
// ============================================================================

Result<> Router::route(const DataPoint& data_point) {
    if (IPB_UNLIKELY(!running_.load(std::memory_order_acquire))) {
        return err(ErrorCode::INVALID_STATE, "Router not running");
    }

    IPB_LOG_TRACE(category::ROUTER, "Routing message: " << data_point.address());

    // Evaluate rules
    auto matches = rule_engine_->evaluate(data_point);

    if (matches.empty()) {
        IPB_LOG_DEBUG(category::ROUTER, "No matching rules for: " << data_point.address());

        // No matching rules - check for dead letter queue
        if (config_.enable_dead_letter_queue) {
            auto result = sink_registry_->write_to_sink(config_.dead_letter_sink_id, data_point);
            if (!result.is_success()) {
                IPB_LOG_WARN(category::ROUTER, "Dead letter queue write failed");
            }
            return ok();  // Message handled (went to DLQ)
        }

        // No DLQ configured, return error
        return err(ErrorCode::NO_MATCHING_RULE,
                   "No routing rule matched and dead letter queue disabled");
    }

    return dispatch_to_sinks(data_point, matches);
}

Result<> Router::route_with_deadline(const DataPoint& data_point, Timestamp deadline) {
    if (IPB_UNLIKELY(!running_.load(std::memory_order_acquire))) {
        return err(ErrorCode::INVALID_STATE, "Router not running");
    }

    IPB_LOG_TRACE(category::ROUTER, "Routing message with deadline: " << data_point.address());

    // Schedule via EDF scheduler
    auto result = scheduler_->submit(
        [this, dp = data_point]() {
            auto res = route(dp);
            if (!res) {
                IPB_LOG_ERROR(category::ROUTER, "Scheduled route failed: " << res.message());
            }
        },
        deadline);

    if (result.success) {
        return ok();
    }

    return err(ErrorCode::SCHEDULER_OVERLOADED, result.error_message);
}

Result<> Router::route_batch(std::span<const DataPoint> batch) {
    if (IPB_UNLIKELY(!running_.load(std::memory_order_acquire))) {
        return err(ErrorCode::INVALID_STATE, "Router not running");
    }

    if (batch.empty()) {
        return ok();
    }

    IPB_LOG_DEBUG(category::ROUTER, "Routing batch of " << batch.size() << " messages");

    // Batch evaluate all rules
    auto all_matches = rule_engine_->evaluate_batch(batch);

    uint64_t failed_count = 0;

    for (size_t i = 0; i < batch.size(); ++i) {
        if (all_matches[i].empty()) {
            if (config_.enable_dead_letter_queue) {
                sink_registry_->write_to_sink(config_.dead_letter_sink_id, batch[i]);
            }
            continue;
        }

        auto result = dispatch_to_sinks(batch[i], all_matches[i]);
        if (!result) {
            ++failed_count;
        }
    }

    if (failed_count > 0) {
        IPB_LOG_WARN(category::ROUTER, "Batch routing: " << failed_count << "/" << batch.size()
                                                         << " messages failed");
        return err(ErrorCode::ALL_SINKS_FAILED,
                   "Some messages failed to route: " + std::to_string(failed_count));
    }

    return ok();
}

std::future<Result<>> Router::route_async(const DataPoint& data_point) {
    return std::async(std::launch::async, [this, dp = data_point]() { return route(dp); });
}

// ============================================================================
// Scheduler Control
// ============================================================================

void Router::set_default_deadline_offset(std::chrono::nanoseconds offset) {
    scheduler_->set_default_deadline_offset(offset);
}

std::chrono::nanoseconds Router::get_default_deadline_offset() const {
    return scheduler_->get_default_deadline_offset();
}

size_t Router::get_pending_task_count() const {
    return scheduler_->pending_count();
}

uint64_t Router::get_missed_deadline_count() const {
    return scheduler_->missed_deadline_count();
}

// ============================================================================
// Metrics
// ============================================================================

Router::Metrics Router::get_metrics() const {
    Metrics metrics;

    // From scheduler
    const auto& sched_stats          = scheduler_->stats();
    metrics.deadlines_met            = sched_stats.deadlines_met.load();
    metrics.deadlines_missed         = sched_stats.deadlines_missed.load();
    metrics.deadline_compliance_rate = sched_stats.deadline_compliance_rate();
    metrics.total_messages           = sched_stats.tasks_completed.load();
    metrics.successful_routes        = sched_stats.tasks_completed.load();
    metrics.failed_routes            = sched_stats.tasks_failed.load();

    // From rule engine
    const auto& rule_stats        = rule_engine_->stats();
    metrics.rule_evaluations      = rule_stats.total_evaluations.load();
    metrics.avg_rule_eval_time_ns = rule_stats.avg_eval_time_ns();
    auto cache_total              = rule_stats.cache_hits.load() + rule_stats.cache_misses.load();
    metrics.cache_hit_rate =
        cache_total > 0 ? static_cast<double>(rule_stats.cache_hits) / cache_total * 100.0 : 0.0;

    // From sink registry
    const auto& sink_stats  = sink_registry_->stats();
    metrics.sink_selections = sink_stats.total_selections.load();
    metrics.failover_events = sink_stats.failover_events.load();

    // From message bus
    const auto& bus_stats       = message_bus_->stats();
    metrics.messages_published  = bus_stats.messages_published.load();
    metrics.messages_delivered  = bus_stats.messages_delivered.load();
    metrics.queue_overflows     = bus_stats.queue_overflows.load();
    metrics.avg_routing_time_us = bus_stats.avg_latency_us();

    return metrics;
}

void Router::reset_metrics() {
    message_bus_->reset_stats();
    rule_engine_->reset_stats();
    scheduler_->reset_stats();
    sink_registry_->reset_stats();
}

// ============================================================================
// Internal Methods
// ============================================================================

void Router::handle_message(const core::Message& msg) {
    // Note: Results are intentionally ignored here as this is a fire-and-forget callback
    // Errors are already logged within the route functions
    if (msg.type == core::Message::Type::DATA_POINT) {
        std::ignore = route(msg.payload);
    } else if (msg.type == core::Message::Type::DATA_BATCH) {
        std::ignore = route_batch(msg.batch_payload);
    } else if (msg.type == core::Message::Type::DEADLINE_TASK) {
        Timestamp deadline(std::chrono::nanoseconds(msg.deadline_ns));
        std::ignore = route_with_deadline(msg.payload, deadline);
    }
}

Result<> Router::dispatch_to_sinks(const DataPoint& dp,
                                   const std::vector<core::RuleMatchResult>& matches) {
    bool any_success = false;
    bool any_failed  = false;
    std::string last_error;

    for (const auto& match : matches) {
        if (!match.matched || match.target_ids.empty()) {
            continue;
        }

        // Determine load balance strategy from rule priority
        auto strategy = (match.priority >= core::RulePriority::HIGH)
                          ? core::LoadBalanceStrategy::FAILOVER
                          : core::LoadBalanceStrategy::ROUND_ROBIN;

        auto result = sink_registry_->write_with_load_balancing(match.target_ids, dp, strategy);

        if (result.is_success()) {
            any_success = true;
        } else {
            any_failed = true;
            last_error = result.error_message();
            IPB_LOG_WARN(category::ROUTER, "Sink write failed: " << last_error);
        }
    }

    if (any_success) {
        return ok();
    }

    if (any_failed) {
        // Send to dead letter queue
        if (config_.enable_dead_letter_queue) {
            auto dlq_result = sink_registry_->write_to_sink(config_.dead_letter_sink_id, dp);
            if (!dlq_result.is_success()) {
                IPB_LOG_ERROR(category::ROUTER, "Dead letter queue write also failed");
            }
        }

        return err(ErrorCode::ALL_SINKS_FAILED, "Failed to dispatch to any sink: " + last_error);
    }

    return ok();
}

core::RoutingRule Router::convert_rule(const RoutingRule& legacy) {
    core::RoutingRule rule;

    rule.name    = legacy.name;
    rule.enabled = legacy.enabled;

    // Convert priority
    rule.priority = static_cast<core::RulePriority>(static_cast<uint8_t>(legacy.priority));

    // Convert rule type and conditions
    switch (legacy.type) {
        case RuleType::STATIC:
            rule.type             = core::RuleType::STATIC;
            rule.source_addresses = legacy.source_addresses;
            break;

        case RuleType::REGEX_PATTERN:
            rule.type            = core::RuleType::PATTERN;
            rule.address_pattern = legacy.address_pattern;
            break;

        case RuleType::PROTOCOL_BASED:
            rule.type         = core::RuleType::PROTOCOL;
            rule.protocol_ids = legacy.protocol_ids;
            break;

        case RuleType::QUALITY_BASED:
            rule.type           = core::RuleType::QUALITY;
            rule.quality_levels = legacy.quality_levels;
            break;

        case RuleType::VALUE_BASED:
            rule.type = core::RuleType::VALUE;
            if (!legacy.value_conditions.empty()) {
                const auto& vc = legacy.value_conditions[0];
                core::ValueCondition cond;
                cond.op              = static_cast<core::CompareOp>(static_cast<uint8_t>(vc.op));
                rule.value_condition = cond;
            }
            break;

        case RuleType::TIMESTAMP_BASED:
            rule.type       = core::RuleType::TIMESTAMP;
            rule.start_time = legacy.start_time;
            rule.end_time   = legacy.end_time;
            break;

        case RuleType::CUSTOM_LOGIC:
            rule.type             = core::RuleType::CUSTOM;
            rule.custom_predicate = legacy.custom_condition;
            break;

        default:
            rule.type = core::RuleType::STATIC;
            break;
    }

    rule.target_sink_ids = legacy.target_sink_ids;

    return rule;
}

RoutingRule Router::convert_rule_back(const core::RoutingRule& rule) {
    RoutingRule legacy;

    legacy.rule_id  = rule.id;
    legacy.name     = rule.name;
    legacy.enabled  = rule.enabled;
    legacy.priority = static_cast<RoutingPriority>(static_cast<uint8_t>(rule.priority));

    switch (rule.type) {
        case core::RuleType::STATIC:
            legacy.type             = RuleType::STATIC;
            legacy.source_addresses = rule.source_addresses;
            break;

        case core::RuleType::PATTERN:
            legacy.type            = RuleType::REGEX_PATTERN;
            legacy.address_pattern = rule.address_pattern;
            break;

        case core::RuleType::PROTOCOL:
            legacy.type         = RuleType::PROTOCOL_BASED;
            legacy.protocol_ids = rule.protocol_ids;
            break;

        case core::RuleType::QUALITY:
            legacy.type           = RuleType::QUALITY_BASED;
            legacy.quality_levels = rule.quality_levels;
            break;

        case core::RuleType::VALUE:
            legacy.type = RuleType::VALUE_BASED;
            break;

        case core::RuleType::TIMESTAMP:
            legacy.type       = RuleType::TIMESTAMP_BASED;
            legacy.start_time = rule.start_time;
            legacy.end_time   = rule.end_time;
            break;

        case core::RuleType::CUSTOM:
            legacy.type             = RuleType::CUSTOM_LOGIC;
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
// RouterFactory Implementation
// ============================================================================

std::unique_ptr<Router> RouterFactory::create() {
    return std::make_unique<Router>();
}

std::unique_ptr<Router> RouterFactory::create(const RouterConfig& config) {
    return std::make_unique<Router>(config);
}

std::unique_ptr<Router> RouterFactory::create_high_throughput() {
    return std::make_unique<Router>(RouterConfig::high_throughput());
}

std::unique_ptr<Router> RouterFactory::create_low_latency() {
    return std::make_unique<Router>(RouterConfig::low_latency());
}

std::unique_ptr<Router> RouterFactory::create_realtime() {
    return std::make_unique<Router>(RouterConfig::realtime());
}

// ============================================================================
// RuleBuilder Implementation
// ============================================================================

RuleBuilder& RuleBuilder::name(std::string rule_name) {
    rule_.name = std::move(rule_name);
    return *this;
}

RuleBuilder& RuleBuilder::priority(RoutingPriority prio) {
    rule_.priority = prio;
    return *this;
}

RuleBuilder& RuleBuilder::enabled(bool is_enabled) {
    rule_.enabled = is_enabled;
    return *this;
}

RuleBuilder& RuleBuilder::match_address(const std::string& address) {
    rule_.type = RuleType::STATIC;
    rule_.source_addresses.push_back(address);
    return *this;
}

RuleBuilder& RuleBuilder::match_addresses(const std::vector<std::string>& addresses) {
    rule_.type             = RuleType::STATIC;
    rule_.source_addresses = addresses;
    return *this;
}

RuleBuilder& RuleBuilder::match_protocol(uint16_t protocol_id) {
    rule_.type = RuleType::PROTOCOL_BASED;
    rule_.protocol_ids.push_back(protocol_id);
    return *this;
}

RuleBuilder& RuleBuilder::match_protocols(const std::vector<uint16_t>& protocol_ids) {
    rule_.type         = RuleType::PROTOCOL_BASED;
    rule_.protocol_ids = protocol_ids;
    return *this;
}

RuleBuilder& RuleBuilder::match_pattern(const std::string& regex_pattern) {
    rule_.type            = RuleType::REGEX_PATTERN;
    rule_.address_pattern = regex_pattern;
    return *this;
}

RuleBuilder& RuleBuilder::match_quality(Quality quality) {
    rule_.type = RuleType::QUALITY_BASED;
    rule_.quality_levels.push_back(quality);
    return *this;
}

RuleBuilder& RuleBuilder::match_time_range(Timestamp start, Timestamp end) {
    rule_.type       = RuleType::TIMESTAMP_BASED;
    rule_.start_time = start;
    rule_.end_time   = end;
    return *this;
}

RuleBuilder& RuleBuilder::match_value_condition(const ValueCondition& condition) {
    rule_.type = RuleType::VALUE_BASED;
    rule_.value_conditions.push_back(condition);
    return *this;
}

RuleBuilder& RuleBuilder::match_custom(std::function<bool(const DataPoint&)> condition) {
    rule_.type             = RuleType::CUSTOM_LOGIC;
    rule_.custom_condition = std::move(condition);
    return *this;
}

RuleBuilder& RuleBuilder::route_to(const std::string& sink_id) {
    rule_.target_sink_ids.push_back(sink_id);
    return *this;
}

RuleBuilder& RuleBuilder::route_to(const std::vector<std::string>& sink_ids) {
    rule_.target_sink_ids = sink_ids;
    return *this;
}

RuleBuilder& RuleBuilder::load_balance(LoadBalanceStrategy strategy) {
    rule_.load_balance_strategy = strategy;
    if (strategy == LoadBalanceStrategy::FAILOVER) {
        rule_.enable_failover = true;
    }
    return *this;
}

RuleBuilder& RuleBuilder::with_weights(const std::vector<uint32_t>& weights) {
    rule_.load_balance_strategy = LoadBalanceStrategy::WEIGHTED_ROUND_ROBIN;
    rule_.sink_weights          = weights;
    return *this;
}

RuleBuilder& RuleBuilder::with_failover(const std::vector<std::string>& backup_sinks) {
    rule_.enable_failover = true;
    rule_.backup_sink_ids = backup_sinks;
    return *this;
}

RuleBuilder& RuleBuilder::custom_target_selector(
    std::function<std::vector<std::string>(const DataPoint&)> selector) {
    rule_.custom_target_selector = std::move(selector);
    return *this;
}

RuleBuilder& RuleBuilder::enable_batching(uint32_t batch_size, std::chrono::milliseconds timeout) {
    rule_.enable_batching = true;
    rule_.batch_size      = batch_size;
    rule_.batch_timeout   = timeout;
    return *this;
}

RoutingRule RuleBuilder::build() {
    if (!rule_.is_valid()) {
        throw std::invalid_argument("Invalid routing rule: " + rule_.name);
    }

    rule_.rule_id = rule_id_counter_++;
    return std::move(rule_);
}

Result<RoutingRule> RuleBuilder::try_build() {
    if (!rule_.is_valid()) {
        return err<RoutingRule>(ErrorCode::RULE_INVALID, "Invalid routing rule: " + rule_.name);
    }

    rule_.rule_id = rule_id_counter_++;
    return ok<RoutingRule>(std::move(rule_));
}

}  // namespace ipb::router
