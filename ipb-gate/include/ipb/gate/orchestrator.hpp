#pragma once

#include "ipb/common/interfaces.hpp"
#include "ipb/common/data_point.hpp"
#include "ipb/common/dataset.hpp"
#include "ipb/router/router.hpp"
#include <yaml-cpp/yaml.h>
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>

// Forward declarations for dynamic loading
namespace ipb::adapter::modbus { class ModbusAdapter; }
namespace ipb::adapter::opcua { class OPCUAAdapter; }
namespace ipb::adapter::mqtt { class MQTTAdapter; }
namespace ipb::sink::kafka { class KafkaSink; }
namespace ipb::sink::zmq { class ZMQSink; }
namespace ipb::sink::console { class ConsoleSink; }
namespace ipb::sink::syslog { class SyslogSink; }

namespace ipb::gate {

/**
 * @brief Configuration structure for IPB Gateway
 */
struct GatewayConfig {
    // General settings
    std::string instance_id = "ipb-gateway-001";
    std::string log_level = "INFO";
    bool enable_hot_reload = true;
    std::chrono::seconds config_check_interval{10};
    
    // EDF Scheduler settings
    struct {
        bool enable_realtime_priority = true;
        int realtime_priority = 50;  // 1-99
        bool enable_cpu_affinity = true;
        std::vector<int> cpu_cores;  // Empty = auto-detect
        std::chrono::microseconds default_deadline{1000};  // 1ms
        size_t max_tasks = 10000;
    } scheduler;
    
    // Router settings
    struct {
        size_t thread_pool_size = 4;
        bool enable_lock_free = true;
        bool enable_zero_copy = true;
        size_t routing_table_size = 1000;
        std::chrono::microseconds routing_timeout{500};
    } router;
    
    // Adapter configurations
    std::map<std::string, YAML::Node> adapters;
    
    // Sink configurations
    std::map<std::string, YAML::Node> sinks;
    
    // Routing rules
    std::vector<YAML::Node> routing_rules;
    
    // MQTT command interface settings
    struct {
        bool enable_mqtt_commands = false;
        std::string broker_url = "mqtt://localhost:1883";
        std::string command_topic = "ipb/gateway/commands";
        std::string response_topic = "ipb/gateway/responses";
        std::string status_topic = "ipb/gateway/status";
        std::chrono::seconds status_interval{30};
        std::string client_id = "ipb-gateway-cmd";
        
        // Authentication
        std::string username;
        std::string password;
        
        // TLS settings
        bool enable_tls = false;
        std::string ca_cert_path;
        std::string client_cert_path;
        std::string client_key_path;
    } mqtt_commands;
    
    // Monitoring settings
    struct {
        bool enable_prometheus_metrics = false;
        uint16_t prometheus_port = 9090;
        std::string prometheus_path = "/metrics";
        bool enable_health_checks = true;
        std::chrono::seconds health_check_interval{10};
    } monitoring;
};

/**
 * @brief Gateway statistics and metrics
 */
struct GatewayMetrics {
    std::atomic<uint64_t> messages_processed{0};
    std::atomic<uint64_t> messages_routed{0};
    std::atomic<uint64_t> messages_dropped{0};
    std::atomic<uint64_t> routing_errors{0};
    std::atomic<uint64_t> adapter_errors{0};
    std::atomic<uint64_t> sink_errors{0};
    
    std::chrono::steady_clock::time_point start_time;
    std::chrono::nanoseconds total_processing_time{0};
    std::chrono::nanoseconds min_processing_time{std::chrono::nanoseconds::max()};
    std::chrono::nanoseconds max_processing_time{0};
    
    mutable std::mutex metrics_mutex;
    
    GatewayMetrics() : start_time(std::chrono::steady_clock::now()) {}
    
    double get_messages_per_second() const {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        return seconds > 0 ? static_cast<double>(messages_processed.load()) / seconds : 0.0;
    }
    
    std::chrono::nanoseconds get_average_processing_time() const {
        auto processed = messages_processed.load();
        return processed > 0 ? total_processing_time / processed : std::chrono::nanoseconds{0};
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
    START_ADAPTER,
    STOP_ADAPTER,
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
 * - Dynamic loading of protocol adapters and sinks
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
    mutable std::mutex config_mutex_;
    
    // Core components
    std::unique_ptr<router::IPBRouter> router_;
    
    // Dynamic components
    std::map<std::string, std::shared_ptr<common::IProtocolSource>> adapters_;
    std::map<std::string, std::shared_ptr<common::IIPBSink>> sinks_;
    
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
    std::shared_ptr<common::IProtocolSource> mqtt_command_adapter_;
    std::shared_ptr<common::IIPBSink> mqtt_response_sink_;
    std::queue<MQTTCommand> command_queue_;
    mutable std::mutex command_queue_mutex_;
    std::condition_variable command_queue_condition_;
    
    // Internal methods
    
    // Configuration management
    common::Result<void> load_config();
    common::Result<void> validate_config() const;
    void monitor_config_file();
    
    // Component management
    common::Result<void> load_adapters();
    common::Result<void> load_sinks();
    common::Result<void> setup_routing();
    
    common::Result<void> start_adapter(const std::string& adapter_id);
    common::Result<void> stop_adapter(const std::string& adapter_id);
    common::Result<void> start_sink(const std::string& sink_id);
    common::Result<void> stop_sink(const std::string& sink_id);
    
    // Dynamic loading
    std::shared_ptr<common::IProtocolSource> create_adapter(const std::string& type, const YAML::Node& config);
    std::shared_ptr<common::IIPBSink> create_sink(const std::string& type, const YAML::Node& config);
    
    // MQTT command processing
    void setup_mqtt_commands();
    void mqtt_command_loop();
    void process_command_queue();
    
    common::Result<YAML::Node> handle_reload_config_command(const MQTTCommand& command);
    common::Result<YAML::Node> handle_adapter_command(const MQTTCommand& command);
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
    std::chrono::steady_clock::time_point get_file_modification_time(const std::string& file_path) const;
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

} // namespace ipb::gate

