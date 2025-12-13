#pragma once

/**
 * @file scoop_registry.hpp
 * @brief Centralized scoop (data source) management with load balancing
 *
 * The ScoopRegistry provides:
 * - Centralized registration and lookup of data sources
 * - Multiple read strategies (round-robin, failover, broadcast)
 * - Health monitoring and automatic failover
 * - Thread-safe scoop management
 * - Subscription aggregation across multiple sources
 *
 * Target: Zero-allocation scoop selection, <100ns lookup time
 */

#include <ipb/common/data_point.hpp>
#include <ipb/common/dataset.hpp>
#include <ipb/common/interfaces.hpp>

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
class ScoopRegistryImpl;

/**
 * @brief Read strategies for multi-source data access
 */
enum class ReadStrategy : uint8_t {
    PRIMARY_ONLY,      ///< Read from primary source only
    FAILOVER,          ///< Read from primary, failover to backup
    ROUND_ROBIN,       ///< Distribute reads across sources
    BROADCAST_MERGE,   ///< Read from all and merge results
    FASTEST_RESPONSE,  ///< Read from source with lowest latency
    QUORUM             ///< Read from N sources, return majority
};

/**
 * @brief Scoop health status
 */
enum class ScoopHealth : uint8_t {
    HEALTHY,       ///< Scoop is operating normally
    DEGRADED,      ///< Scoop is working but with issues
    UNHEALTHY,     ///< Scoop is not providing data
    DISCONNECTED,  ///< Scoop is disconnected
    UNKNOWN        ///< Health status unknown
};

/**
 * @brief Metadata for a registered scoop
 */
struct ScoopInfo {
    std::string id;
    std::string type;
    std::shared_ptr<common::IProtocolSource> scoop;

    // Configuration
    uint32_t priority = 0;      ///< Priority for failover (lower = higher priority)
    bool enabled      = true;   ///< Whether scoop is enabled
    bool is_primary   = false;  ///< Whether this is a primary source

    // Health
    ScoopHealth health = ScoopHealth::UNKNOWN;
    common::Timestamp last_health_check;
    std::string health_message;

    // Connection state
    bool connected = false;
    common::Timestamp last_connect_time;
    common::Timestamp last_disconnect_time;

    // Statistics
    std::atomic<uint64_t> reads_attempted{0};
    std::atomic<uint64_t> reads_successful{0};
    std::atomic<uint64_t> reads_failed{0};
    std::atomic<uint64_t> data_points_received{0};
    std::atomic<uint64_t> bytes_received{0};
    std::atomic<int64_t> total_latency_ns{0};

    // Default constructor
    ScoopInfo() = default;

    // Copy constructor (atomics need explicit copy)
    ScoopInfo(const ScoopInfo& other)
        : id(other.id), type(other.type), scoop(other.scoop), priority(other.priority),
          enabled(other.enabled), is_primary(other.is_primary), health(other.health),
          last_health_check(other.last_health_check), health_message(other.health_message),
          connected(other.connected), last_connect_time(other.last_connect_time),
          last_disconnect_time(other.last_disconnect_time),
          reads_attempted(other.reads_attempted.load()),
          reads_successful(other.reads_successful.load()), reads_failed(other.reads_failed.load()),
          data_points_received(other.data_points_received.load()),
          bytes_received(other.bytes_received.load()),
          total_latency_ns(other.total_latency_ns.load()) {}

    // Move constructor
    ScoopInfo(ScoopInfo&& other) noexcept
        : id(std::move(other.id)), type(std::move(other.type)), scoop(std::move(other.scoop)),
          priority(other.priority), enabled(other.enabled), is_primary(other.is_primary),
          health(other.health), last_health_check(other.last_health_check),
          health_message(std::move(other.health_message)), connected(other.connected),
          last_connect_time(other.last_connect_time),
          last_disconnect_time(other.last_disconnect_time),
          reads_attempted(other.reads_attempted.load()),
          reads_successful(other.reads_successful.load()), reads_failed(other.reads_failed.load()),
          data_points_received(other.data_points_received.load()),
          bytes_received(other.bytes_received.load()),
          total_latency_ns(other.total_latency_ns.load()) {}

    // Copy assignment
    ScoopInfo& operator=(const ScoopInfo& other) {
        if (this != &other) {
            id                   = other.id;
            type                 = other.type;
            scoop                = other.scoop;
            priority             = other.priority;
            enabled              = other.enabled;
            is_primary           = other.is_primary;
            health               = other.health;
            last_health_check    = other.last_health_check;
            health_message       = other.health_message;
            connected            = other.connected;
            last_connect_time    = other.last_connect_time;
            last_disconnect_time = other.last_disconnect_time;
            reads_attempted.store(other.reads_attempted.load());
            reads_successful.store(other.reads_successful.load());
            reads_failed.store(other.reads_failed.load());
            data_points_received.store(other.data_points_received.load());
            bytes_received.store(other.bytes_received.load());
            total_latency_ns.store(other.total_latency_ns.load());
        }
        return *this;
    }

    // Move assignment
    ScoopInfo& operator=(ScoopInfo&& other) noexcept {
        if (this != &other) {
            id                   = std::move(other.id);
            type                 = std::move(other.type);
            scoop                = std::move(other.scoop);
            priority             = other.priority;
            enabled              = other.enabled;
            is_primary           = other.is_primary;
            health               = other.health;
            last_health_check    = other.last_health_check;
            health_message       = std::move(other.health_message);
            connected            = other.connected;
            last_connect_time    = other.last_connect_time;
            last_disconnect_time = other.last_disconnect_time;
            reads_attempted.store(other.reads_attempted.load());
            reads_successful.store(other.reads_successful.load());
            reads_failed.store(other.reads_failed.load());
            data_points_received.store(other.data_points_received.load());
            bytes_received.store(other.bytes_received.load());
            total_latency_ns.store(other.total_latency_ns.load());
        }
        return *this;
    }

    /// Calculate success rate
    double success_rate() const noexcept {
        auto total = reads_successful.load() + reads_failed.load();
        return total > 0 ? static_cast<double>(reads_successful) / total * 100.0 : 100.0;
    }

    /// Calculate average latency in microseconds
    double avg_latency_us() const noexcept {
        auto count = reads_successful.load();
        return count > 0 ? static_cast<double>(total_latency_ns) / count / 1000.0 : 0.0;
    }
};

/**
 * @brief Result of scoop selection
 */
struct ScoopSelectionResult {
    bool success = false;
    std::vector<std::string> selected_scoop_ids;
    std::string error_message;

    explicit operator bool() const noexcept { return success; }
};

/**
 * @brief Statistics for scoop registry
 */
struct ScoopRegistryStats {
    std::atomic<uint64_t> total_reads{0};
    std::atomic<uint64_t> successful_reads{0};
    std::atomic<uint64_t> failed_reads{0};
    std::atomic<uint64_t> failover_events{0};

    std::atomic<uint64_t> active_scoops{0};
    std::atomic<uint64_t> healthy_scoops{0};
    std::atomic<uint64_t> connected_scoops{0};
    std::atomic<uint64_t> unhealthy_scoops{0};

    std::atomic<uint64_t> active_subscriptions{0};

    ScoopRegistryStats() = default;

    ScoopRegistryStats(const ScoopRegistryStats& other)
        : total_reads(other.total_reads.load()), successful_reads(other.successful_reads.load()),
          failed_reads(other.failed_reads.load()), failover_events(other.failover_events.load()),
          active_scoops(other.active_scoops.load()), healthy_scoops(other.healthy_scoops.load()),
          connected_scoops(other.connected_scoops.load()),
          unhealthy_scoops(other.unhealthy_scoops.load()),
          active_subscriptions(other.active_subscriptions.load()) {}

    ScoopRegistryStats& operator=(const ScoopRegistryStats& other) {
        if (this != &other) {
            total_reads.store(other.total_reads.load());
            successful_reads.store(other.successful_reads.load());
            failed_reads.store(other.failed_reads.load());
            failover_events.store(other.failover_events.load());
            active_scoops.store(other.active_scoops.load());
            healthy_scoops.store(other.healthy_scoops.load());
            connected_scoops.store(other.connected_scoops.load());
            unhealthy_scoops.store(other.unhealthy_scoops.load());
            active_subscriptions.store(other.active_subscriptions.load());
        }
        return *this;
    }

    void reset() noexcept {
        total_reads.store(0);
        successful_reads.store(0);
        failed_reads.store(0);
        failover_events.store(0);
    }
};

/**
 * @brief Configuration for ScoopRegistry
 */
struct ScoopRegistryConfig {
    /// Default read strategy
    ReadStrategy default_strategy = ReadStrategy::FAILOVER;

    /// Enable automatic health checking
    bool enable_health_check = true;

    /// Health check interval
    std::chrono::milliseconds health_check_interval{5000};

    /// Unhealthy threshold (consecutive failures)
    uint32_t unhealthy_threshold = 3;

    /// Enable automatic reconnection
    bool enable_auto_reconnect = true;

    /// Reconnection interval
    std::chrono::milliseconds reconnect_interval{10000};

    /// Enable automatic failover
    bool enable_failover = true;

    /// Quorum size for QUORUM strategy
    uint32_t quorum_size = 2;

    /// Timeout for read operations
    std::chrono::milliseconds read_timeout{5000};
};

/**
 * @brief Aggregated subscription for multiple scoops
 */
class AggregatedSubscription {
public:
    using DataCallback = std::function<void(const common::DataSet&, std::string_view source_id)>;
    using ErrorCallback =
        std::function<void(std::string_view source_id, common::ErrorCode, std::string_view)>;

    AggregatedSubscription() = default;
    ~AggregatedSubscription();

    // Non-copyable, movable
    AggregatedSubscription(const AggregatedSubscription&)            = delete;
    AggregatedSubscription& operator=(const AggregatedSubscription&) = delete;
    AggregatedSubscription(AggregatedSubscription&&) noexcept;
    AggregatedSubscription& operator=(AggregatedSubscription&&) noexcept;

    /// Check if subscription is active
    bool is_active() const noexcept;

    /// Cancel the subscription
    void cancel();

    /// Get number of active source subscriptions
    size_t source_count() const noexcept;

private:
    friend class ScoopRegistryImpl;

    struct SourceSubscription {
        std::string scoop_id;
        bool active = true;
    };

    std::vector<SourceSubscription> sources_;
    std::weak_ptr<ScoopRegistryImpl> registry_;
    uint64_t id_ = 0;
};

/**
 * @brief Centralized scoop registry with read strategies
 *
 * Features:
 * - Thread-safe scoop registration
 * - Multiple read strategies
 * - Health monitoring
 * - Automatic failover
 * - Subscription aggregation
 *
 * Example usage:
 * @code
 * ScoopRegistry registry;
 *
 * // Register scoops
 * registry.register_scoop("modbus_1", modbus_scoop_1, true);  // Primary
 * registry.register_scoop("modbus_2", modbus_scoop_2, false); // Backup
 *
 * // Read with failover
 * auto result = registry.read_from({"modbus_1", "modbus_2"},
 *                                  ReadStrategy::FAILOVER);
 *
 * // Subscribe to aggregated data
 * auto sub = registry.subscribe({"modbus_1", "modbus_2"},
 *     [](const DataSet& data, std::string_view source) {
 *         // Handle data from any source
 *     });
 * @endcode
 */
class ScoopRegistry {
public:
    ScoopRegistry();
    explicit ScoopRegistry(const ScoopRegistryConfig& config);
    ~ScoopRegistry();

    // Non-copyable, movable
    ScoopRegistry(const ScoopRegistry&)            = delete;
    ScoopRegistry& operator=(const ScoopRegistry&) = delete;
    ScoopRegistry(ScoopRegistry&&) noexcept;
    ScoopRegistry& operator=(ScoopRegistry&&) noexcept;

    // Lifecycle

    /// Start health monitoring and auto-reconnect
    bool start();

    /// Stop health monitoring
    void stop();

    /// Check if running
    bool is_running() const noexcept;

    // Scoop Registration

    /// Register a scoop
    bool register_scoop(std::string_view id, std::shared_ptr<common::IProtocolSource> scoop);

    /// Register a scoop with primary flag
    bool register_scoop(std::string_view id, std::shared_ptr<common::IProtocolSource> scoop,
                        bool is_primary);

    /// Register a scoop with priority
    bool register_scoop(std::string_view id, std::shared_ptr<common::IProtocolSource> scoop,
                        bool is_primary, uint32_t priority);

    /// Unregister a scoop
    bool unregister_scoop(std::string_view id);

    /// Check if scoop is registered
    bool has_scoop(std::string_view id) const;

    /// Get scoop by ID
    std::shared_ptr<common::IProtocolSource> get_scoop(std::string_view id);

    /// Get scoop info by ID
    std::optional<ScoopInfo> get_scoop_info(std::string_view id) const;

    /// Get all registered scoop IDs
    std::vector<std::string> get_scoop_ids() const;

    /// Get scoop count
    size_t scoop_count() const noexcept;

    // Scoop Configuration

    /// Enable or disable a scoop
    bool set_scoop_enabled(std::string_view id, bool enabled);

    /// Set scoop as primary
    bool set_scoop_primary(std::string_view id, bool is_primary);

    /// Set scoop priority for failover
    bool set_scoop_priority(std::string_view id, uint32_t priority);

    // Reading Data

    /// Select scoop(s) from a set using specified strategy
    ScoopSelectionResult select_scoop(const std::vector<std::string>& candidate_ids,
                                      ReadStrategy strategy = ReadStrategy::FAILOVER);

    /// Read from selected scoop(s)
    common::Result<common::DataSet> read_from(const std::vector<std::string>& candidate_ids,
                                              ReadStrategy strategy = ReadStrategy::FAILOVER);

    /// Read from a specific scoop
    common::Result<common::DataSet> read_from_scoop(std::string_view scoop_id);

    /// Read and merge from multiple scoops
    common::Result<common::DataSet> read_merged(const std::vector<std::string>& scoop_ids);

    // Subscriptions

    /// Subscribe to data from multiple scoops
    [[nodiscard]] AggregatedSubscription subscribe(
        const std::vector<std::string>& scoop_ids,
        AggregatedSubscription::DataCallback data_callback,
        AggregatedSubscription::ErrorCallback error_callback = nullptr);

    /// Subscribe to all registered scoops
    [[nodiscard]] AggregatedSubscription subscribe_all(
        AggregatedSubscription::DataCallback data_callback,
        AggregatedSubscription::ErrorCallback error_callback = nullptr);

    // Connection Management

    /// Connect a scoop
    common::Result<> connect_scoop(std::string_view id);

    /// Disconnect a scoop
    common::Result<> disconnect_scoop(std::string_view id);

    /// Connect all scoops
    void connect_all();

    /// Disconnect all scoops
    void disconnect_all();

    /// Get connected scoop IDs
    std::vector<std::string> get_connected_scoops() const;

    // Health Management

    /// Get health status of a scoop
    ScoopHealth get_scoop_health(std::string_view id) const;

    /// Force health check for a scoop
    ScoopHealth check_scoop_health(std::string_view id);

    /// Get all healthy scoops
    std::vector<std::string> get_healthy_scoops() const;

    /// Get all unhealthy scoops
    std::vector<std::string> get_unhealthy_scoops() const;

    /// Mark scoop as unhealthy (manual override)
    void mark_scoop_unhealthy(std::string_view id, std::string_view reason);

    /// Mark scoop as healthy (manual override)
    void mark_scoop_healthy(std::string_view id);

    // Address Space

    /// Add address to multiple scoops
    common::Result<> add_address(const std::vector<std::string>& scoop_ids,
                                 std::string_view address);

    /// Remove address from multiple scoops
    common::Result<> remove_address(const std::vector<std::string>& scoop_ids,
                                    std::string_view address);

    /// Get addresses from a scoop
    std::vector<std::string> get_addresses(std::string_view scoop_id) const;

    // Statistics

    /// Get registry statistics
    const ScoopRegistryStats& stats() const noexcept;

    /// Reset statistics
    void reset_stats();

    /// Get per-scoop statistics
    std::unordered_map<std::string, ScoopInfo> get_all_scoop_stats() const;

    // Configuration

    /// Get current configuration
    const ScoopRegistryConfig& config() const noexcept;

private:
    std::unique_ptr<ScoopRegistryImpl> impl_;
};

}  // namespace ipb::core
