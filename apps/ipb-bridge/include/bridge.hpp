#pragma once

/**
 * @file bridge.hpp
 * @brief IPB Bridge - Lightweight edge/embedded protocol bridge
 *
 * Designed for resource-constrained environments:
 * - Minimal memory footprint
 * - No dynamic allocation in hot path
 * - Deterministic timing
 * - Hardware watchdog support
 */

#include <ipb/common/data_point.hpp>
#include <ipb/common/error.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ipb::bridge {

// Forward declarations
struct BridgeConfig;
class DataSource;
class DataSink;

/**
 * @brief Bridge operational state
 */
enum class BridgeState : uint8_t { STOPPED = 0, INITIALIZING, RUNNING, PAUSED, ERROR, SHUTDOWN };

/**
 * @brief Bridge statistics (lock-free)
 */
struct BridgeStats {
    std::atomic<uint64_t> messages_received{0};
    std::atomic<uint64_t> messages_forwarded{0};
    std::atomic<uint64_t> messages_dropped{0};
    std::atomic<uint64_t> errors{0};
    std::atomic<uint64_t> uptime_seconds{0};
    std::atomic<uint32_t> active_sources{0};
    std::atomic<uint32_t> active_sinks{0};

    // Copy constructor for atomic members
    BridgeStats() = default;
    BridgeStats(const BridgeStats& other)
        : messages_received(other.messages_received.load()),
          messages_forwarded(other.messages_forwarded.load()),
          messages_dropped(other.messages_dropped.load()), errors(other.errors.load()),
          uptime_seconds(other.uptime_seconds.load()), active_sources(other.active_sources.load()),
          active_sinks(other.active_sinks.load()) {}
};

/**
 * @brief Data source interface (Scoop abstraction)
 */
class DataSource {
public:
    virtual ~DataSource() = default;

    virtual std::string id() const       = 0;
    virtual common::Result<void> start() = 0;
    virtual void stop()                  = 0;
    virtual bool is_running() const      = 0;

    using DataCallback = std::function<void(const common::DataPoint&)>;
    virtual void set_callback(DataCallback callback) = 0;
};

/**
 * @brief Data sink interface (Sink abstraction)
 */
class DataSink {
public:
    virtual ~DataSink() = default;

    virtual std::string id() const       = 0;
    virtual common::Result<void> start() = 0;
    virtual void stop()                  = 0;
    virtual bool is_running() const      = 0;

    virtual common::Result<void> send(const common::DataPoint& data) = 0;
    virtual common::Result<void> flush()                             = 0;
};

/**
 * @brief IPB Bridge - Main application class
 *
 * Lightweight protocol bridge for edge/embedded deployments.
 * Optimized for:
 * - Low memory usage
 * - Deterministic latency
 * - Simple configuration
 * - Robust error recovery
 */
class Bridge {
public:
    Bridge();
    ~Bridge();

    // Non-copyable, non-movable
    Bridge(const Bridge&)            = delete;
    Bridge& operator=(const Bridge&) = delete;

    /**
     * @brief Initialize bridge with configuration
     * @param config Bridge configuration
     * @return Success or error
     */
    common::Result<void> initialize(const BridgeConfig& config);

    /**
     * @brief Initialize from configuration file
     * @param config_path Path to YAML/JSON config file
     * @return Success or error
     */
    common::Result<void> initialize_from_file(const std::string& config_path);

    /**
     * @brief Start the bridge
     * @return Success or error
     */
    common::Result<void> start();

    /**
     * @brief Stop the bridge
     */
    void stop();

    /**
     * @brief Pause data forwarding (sources continue to run)
     */
    void pause();

    /**
     * @brief Resume data forwarding
     */
    void resume();

    /**
     * @brief Run until stopped (blocking)
     */
    void run();

    /**
     * @brief Process one iteration (non-blocking)
     *
     * For use in cooperative multitasking environments.
     * @return true if work was done, false if idle
     */
    bool tick();

    /**
     * @brief Add a data source
     * @param source Source to add
     * @return Success or error
     */
    common::Result<void> add_source(std::unique_ptr<DataSource> source);

    /**
     * @brief Add a data sink
     * @param sink Sink to add
     * @return Success or error
     */
    common::Result<void> add_sink(std::unique_ptr<DataSink> sink);

    /**
     * @brief Remove a source by ID
     * @param id Source ID
     * @return Success or error
     */
    common::Result<void> remove_source(const std::string& id);

    /**
     * @brief Remove a sink by ID
     * @param id Sink ID
     * @return Success or error
     */
    common::Result<void> remove_sink(const std::string& id);

    /**
     * @brief Get current state
     */
    BridgeState state() const { return state_.load(); }

    /**
     * @brief Get statistics
     */
    const BridgeStats& stats() const { return stats_; }

    /**
     * @brief Get last error message
     */
    const std::string& last_error() const { return last_error_; }

    /**
     * @brief Feed the watchdog (call periodically in constrained environments)
     */
    void feed_watchdog();

    /**
     * @brief Check health status
     * @return true if all components healthy
     */
    bool is_healthy() const;

private:
    // Internal methods
    void data_received(const common::DataPoint& data);
    void forward_data(const common::DataPoint& data);
    void update_stats();
    void handle_error(const std::string& message);

    // State
    std::atomic<BridgeState> state_{BridgeState::STOPPED};
    std::atomic<bool> paused_{false};
    std::string last_error_;

    // Components
    std::vector<std::unique_ptr<DataSource>> sources_;
    std::vector<std::unique_ptr<DataSink>> sinks_;

    // Statistics
    BridgeStats stats_;

    // Timing
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_watchdog_feed_;

    // Configuration
    std::chrono::milliseconds watchdog_timeout_{30000};
    bool round_robin_sinks_{true};
    size_t current_sink_index_{0};
};

/**
 * @brief Bridge configuration
 */
struct BridgeConfig {
    std::string instance_id;
    std::string name{"IPB Bridge"};

    // Watchdog
    bool enable_watchdog{true};
    std::chrono::milliseconds watchdog_timeout{30000};

    // Forwarding behavior
    bool round_robin_sinks{false};   // false = send to all sinks
    bool drop_on_sink_error{false};  // true = drop data if sink fails

    // Resource limits
    uint32_t max_sources{16};
    uint32_t max_sinks{8};
    uint32_t max_queue_size{1000};

    // Logging
    std::string log_level{"info"};
};

}  // namespace ipb::bridge
