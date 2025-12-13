#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "ipb/common/data_point.hpp"
#include "ipb/common/dataset.hpp"
#include "ipb/common/interfaces.hpp"
#include "ipb/core/config/config_loader.hpp"
#include "ipb/core/config/config_types.hpp"
#include "ipb/core/rule_engine/rule_engine.hpp"
#include "ipb/router/router.hpp"

// Forward declarations for dynamic loading
namespace ipb::scoop::modbus {
class ModbusScoop;
}  // namespace ipb::scoop::modbus
namespace ipb::scoop::opcua {
class OPCUAScoop;
}  // namespace ipb::scoop::opcua
namespace ipb::scoop::mqtt {
class MQTTScoop;
}  // namespace ipb::scoop::mqtt
namespace ipb::sink::kafka {
class KafkaSink;
}  // namespace ipb::sink::kafka
namespace ipb::sink::zmq {
class ZMQSink;
}  // namespace ipb::sink::zmq
namespace ipb::sink::console {
class ConsoleSink;
}  // namespace ipb::sink::console
namespace ipb::sink::syslog {
class SyslogSink;
}  // namespace ipb::sink::syslog

namespace ipb::gate {

// Use core configuration types
using GatewayConfig = core::config::ApplicationConfig;

/**
 * @brief Gateway statistics and metrics
 */
struct GatewayMetrics {
    std::atomic<uint64_t> messages_processed{0};
    std::atomic<uint64_t> messages_routed{0};
    std::atomic<uint64_t> messages_dropped{0};
    std::atomic<uint64_t> routing_errors{0};
    std::atomic<uint64_t> scoop_errors{0};
    std::atomic<uint64_t> sink_errors{0};

    std::chrono::steady_clock::time_point start_time;
    std::chrono::nanoseconds total_processing_time{0};
    std::chrono::nanoseconds min_processing_time{std::chrono::nanoseconds::max()};
    std::chrono::nanoseconds max_processing_time{0};

    mutable std::mutex metrics_mutex;

    GatewayMetrics() : start_time(std::chrono::steady_clock::now()) {}

    // Copy constructor - needed for returning metrics
    GatewayMetrics(const GatewayMetrics& other)
        : messages_processed(other.messages_processed.load()),
          messages_routed(other.messages_routed.load()),
          messages_dropped(other.messages_dropped.load()),
          routing_errors(other.routing_errors.load()), scoop_errors(other.scoop_errors.load()),
          sink_errors(other.sink_errors.load()), start_time(other.start_time),
          total_processing_time(other.total_processing_time),
          min_processing_time(other.min_processing_time),
          max_processing_time(other.max_processing_time) {}

    // Copy assignment
    GatewayMetrics& operator=(const GatewayMetrics& other) {
        if (this != &other) {
            messages_processed.store(other.messages_processed.load());
            messages_routed.store(other.messages_routed.load());
            messages_dropped.store(other.messages_dropped.load());
            routing_errors.store(other.routing_errors.load());
            scoop_errors.store(other.scoop_errors.load());
            sink_errors.store(other.sink_errors.load());
            start_time            = other.start_time;
            total_processing_time = other.total_processing_time;
            min_processing_time   = other.min_processing_time;
            max_processing_time   = other.max_processing_time;
        }
        return *this;
    }

    double get_messages_per_second() const {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        return seconds > 0 ? static_cast<double>(messages_processed.load()) / seconds : 0.0;
    }

    std::chrono::nanoseconds get_average_processing_time() const {
        auto processed = messages_processed.load();
        if (processed > 0) {
            return std::chrono::nanoseconds{total_processing_time.count() /
                                            static_cast<int64_t>(processed)};
        }
        return std::chrono::nanoseconds{0};
    }

    void update_processing_time(std::chrono::nanoseconds processing_time) {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        total_processing_time += processing_time;
        min_processing_time = std::min(min_processing_time, processing_time);
        max_processing_time = std::max(max_processing_time, processing_time);
    }
};

/**
 * @brief MQTT command types
 */
enum class MQTTCommandType {
    RELOAD_CONFIG,
    START_SCOOP,
    STOP_SCOOP,
    START_SINK,
    STOP_SINK,
    ADD_ROUTING_RULE,
    REMOVE_ROUTING_RULE,
    GET_STATUS,
    GET_METRICS,
    SET_LOG_LEVEL,
    SHUTDOWN
};

/**
 * @brief MQTT command structure
 */
struct MQTTCommand {
    MQTTCommandType type;
    std::string target_id;
    YAML::Node parameters;
    std::string request_id;
    std::chrono::steady_clock::time_point timestamp;
};

/**
 * @brief Main orchestrator for IPB Gateway
 *
 * This class manages the entire lifecycle of the IPB gateway, including:
 * - Loading and managing configuration
 * - Dynamic loading of protocol scoops and sinks
 * - EDF scheduling and routing
 * - MQTT command interface
 * - Health monitoring and metrics
 */
class IPBOrchestrator {
public:
    /**
     * @brief Constructor
     */
    explicit IPBOrchestrator(const std::string& config_file_path = "");

    /**
     * @brief Destructor
     */
    ~IPBOrchestrator();

    /**
     * @brief Initialize the orchestrator
     */
    common::Result<void> initialize();

    /**
     * @brief Start the gateway
     */
    common::Result<void> start();

    /**
     * @brief Stop the gateway
     */
    common::Result<void> stop();

    /**
     * @brief Shutdown the gateway
     */
    common::Result<void> shutdown();

    /**
     * @brief Check if the gateway is running
     */
    bool is_running() const { return running_.load(); }

    /**
     * @brief Check if the gateway is healthy
     */
    bool is_healthy() const;

    /**
     * @brief Get current metrics
     */
    GatewayMetrics get_metrics() const { return metrics_; }

    /**
     * @brief Get current configuration
     */
    const GatewayConfig& get_config() const { return config_; }

    /**
     * @brief Reload configuration from file
     */
    common::Result<void> reload_config();

    /**
     * @brief Update configuration at runtime
     */
    common::Result<void> update_config(const GatewayConfig& new_config);

    /**
     * @brief Process MQTT command
     */
    common::Result<YAML::Node> process_mqtt_command(const MQTTCommand& command);

    /**
     * @brief Get status information
     */
    YAML::Node get_status() const;

private:
    // Configuration
    std::string config_file_path_;
    GatewayConfig config_;
    std::unique_ptr<core::config::ConfigLoader> config_loader_;
    mutable std::mutex config_mutex_;

    // Core components
    std::unique_ptr<router::Router> router_;
    std::unique_ptr<core::RuleEngine> rule_engine_;

    // Dynamic components
    std::map<std::string, std::shared_ptr<common::IProtocolSource>> scoops_;
    std::map<std::string, std::shared_ptr<common::ISink>> sinks_;

    // State management
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_requested_{false};

    // Threading
    std::thread maintenance_thread_;
    std::thread config_monitor_thread_;
    std::thread mqtt_command_thread_;
    std::thread metrics_thread_;

    // Metrics and monitoring
    mutable GatewayMetrics metrics_;

    // MQTT command interface
    std::shared_ptr<common::IProtocolSource> mqtt_command_scoop_;
    std::shared_ptr<common::ISink> mqtt_response_sink_;
    std::queue<MQTTCommand> command_queue_;
    mutable std::mutex command_queue_mutex_;
    std::condition_variable command_queue_condition_;

    // Internal methods

    // Configuration management
    common::Result<void> load_config();
    common::Result<void> validate_config() const;
    void monitor_config_file();

    // Component management
    common::Result<void> load_scoops();
    common::Result<void> load_sinks();
    common::Result<void> setup_routing();

    common::Result<void> start_scoop(const std::string& scoop_id);
    common::Result<void> stop_scoop(const std::string& scoop_id);
    common::Result<void> start_sink(const std::string& sink_id);
    common::Result<void> stop_sink(const std::string& sink_id);

    // Dynamic loading
    std::shared_ptr<common::IProtocolSource> create_scoop(const core::config::ScoopConfig& config);
    std::shared_ptr<common::ISink> create_sink(const core::config::SinkConfig& config);

    // Routing integration
    void setup_rule_engine();
    common::Result<void> apply_routing_rules();

    // MQTT command processing
    void setup_mqtt_commands();
    void mqtt_command_loop();
    void process_command_queue();

    common::Result<YAML::Node> handle_reload_config_command(const MQTTCommand& command);
    common::Result<YAML::Node> handle_scoop_command(const MQTTCommand& command);
    common::Result<YAML::Node> handle_sink_command(const MQTTCommand& command);
    common::Result<YAML::Node> handle_routing_command(const MQTTCommand& command);
    common::Result<YAML::Node> handle_status_command(const MQTTCommand& command);
    common::Result<YAML::Node> handle_metrics_command(const MQTTCommand& command);

    void send_mqtt_response(const std::string& request_id, const YAML::Node& response);
    void send_mqtt_status();

    // Maintenance and monitoring
    void maintenance_loop();
    void metrics_loop();
    void health_check();

    // Utility methods
    void setup_realtime_scheduling();
    void setup_cpu_affinity();
    void setup_signal_handlers();

    std::string generate_request_id() const;
    std::chrono::steady_clock::time_point get_file_modification_time(
        const std::string& file_path) const;
};

/**
 * @brief Factory for creating orchestrator instances
 */
class OrchestratorFactory {
public:
    /**
     * @brief Create orchestrator with configuration file
     */
    static std::unique_ptr<IPBOrchestrator> create(const std::string& config_file);

    /**
     * @brief Create orchestrator with default configuration
     */
    static std::unique_ptr<IPBOrchestrator> create_default();

    /**
     * @brief Create orchestrator for testing
     */
    static std::unique_ptr<IPBOrchestrator> create_test();
};

}  // namespace ipb::gate
