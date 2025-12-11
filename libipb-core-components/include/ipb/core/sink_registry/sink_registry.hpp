#pragma once

/**
 * @file sink_registry.hpp
 * @brief Centralized sink management with load balancing
 *
 * The SinkRegistry provides:
 * - Centralized registration and lookup of sinks
 * - Multiple load balancing strategies
 * - Health monitoring and failover
 * - Thread-safe sink management
 *
 * Target: Zero-allocation sink selection, <100ns lookup time
 */

#include <ipb/common/interfaces.hpp>
#include <ipb/common/data_point.hpp>
#include <ipb/common/error.hpp>
#include <ipb/common/debug.hpp>
#include <ipb/common/platform.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ipb::core {

// Forward declarations
class SinkRegistryImpl;
class LoadBalancer;

/**
 * @brief Load balancing strategies
 */
enum class LoadBalanceStrategy : uint8_t {
    ROUND_ROBIN,           ///< Simple round-robin
    WEIGHTED_ROUND_ROBIN,  ///< Weighted distribution
    LEAST_CONNECTIONS,     ///< Route to sink with fewest pending
    LEAST_LATENCY,         ///< Route to sink with lowest latency
    HASH_BASED,            ///< Consistent hashing on address
    RANDOM,                ///< Random selection
    FAILOVER,              ///< Primary with backup(s)
    BROADCAST              ///< Send to all sinks
};

/**
 * @brief Sink health status
 */
enum class SinkHealth : uint8_t {
    HEALTHY,      ///< Sink is operating normally
    DEGRADED,     ///< Sink is working but with issues
    UNHEALTHY,    ///< Sink is not accepting data
    UNKNOWN       ///< Health status unknown
};

/**
 * @brief Metadata for a registered sink
 */
struct SinkInfo {
    std::string id;
    std::string type;
    std::shared_ptr<common::IIPBSink> sink;

    // Configuration
    uint32_t weight = 100;      ///< Weight for load balancing (higher = more traffic)
    bool enabled = true;        ///< Whether sink is enabled
    uint32_t priority = 0;      ///< Priority for failover (lower = higher priority)

    // Health
    SinkHealth health = SinkHealth::UNKNOWN;
    common::Timestamp last_health_check;
    std::string health_message;

    // Statistics
    std::atomic<uint64_t> messages_sent{0};
    std::atomic<uint64_t> messages_failed{0};
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<int64_t> total_latency_ns{0};
    std::atomic<int64_t> pending_count{0};

    /// Calculate success rate
    double success_rate() const noexcept {
        auto total = messages_sent.load() + messages_failed.load();
        return total > 0 ?
            static_cast<double>(messages_sent) / total * 100.0 : 100.0;
    }

    /// Calculate average latency in microseconds
    double avg_latency_us() const noexcept {
        auto count = messages_sent.load();
        return count > 0 ?
            static_cast<double>(total_latency_ns) / count / 1000.0 : 0.0;
    }
};

/**
 * @brief Result of sink selection
 */
struct SinkSelectionResult {
    bool success = false;
    std::vector<std::string> selected_sink_ids;
    std::string error_message;

    explicit operator bool() const noexcept { return success; }
};

/**
 * @brief Statistics for sink registry
 */
struct SinkRegistryStats {
    std::atomic<uint64_t> total_selections{0};
    std::atomic<uint64_t> successful_selections{0};
    std::atomic<uint64_t> failed_selections{0};
    std::atomic<uint64_t> failover_events{0};

    std::atomic<uint64_t> active_sinks{0};
    std::atomic<uint64_t> healthy_sinks{0};
    std::atomic<uint64_t> degraded_sinks{0};
    std::atomic<uint64_t> unhealthy_sinks{0};

    void reset() noexcept {
        total_selections.store(0);
        successful_selections.store(0);
        failed_selections.store(0);
        failover_events.store(0);
    }
};

/**
 * @brief Configuration for SinkRegistry
 */
struct SinkRegistryConfig {
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
};

/**
 * @brief Centralized sink registry with load balancing
 *
 * Features:
 * - Thread-safe sink registration
 * - Multiple load balancing strategies
 * - Health monitoring
 * - Automatic failover
 *
 * Example usage:
 * @code
 * SinkRegistry registry;
 *
 * // Register sinks
 * registry.register_sink("kafka_1", kafka_sink_1);
 * registry.register_sink("kafka_2", kafka_sink_2, 150);  // Higher weight
 *
 * // Select sink with load balancing
 * auto result = registry.select_sink({"kafka_1", "kafka_2"},
 *                                    LoadBalanceStrategy::WEIGHTED_ROUND_ROBIN);
 *
 * // Route data through selected sink
 * if (result) {
 *     registry.write_to_sink(result.selected_sink_ids[0], data_point);
 * }
 * @endcode
 */
class SinkRegistry {
public:
    SinkRegistry();
    explicit SinkRegistry(const SinkRegistryConfig& config);
    ~SinkRegistry();

    // Non-copyable, movable
    SinkRegistry(const SinkRegistry&) = delete;
    SinkRegistry& operator=(const SinkRegistry&) = delete;
    SinkRegistry(SinkRegistry&&) noexcept;
    SinkRegistry& operator=(SinkRegistry&&) noexcept;

    // Lifecycle

    /// Start health monitoring
    bool start();

    /// Stop health monitoring
    void stop();

    /// Check if running
    bool is_running() const noexcept;

    // Sink Registration

    /// Register a sink
    bool register_sink(std::string_view id, std::shared_ptr<common::IIPBSink> sink);

    /// Register a sink with weight
    bool register_sink(std::string_view id, std::shared_ptr<common::IIPBSink> sink,
                      uint32_t weight);

    /// Unregister a sink
    bool unregister_sink(std::string_view id);

    /// Check if sink is registered
    bool has_sink(std::string_view id) const;

    /// Get sink by ID
    std::shared_ptr<common::IIPBSink> get_sink(std::string_view id);

    /// Get sink info by ID
    std::optional<SinkInfo> get_sink_info(std::string_view id) const;

    /// Get all registered sink IDs
    std::vector<std::string> get_sink_ids() const;

    /// Get sink count
    size_t sink_count() const noexcept;

    // Sink Configuration

    /// Enable or disable a sink
    bool set_sink_enabled(std::string_view id, bool enabled);

    /// Set sink weight for load balancing
    bool set_sink_weight(std::string_view id, uint32_t weight);

    /// Set sink priority for failover
    bool set_sink_priority(std::string_view id, uint32_t priority);

    // Load Balancing

    /// Select sink(s) from a set using specified strategy
    SinkSelectionResult select_sink(
        const std::vector<std::string>& candidate_ids,
        LoadBalanceStrategy strategy = LoadBalanceStrategy::ROUND_ROBIN);

    /// Select sink using data point for hash-based strategies
    SinkSelectionResult select_sink(
        const std::vector<std::string>& candidate_ids,
        const common::DataPoint& data_point,
        LoadBalanceStrategy strategy = LoadBalanceStrategy::ROUND_ROBIN);

    /// Select sink with filter predicate
    SinkSelectionResult select_sink_filtered(
        const std::vector<std::string>& candidate_ids,
        std::function<bool(const SinkInfo&)> filter,
        LoadBalanceStrategy strategy = LoadBalanceStrategy::ROUND_ROBIN);

    // Data Routing

    /// Write data point to a specific sink
    common::Result<> write_to_sink(std::string_view sink_id,
                                   const common::DataPoint& data_point);

    /// Write batch to a specific sink
    common::Result<> write_batch_to_sink(std::string_view sink_id,
                                         std::span<const common::DataPoint> batch);

    /// Write to selected sink(s) with load balancing
    common::Result<> write_with_load_balancing(
        const std::vector<std::string>& candidate_ids,
        const common::DataPoint& data_point,
        LoadBalanceStrategy strategy = LoadBalanceStrategy::ROUND_ROBIN);

    /// Write to all sinks (broadcast)
    std::vector<std::pair<std::string, common::Result<>>> write_to_all(
        const std::vector<std::string>& sink_ids,
        const common::DataPoint& data_point);

    // Health Management

    /// Get health status of a sink
    SinkHealth get_sink_health(std::string_view id) const;

    /// Force health check for a sink
    SinkHealth check_sink_health(std::string_view id);

    /// Get all healthy sinks
    std::vector<std::string> get_healthy_sinks() const;

    /// Get all unhealthy sinks
    std::vector<std::string> get_unhealthy_sinks() const;

    /// Mark sink as unhealthy (manual override)
    void mark_sink_unhealthy(std::string_view id, std::string_view reason);

    /// Mark sink as healthy (manual override)
    void mark_sink_healthy(std::string_view id);

    // Statistics

    /// Get registry statistics
    const SinkRegistryStats& stats() const noexcept;

    /// Reset statistics
    void reset_stats();

    /// Get per-sink statistics
    std::unordered_map<std::string, SinkInfo> get_all_sink_stats() const;

    // Configuration

    /// Get current configuration
    const SinkRegistryConfig& config() const noexcept;

private:
    std::unique_ptr<SinkRegistryImpl> impl_;
};

} // namespace ipb::core
