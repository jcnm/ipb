#pragma once

/**
 * @file registry.hpp
 * @brief Generic registry abstraction for component management
 *
 * Provides a type-safe, thread-safe registry with:
 * - Load balancing strategies
 * - Health monitoring
 * - Statistics tracking
 * - Automatic failover
 *
 * Use this as a base for SinkRegistry, ScoopRegistry, etc.
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "error.hpp"
#include "platform.hpp"

namespace ipb::common {

// ============================================================================
// LOAD BALANCING
// ============================================================================

/**
 * @brief Load balancing strategies
 */
enum class LoadBalanceStrategy : uint8_t {
    ROUND_ROBIN,           ///< Simple round-robin
    WEIGHTED_ROUND_ROBIN,  ///< Weighted distribution
    LEAST_CONNECTIONS,     ///< Route to item with fewest pending
    LEAST_LATENCY,         ///< Route to item with lowest latency
    HASH_BASED,            ///< Consistent hashing
    RANDOM,                ///< Random selection
    FAILOVER,              ///< Primary with backup(s)
    BROADCAST              ///< Send to all
};

/**
 * @brief Health status
 */
enum class HealthStatus : uint8_t {
    HEALTHY,    ///< Operating normally
    DEGRADED,   ///< Working but with issues
    UNHEALTHY,  ///< Not accepting data
    UNKNOWN     ///< Health status unknown
};

constexpr std::string_view health_status_name(HealthStatus status) noexcept {
    switch (status) {
        case HealthStatus::HEALTHY: return "HEALTHY";
        case HealthStatus::DEGRADED: return "DEGRADED";
        case HealthStatus::UNHEALTHY: return "UNHEALTHY";
        case HealthStatus::UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// REGISTRY ITEM INFO
// ============================================================================

/**
 * @brief Metadata and statistics for a registered item
 */
template<typename T>
struct RegistryItemInfo {
    std::string id;
    std::string type;
    std::shared_ptr<T> item;

    // Configuration
    uint32_t weight = 100;      ///< Weight for load balancing
    bool enabled = true;        ///< Whether item is enabled
    uint32_t priority = 0;      ///< Priority for failover (lower = higher)

    // Health
    HealthStatus health = HealthStatus::UNKNOWN;
    Timestamp last_health_check;
    std::string health_message;

    // Statistics
    std::atomic<uint64_t> operations_success{0};
    std::atomic<uint64_t> operations_failed{0};
    std::atomic<uint64_t> bytes_processed{0};
    std::atomic<int64_t> total_latency_ns{0};
    std::atomic<int64_t> pending_count{0};

    RegistryItemInfo() = default;

    RegistryItemInfo(std::string item_id, std::shared_ptr<T> item_ptr)
        : id(std::move(item_id))
        , item(std::move(item_ptr))
    {}

    // Copy constructor
    RegistryItemInfo(const RegistryItemInfo& other)
        : id(other.id)
        , type(other.type)
        , item(other.item)
        , weight(other.weight)
        , enabled(other.enabled)
        , priority(other.priority)
        , health(other.health)
        , last_health_check(other.last_health_check)
        , health_message(other.health_message)
        , operations_success(other.operations_success.load())
        , operations_failed(other.operations_failed.load())
        , bytes_processed(other.bytes_processed.load())
        , total_latency_ns(other.total_latency_ns.load())
        , pending_count(other.pending_count.load())
    {}

    // Move constructor
    RegistryItemInfo(RegistryItemInfo&& other) noexcept
        : id(std::move(other.id))
        , type(std::move(other.type))
        , item(std::move(other.item))
        , weight(other.weight)
        , enabled(other.enabled)
        , priority(other.priority)
        , health(other.health)
        , last_health_check(other.last_health_check)
        , health_message(std::move(other.health_message))
        , operations_success(other.operations_success.load())
        , operations_failed(other.operations_failed.load())
        , bytes_processed(other.bytes_processed.load())
        , total_latency_ns(other.total_latency_ns.load())
        , pending_count(other.pending_count.load())
    {}

    // Copy assignment
    RegistryItemInfo& operator=(const RegistryItemInfo& other) {
        if (this != &other) {
            id = other.id;
            type = other.type;
            item = other.item;
            weight = other.weight;
            enabled = other.enabled;
            priority = other.priority;
            health = other.health;
            last_health_check = other.last_health_check;
            health_message = other.health_message;
            operations_success.store(other.operations_success.load());
            operations_failed.store(other.operations_failed.load());
            bytes_processed.store(other.bytes_processed.load());
            total_latency_ns.store(other.total_latency_ns.load());
            pending_count.store(other.pending_count.load());
        }
        return *this;
    }

    // Move assignment
    RegistryItemInfo& operator=(RegistryItemInfo&& other) noexcept {
        if (this != &other) {
            id = std::move(other.id);
            type = std::move(other.type);
            item = std::move(other.item);
            weight = other.weight;
            enabled = other.enabled;
            priority = other.priority;
            health = other.health;
            last_health_check = other.last_health_check;
            health_message = std::move(other.health_message);
            operations_success.store(other.operations_success.load());
            operations_failed.store(other.operations_failed.load());
            bytes_processed.store(other.bytes_processed.load());
            total_latency_ns.store(other.total_latency_ns.load());
            pending_count.store(other.pending_count.load());
        }
        return *this;
    }

    /// Calculate success rate
    double success_rate() const noexcept {
        auto total = operations_success.load() + operations_failed.load();
        return total > 0 ? static_cast<double>(operations_success) / total * 100.0 : 100.0;
    }

    /// Calculate average latency in microseconds
    double avg_latency_us() const noexcept {
        auto count = operations_success.load();
        return count > 0 ? static_cast<double>(total_latency_ns) / count / 1000.0 : 0.0;
    }

    /// Record a successful operation
    void record_success(int64_t latency_ns = 0, uint64_t bytes = 0) noexcept {
        operations_success++;
        if (latency_ns > 0) total_latency_ns += latency_ns;
        if (bytes > 0) bytes_processed += bytes;
    }

    /// Record a failed operation
    void record_failure() noexcept {
        operations_failed++;
    }

    /// Reset statistics
    void reset_stats() noexcept {
        operations_success = 0;
        operations_failed = 0;
        bytes_processed = 0;
        total_latency_ns = 0;
        pending_count = 0;
    }
};

// ============================================================================
// SELECTION RESULT
// ============================================================================

/**
 * @brief Result of item selection
 */
struct SelectionResult {
    bool success = false;
    std::vector<std::string> selected_ids;
    std::string error_message;

    explicit operator bool() const noexcept { return success; }

    static SelectionResult ok(std::vector<std::string> ids) {
        return SelectionResult{true, std::move(ids), {}};
    }

    static SelectionResult ok(std::string id) {
        return SelectionResult{true, {std::move(id)}, {}};
    }

    static SelectionResult fail(std::string message) {
        return SelectionResult{false, {}, std::move(message)};
    }
};

// ============================================================================
// REGISTRY STATISTICS
// ============================================================================

/**
 * @brief Statistics for a registry
 */
struct RegistryStats {
    std::atomic<uint64_t> total_selections{0};
    std::atomic<uint64_t> successful_selections{0};
    std::atomic<uint64_t> failed_selections{0};
    std::atomic<uint64_t> failover_events{0};

    std::atomic<uint64_t> active_items{0};
    std::atomic<uint64_t> healthy_items{0};
    std::atomic<uint64_t> degraded_items{0};
    std::atomic<uint64_t> unhealthy_items{0};

    RegistryStats() = default;

    RegistryStats(const RegistryStats& other)
        : total_selections(other.total_selections.load())
        , successful_selections(other.successful_selections.load())
        , failed_selections(other.failed_selections.load())
        , failover_events(other.failover_events.load())
        , active_items(other.active_items.load())
        , healthy_items(other.healthy_items.load())
        , degraded_items(other.degraded_items.load())
        , unhealthy_items(other.unhealthy_items.load())
    {}

    void reset() noexcept {
        total_selections = 0;
        successful_selections = 0;
        failed_selections = 0;
        failover_events = 0;
    }
};

// ============================================================================
// REGISTRY CONFIGURATION
// ============================================================================

/**
 * @brief Configuration for a registry
 */
struct RegistryConfig {
    /// Default load balancing strategy
    LoadBalanceStrategy default_strategy = LoadBalanceStrategy::ROUND_ROBIN;

    /// Enable automatic health checking
    bool enable_health_check = true;

    /// Health check interval
    std::chrono::milliseconds health_check_interval{5000};

    /// Unhealthy threshold (consecutive failures)
    uint32_t unhealthy_threshold = 3;

    /// Enable automatic failover
    bool enable_failover = true;

    /// Failover timeout before trying primary again
    std::chrono::milliseconds failover_timeout{30000};

    /// Maximum items to allow
    size_t max_items = 1000;
};

// ============================================================================
// GENERIC REGISTRY
// ============================================================================

/**
 * @brief Generic thread-safe registry with load balancing and health monitoring
 *
 * @tparam T The type of items to manage (must be pointer-compatible)
 *
 * Example usage:
 * @code
 * Registry<IIPBSink> sink_registry;
 * sink_registry.register_item("kafka", kafka_sink);
 * sink_registry.register_item("mqtt", mqtt_sink, 150);  // Higher weight
 *
 * auto result = sink_registry.select({"kafka", "mqtt"}, LoadBalanceStrategy::WEIGHTED_ROUND_ROBIN);
 * if (result) {
 *     auto sink = sink_registry.get(result.selected_ids[0]);
 *     sink->write(data);
 * }
 * @endcode
 */
template<typename T>
class Registry {
public:
    using ItemInfo = RegistryItemInfo<T>;
    using ItemPtr = std::shared_ptr<T>;
    using HealthChecker = std::function<HealthStatus(const ItemPtr&)>;

    Registry() : config_(RegistryConfig{}) {}
    explicit Registry(RegistryConfig config) : config_(std::move(config)) {}

    ~Registry() {
        stop();
    }

    // Non-copyable, movable
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    Registry(Registry&& other) noexcept
        : config_(std::move(other.config_))
        , items_(std::move(other.items_))
        , stats_(other.stats_)
        , running_(other.running_.load())
    {
        other.running_ = false;
    }

    Registry& operator=(Registry&& other) noexcept {
        if (this != &other) {
            stop();
            config_ = std::move(other.config_);
            items_ = std::move(other.items_);
            stats_ = other.stats_;
            running_ = other.running_.load();
            other.running_ = false;
        }
        return *this;
    }

    // ========================================================================
    // LIFECYCLE
    // ========================================================================

    /// Start health monitoring
    bool start() {
        if (running_) return true;

        if (config_.enable_health_check) {
            running_ = true;
            health_thread_ = std::thread([this]() { health_check_loop(); });
        }
        return true;
    }

    /// Stop health monitoring
    void stop() {
        running_ = false;
        if (health_thread_.joinable()) {
            health_thread_.join();
        }
    }

    /// Check if running
    bool is_running() const noexcept { return running_; }

    // ========================================================================
    // REGISTRATION
    // ========================================================================

    /// Register an item
    bool register_item(std::string_view id, ItemPtr item) {
        return register_item(id, std::move(item), 100);
    }

    /// Register an item with weight
    bool register_item(std::string_view id, ItemPtr item, uint32_t weight) {
        if (!item || id.empty()) return false;

        std::unique_lock lock(mutex_);
        if (items_.size() >= config_.max_items) return false;
        if (items_.count(std::string(id)) > 0) return false;

        ItemInfo info(std::string(id), std::move(item));
        info.weight = weight;
        info.health = HealthStatus::UNKNOWN;
        items_.emplace(std::string(id), std::move(info));
        stats_.active_items++;

        return true;
    }

    /// Unregister an item
    bool unregister_item(std::string_view id) {
        std::unique_lock lock(mutex_);
        auto it = items_.find(std::string(id));
        if (it == items_.end()) return false;

        items_.erase(it);
        stats_.active_items--;
        return true;
    }

    /// Check if item is registered
    bool has(std::string_view id) const {
        std::shared_lock lock(mutex_);
        return items_.count(std::string(id)) > 0;
    }

    /// Get item by ID
    ItemPtr get(std::string_view id) {
        std::shared_lock lock(mutex_);
        auto it = items_.find(std::string(id));
        return it != items_.end() ? it->second.item : nullptr;
    }

    /// Get item info by ID
    std::optional<ItemInfo> get_info(std::string_view id) const {
        std::shared_lock lock(mutex_);
        auto it = items_.find(std::string(id));
        if (it == items_.end()) return std::nullopt;
        return it->second;
    }

    /// Get all registered IDs
    std::vector<std::string> get_ids() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> ids;
        ids.reserve(items_.size());
        for (const auto& [id, _] : items_) {
            ids.push_back(id);
        }
        return ids;
    }

    /// Get item count
    size_t count() const noexcept {
        std::shared_lock lock(mutex_);
        return items_.size();
    }

    // ========================================================================
    // CONFIGURATION
    // ========================================================================

    /// Enable or disable an item
    bool set_enabled(std::string_view id, bool enabled) {
        std::unique_lock lock(mutex_);
        auto it = items_.find(std::string(id));
        if (it == items_.end()) return false;
        it->second.enabled = enabled;
        return true;
    }

    /// Set item weight for load balancing
    bool set_weight(std::string_view id, uint32_t weight) {
        std::unique_lock lock(mutex_);
        auto it = items_.find(std::string(id));
        if (it == items_.end()) return false;
        it->second.weight = weight;
        return true;
    }

    /// Set item priority for failover
    bool set_priority(std::string_view id, uint32_t priority) {
        std::unique_lock lock(mutex_);
        auto it = items_.find(std::string(id));
        if (it == items_.end()) return false;
        it->second.priority = priority;
        return true;
    }

    // ========================================================================
    // SELECTION (LOAD BALANCING)
    // ========================================================================

    /// Select item(s) from candidates using specified strategy
    SelectionResult select(
        const std::vector<std::string>& candidates,
        LoadBalanceStrategy strategy = LoadBalanceStrategy::ROUND_ROBIN)
    {
        return select_filtered(candidates, nullptr, strategy);
    }

    /// Select item(s) with custom filter
    SelectionResult select_filtered(
        const std::vector<std::string>& candidates,
        std::function<bool(const ItemInfo&)> filter,
        LoadBalanceStrategy strategy = LoadBalanceStrategy::ROUND_ROBIN)
    {
        stats_.total_selections++;

        std::shared_lock lock(mutex_);

        // Gather eligible items
        std::vector<const ItemInfo*> eligible;
        for (const auto& id : candidates) {
            auto it = items_.find(id);
            if (it == items_.end()) continue;
            if (!it->second.enabled) continue;
            if (it->second.health == HealthStatus::UNHEALTHY) continue;
            if (filter && !filter(it->second)) continue;
            eligible.push_back(&it->second);
        }

        if (eligible.empty()) {
            stats_.failed_selections++;
            return SelectionResult::fail("No eligible items found");
        }

        std::vector<std::string> selected;

        switch (strategy) {
            case LoadBalanceStrategy::ROUND_ROBIN:
                selected.push_back(select_round_robin(eligible));
                break;

            case LoadBalanceStrategy::WEIGHTED_ROUND_ROBIN:
                selected.push_back(select_weighted_round_robin(eligible));
                break;

            case LoadBalanceStrategy::RANDOM:
                selected.push_back(select_random(eligible));
                break;

            case LoadBalanceStrategy::LEAST_CONNECTIONS:
                selected.push_back(select_least_connections(eligible));
                break;

            case LoadBalanceStrategy::LEAST_LATENCY:
                selected.push_back(select_least_latency(eligible));
                break;

            case LoadBalanceStrategy::FAILOVER:
                selected.push_back(select_failover(eligible));
                break;

            case LoadBalanceStrategy::BROADCAST:
                for (const auto* info : eligible) {
                    selected.push_back(info->id);
                }
                break;

            case LoadBalanceStrategy::HASH_BASED:
            default:
                selected.push_back(eligible[0]->id);
                break;
        }

        stats_.successful_selections++;
        return SelectionResult::ok(std::move(selected));
    }

    // ========================================================================
    // HEALTH MANAGEMENT
    // ========================================================================

    /// Get health status of an item
    HealthStatus get_health(std::string_view id) const {
        std::shared_lock lock(mutex_);
        auto it = items_.find(std::string(id));
        return it != items_.end() ? it->second.health : HealthStatus::UNKNOWN;
    }

    /// Set custom health checker
    void set_health_checker(HealthChecker checker) {
        health_checker_ = std::move(checker);
    }

    /// Get all healthy items
    std::vector<std::string> get_healthy() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> result;
        for (const auto& [id, info] : items_) {
            if (info.health == HealthStatus::HEALTHY) {
                result.push_back(id);
            }
        }
        return result;
    }

    /// Get all unhealthy items
    std::vector<std::string> get_unhealthy() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> result;
        for (const auto& [id, info] : items_) {
            if (info.health == HealthStatus::UNHEALTHY) {
                result.push_back(id);
            }
        }
        return result;
    }

    /// Mark item as unhealthy
    void mark_unhealthy(std::string_view id, std::string_view reason = "") {
        std::unique_lock lock(mutex_);
        auto it = items_.find(std::string(id));
        if (it != items_.end()) {
            it->second.health = HealthStatus::UNHEALTHY;
            it->second.health_message = std::string(reason);
            it->second.last_health_check = Timestamp::now();
            stats_.unhealthy_items++;
            if (it->second.health == HealthStatus::HEALTHY) {
                stats_.healthy_items--;
            }
        }
    }

    /// Mark item as healthy
    void mark_healthy(std::string_view id) {
        std::unique_lock lock(mutex_);
        auto it = items_.find(std::string(id));
        if (it != items_.end()) {
            auto old_health = it->second.health;
            it->second.health = HealthStatus::HEALTHY;
            it->second.health_message.clear();
            it->second.last_health_check = Timestamp::now();
            stats_.healthy_items++;
            if (old_health == HealthStatus::UNHEALTHY) {
                stats_.unhealthy_items--;
            }
        }
    }

    // ========================================================================
    // STATISTICS
    // ========================================================================

    /// Get registry statistics
    const RegistryStats& stats() const noexcept { return stats_; }

    /// Reset statistics
    void reset_stats() { stats_.reset(); }

    /// Get all item statistics
    std::unordered_map<std::string, ItemInfo> get_all_stats() const {
        std::shared_lock lock(mutex_);
        return items_;
    }

    /// Record operation result for an item
    void record_operation(std::string_view id, bool success, int64_t latency_ns = 0, uint64_t bytes = 0) {
        std::shared_lock lock(mutex_);
        auto it = items_.find(std::string(id));
        if (it != items_.end()) {
            if (success) {
                it->second.record_success(latency_ns, bytes);
            } else {
                it->second.record_failure();
            }
        }
    }

    // ========================================================================
    // CONFIGURATION ACCESS
    // ========================================================================

    /// Get current configuration
    const RegistryConfig& config() const noexcept { return config_; }

private:
    std::string select_round_robin(const std::vector<const ItemInfo*>& eligible) {
        size_t idx = round_robin_counter_++ % eligible.size();
        return eligible[idx]->id;
    }

    std::string select_weighted_round_robin(const std::vector<const ItemInfo*>& eligible) {
        uint64_t total_weight = 0;
        for (const auto* info : eligible) {
            total_weight += info->weight;
        }

        if (total_weight == 0) {
            return select_round_robin(eligible);
        }

        uint64_t point = weighted_counter_++ % total_weight;
        uint64_t cumulative = 0;
        for (const auto* info : eligible) {
            cumulative += info->weight;
            if (point < cumulative) {
                return info->id;
            }
        }
        return eligible.back()->id;
    }

    std::string select_random(const std::vector<const ItemInfo*>& eligible) {
        static thread_local std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, eligible.size() - 1);
        return eligible[dist(gen)]->id;
    }

    std::string select_least_connections(const std::vector<const ItemInfo*>& eligible) {
        const ItemInfo* best = eligible[0];
        int64_t min_pending = best->pending_count.load();
        for (const auto* info : eligible) {
            int64_t pending = info->pending_count.load();
            if (pending < min_pending) {
                min_pending = pending;
                best = info;
            }
        }
        return best->id;
    }

    std::string select_least_latency(const std::vector<const ItemInfo*>& eligible) {
        const ItemInfo* best = eligible[0];
        double min_latency = best->avg_latency_us();
        for (const auto* info : eligible) {
            double latency = info->avg_latency_us();
            if (latency < min_latency || min_latency == 0) {
                min_latency = latency;
                best = info;
            }
        }
        return best->id;
    }

    std::string select_failover(const std::vector<const ItemInfo*>& eligible) {
        // Sort by priority, return first healthy
        std::vector<const ItemInfo*> sorted = eligible;
        std::sort(sorted.begin(), sorted.end(),
            [](const ItemInfo* a, const ItemInfo* b) {
                return a->priority < b->priority;
            });

        for (const auto* info : sorted) {
            if (info->health != HealthStatus::UNHEALTHY) {
                return info->id;
            }
        }
        // All unhealthy, return first anyway
        return sorted[0]->id;
    }

    void health_check_loop() {
        while (running_) {
            if (health_checker_) {
                std::shared_lock lock(mutex_);
                for (auto& [id, info] : items_) {
                    auto status = health_checker_(info.item);
                    info.health = status;
                    info.last_health_check = Timestamp::now();
                }
            }

            // Update stats
            update_health_stats();

            // Sleep until next check
            std::this_thread::sleep_for(config_.health_check_interval);
        }
    }

    void update_health_stats() {
        std::shared_lock lock(mutex_);
        uint64_t healthy = 0, degraded = 0, unhealthy = 0;
        for (const auto& [_, info] : items_) {
            switch (info.health) {
                case HealthStatus::HEALTHY: healthy++; break;
                case HealthStatus::DEGRADED: degraded++; break;
                case HealthStatus::UNHEALTHY: unhealthy++; break;
                default: break;
            }
        }
        stats_.healthy_items = healthy;
        stats_.degraded_items = degraded;
        stats_.unhealthy_items = unhealthy;
    }

    RegistryConfig config_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ItemInfo> items_;
    RegistryStats stats_;

    std::atomic<bool> running_{false};
    std::thread health_thread_;
    HealthChecker health_checker_;

    std::atomic<uint64_t> round_robin_counter_{0};
    std::atomic<uint64_t> weighted_counter_{0};
};

}  // namespace ipb::common
