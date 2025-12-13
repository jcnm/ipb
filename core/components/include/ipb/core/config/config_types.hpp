#pragma once

/**
 * @file config_types.hpp
 * @brief Configuration types for IPB components
 *
 * Defines configuration structures that can be loaded from
 * YAML (default) or JSON files.
 */

#include <ipb/common/protocol_capabilities.hpp>
#include <ipb/common/error.hpp>

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ipb::core::config {

// ============================================================================
// CONFIGURATION FORMAT
// ============================================================================

/**
 * @brief Supported configuration file formats
 */
enum class ConfigFormat : uint8_t {
    AUTO,   ///< Auto-detect from file extension
    YAML,   ///< YAML format (default)
    JSON    ///< JSON format
};

// ============================================================================
// BASE CONFIGURATION
// ============================================================================

/**
 * @brief Generic configuration value
 */
using ConfigValue = std::variant<
    std::monostate,           ///< null/empty
    bool,                     ///< boolean
    int64_t,                  ///< integer
    double,                   ///< floating point
    std::string,              ///< string
    std::vector<std::string>, ///< string array
    std::map<std::string, std::string>  ///< string map
>;

/**
 * @brief Base configuration with common fields
 */
struct BaseConfig {
    std::string id;                    ///< Unique identifier
    std::string name;                  ///< Human-readable name
    std::string description;           ///< Description
    bool enabled = true;               ///< Whether component is enabled
    std::map<std::string, ConfigValue> metadata;  ///< Additional metadata
};

// ============================================================================
// SECURITY CONFIGURATION
// ============================================================================

/**
 * @brief TLS/SSL configuration
 */
struct TlsConfig {
    bool enabled = false;
    std::string cert_file;
    std::string key_file;
    std::string ca_file;
    std::string ca_path;
    bool verify_peer = true;
    bool verify_hostname = true;
    std::string cipher_suites;
    std::string tls_version;  ///< "1.2", "1.3", or "auto"
};

/**
 * @brief Authentication configuration
 */
struct AuthConfig {
    common::AuthMechanism mechanism = common::AuthMechanism::NONE;
    std::string username;
    std::string password;
    std::string token;
    std::string certificate_file;
    std::string private_key_file;
    std::map<std::string, std::string> extra_params;
};

/**
 * @brief Complete security configuration
 */
struct SecurityConfig {
    TlsConfig tls;
    AuthConfig auth;
    bool encrypt_payload = false;
    bool sign_messages = false;
};

// ============================================================================
// CONNECTION CONFIGURATION
// ============================================================================

/**
 * @brief Network endpoint configuration
 */
struct EndpointConfig {
    std::string host;
    uint16_t port = 0;
    std::string path;           ///< For HTTP/WebSocket
    std::string protocol;       ///< tcp, udp, serial, etc.

    // Serial specific
    std::string device;         ///< /dev/ttyUSB0, COM1, etc.
    uint32_t baud_rate = 9600;
    uint8_t data_bits = 8;
    uint8_t stop_bits = 1;
    char parity = 'N';          ///< N, E, O

    std::string to_uri() const {
        if (!device.empty()) {
            return protocol + "://" + device;
        }
        return protocol + "://" + host + ":" + std::to_string(port) + path;
    }
};

/**
 * @brief Connection behavior configuration
 */
struct ConnectionConfig {
    EndpointConfig endpoint;
    SecurityConfig security;

    // Timeouts
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds read_timeout{30000};
    std::chrono::milliseconds write_timeout{30000};
    std::chrono::milliseconds keepalive_interval{60000};

    // Reconnection
    bool auto_reconnect = true;
    std::chrono::milliseconds reconnect_delay{1000};
    std::chrono::milliseconds max_reconnect_delay{60000};
    uint32_t max_reconnect_attempts = 0;  ///< 0 = infinite

    // Buffer sizes
    uint32_t send_buffer_size = 65536;
    uint32_t recv_buffer_size = 65536;
};

// ============================================================================
// SCOOP CONFIGURATION
// ============================================================================

/**
 * @brief Data point mapping for Scoops
 */
struct DataPointMapping {
    std::string source_address;     ///< Protocol-specific address
    std::string target_name;        ///< IPB DataPoint name
    std::string data_type;          ///< int, float, bool, string, etc.
    double scale_factor = 1.0;
    double offset = 0.0;
    std::string unit;
    std::map<std::string, std::string> metadata;
};

/**
 * @brief Polling configuration for Scoops
 */
struct PollingConfig {
    bool enabled = true;
    std::chrono::milliseconds interval{1000};
    std::chrono::milliseconds timeout{5000};
    uint32_t retry_count = 3;
    std::chrono::milliseconds retry_delay{100};
};

/**
 * @brief Subscription configuration for event-based Scoops
 */
struct SubscriptionConfig {
    bool enabled = false;
    std::vector<std::string> topics;
    uint8_t qos = 0;
    bool persistent = false;
};

/**
 * @brief Complete Scoop configuration
 */
struct ScoopConfig : BaseConfig {
    // Protocol
    common::ProtocolType protocol_type = common::ProtocolType::CUSTOM;
    std::string protocol_version;

    // Connection
    ConnectionConfig connection;

    // Data acquisition
    PollingConfig polling;
    SubscriptionConfig subscription;
    std::vector<DataPointMapping> mappings;

    // Protocol-specific settings
    std::map<std::string, ConfigValue> protocol_settings;

    // Platform requirements (optional)
    std::optional<common::ProtocolCapabilities> capabilities;

    // Behavior
    bool start_on_load = true;
    uint32_t priority = 0;
    bool is_primary = false;
};

// ============================================================================
// SINK CONFIGURATION
// ============================================================================

/**
 * @brief Output formatting configuration
 */
struct FormatConfig {
    std::string format;             ///< json, csv, binary, custom
    std::string timestamp_format;   ///< ISO8601, unix, custom
    std::string encoding;           ///< utf-8, ascii, etc.
    bool include_metadata = true;
    bool pretty_print = false;
    std::string custom_template;
};

/**
 * @brief Batching configuration for Sinks
 */
struct BatchConfig {
    bool enabled = false;
    uint32_t max_size = 100;
    std::chrono::milliseconds max_delay{1000};
    bool flush_on_shutdown = true;
};

/**
 * @brief Retry configuration for Sinks
 */
struct RetryConfig {
    bool enabled = true;
    uint32_t max_retries = 3;
    std::chrono::milliseconds initial_delay{100};
    std::chrono::milliseconds max_delay{10000};
    double backoff_multiplier = 2.0;
};

/**
 * @brief Filtering configuration
 */
struct FilterConfig {
    std::vector<std::string> include_patterns;  ///< Regex patterns to include
    std::vector<std::string> exclude_patterns;  ///< Regex patterns to exclude
    std::map<std::string, std::string> tag_filters;
    double min_change_threshold = 0.0;  ///< Minimum change to send
    std::chrono::milliseconds min_interval{0};  ///< Rate limiting
};

/**
 * @brief Complete Sink configuration
 */
struct SinkConfig : BaseConfig {
    // Protocol
    common::ProtocolType protocol_type = common::ProtocolType::CUSTOM;
    std::string protocol_version;

    // Connection
    ConnectionConfig connection;

    // Output
    FormatConfig format;
    BatchConfig batch;
    RetryConfig retry;
    FilterConfig filter;

    // Protocol-specific settings
    std::map<std::string, ConfigValue> protocol_settings;

    // Platform requirements (optional)
    std::optional<common::ProtocolCapabilities> capabilities;

    // Behavior
    bool start_on_load = true;
    uint32_t weight = 100;          ///< Load balancing weight
    uint32_t priority = 0;          ///< Failover priority
};

// ============================================================================
// ROUTER CONFIGURATION
// ============================================================================

/**
 * @brief Source filter for routing rules
 *
 * Defines criteria for matching data points to routing rules.
 * Supports address patterns, protocol filtering, and quality filtering.
 */
struct RouteFilterConfig {
    std::string address_pattern;              ///< Glob or regex pattern for address
    std::vector<std::string> protocol_ids;    ///< Filter by protocol IDs (empty = all)
    std::vector<std::string> quality_levels;  ///< Filter by quality (GOOD, BAD, etc.)
    std::vector<std::string> tags;            ///< Filter by tags

    // Value-based filtering
    bool enable_value_filter = false;
    std::string value_condition;              ///< e.g., "> 100", "between 0 100"
};

/**
 * @brief Destination configuration for a route
 */
struct RouteDestinationConfig {
    std::string sink_id;                      ///< Target sink ID
    uint32_t priority = 0;                    ///< Priority for this destination (higher = first)
    uint32_t weight = 100;                    ///< Load balancing weight
    bool failover_only = false;               ///< Only use if primary fails
};

/**
 * @brief Route definition
 */
struct RouteConfig {
    std::string id;
    std::string name;
    std::string source_pattern;     ///< Glob or regex for source matching (legacy)
    std::vector<std::string> sink_ids;  ///< Simple sink list (legacy)
    bool enabled = true;
    uint32_t priority = 0;

    // Enhanced filtering (preferred over source_pattern)
    RouteFilterConfig filter;

    // Enhanced destinations (preferred over sink_ids)
    std::vector<RouteDestinationConfig> destinations;

    // Transformation
    std::string transform_script;   ///< Optional transformation
    std::map<std::string, std::string> field_mappings;

    // Behavior
    bool stop_on_match = false;     ///< Stop evaluating further rules if matched
};

/**
 * @brief Router configuration
 */
struct RouterConfig {
    std::string id = "default";
    std::string name = "IPB Router";

    // Threading
    uint32_t worker_threads = 0;    ///< 0 = auto (CPU count)
    uint32_t queue_size = 10000;

    // Performance
    bool enable_zero_copy = true;
    bool enable_lock_free = true;
    uint32_t batch_size = 100;
    size_t routing_table_size = 1000;  ///< Max routing rules
    std::chrono::microseconds routing_timeout{500};  ///< Timeout per evaluation

    // Routes
    std::vector<RouteConfig> routes;

    // Default behavior
    std::string default_sink_id;
    bool drop_unrouted = false;
};

// ============================================================================
// SCHEDULER CONFIGURATION
// ============================================================================

/**
 * @brief EDF (Earliest Deadline First) scheduler configuration
 *
 * Real-time scheduling settings for deterministic task execution.
 */
struct SchedulerConfig {
    bool enabled = true;

    // Real-time settings
    bool enable_realtime_priority = false;    ///< Use RT scheduling (requires privileges)
    int realtime_priority = 50;               ///< RT priority (1-99, higher = more urgent)

    // CPU affinity
    bool enable_cpu_affinity = false;         ///< Pin threads to specific CPUs
    std::vector<int> cpu_cores;               ///< CPU cores to use (empty = auto)

    // Task management
    std::chrono::microseconds default_deadline{1000};  ///< Default task deadline (1ms)
    size_t max_tasks = 10000;                 ///< Maximum concurrent tasks
    size_t worker_threads = 0;                ///< Worker threads (0 = auto)

    // Behavior
    bool preemptive = true;                   ///< Allow task preemption
    std::chrono::milliseconds watchdog_timeout{5000};  ///< Watchdog for stuck tasks
};

// ============================================================================
// COMMAND INTERFACE CONFIGURATION
// ============================================================================

/**
 * @brief MQTT-based command interface configuration
 *
 * Allows remote management of the gateway via MQTT messages.
 */
struct CommandInterfaceConfig {
    bool enabled = false;

    // Connection
    std::string broker_url = "mqtt://localhost:1883";
    std::string client_id = "ipb-gateway-cmd";

    // Topics
    std::string command_topic = "ipb/gateway/commands";
    std::string response_topic = "ipb/gateway/responses";
    std::string status_topic = "ipb/gateway/status";

    // Behavior
    std::chrono::seconds status_interval{30};  ///< Status publish interval
    uint8_t qos = 1;                           ///< MQTT QoS level

    // Security
    SecurityConfig security;
};

// ============================================================================
// APPLICATION CONFIGURATION (base types first)
// ============================================================================

/**
 * @brief Logging configuration
 */
struct LoggingConfig {
    std::string level = "info";     ///< trace, debug, info, warn, error
    std::string output = "console"; ///< console, file, syslog
    std::string file_path;
    uint32_t max_file_size_mb = 100;
    uint32_t max_files = 5;
    bool include_timestamp = true;
    bool include_thread_id = false;
};

/**
 * @brief Metrics configuration
 */
struct MetricsConfig {
    bool enabled = true;
    std::chrono::seconds collection_interval{10};
    std::string export_format = "prometheus";
    std::string export_endpoint;
    uint16_t export_port = 9090;
};

// ============================================================================
// HEALTH & MONITORING CONFIGURATION
// ============================================================================

/**
 * @brief Health check configuration
 */
struct HealthCheckConfig {
    bool enabled = true;
    std::chrono::seconds interval{10};        ///< Health check interval
    std::chrono::seconds timeout{5};          ///< Timeout for health checks
    uint32_t unhealthy_threshold = 3;         ///< Failures before unhealthy
    uint32_t healthy_threshold = 2;           ///< Successes before healthy

    // Endpoints to check
    std::vector<std::string> check_endpoints;
};

/**
 * @brief Prometheus metrics export configuration
 */
struct PrometheusConfig {
    bool enabled = false;
    uint16_t port = 9090;
    std::string path = "/metrics";
    std::string bind_address = "0.0.0.0";
};

/**
 * @brief Complete monitoring configuration
 */
struct MonitoringConfig {
    MetricsConfig metrics;
    HealthCheckConfig health_check;
    PrometheusConfig prometheus;
};

// ============================================================================
// HOT RELOAD CONFIGURATION
// ============================================================================

/**
 * @brief Configuration hot reload settings
 */
struct HotReloadConfig {
    bool enabled = true;
    std::chrono::seconds check_interval{10};  ///< Config file check interval
    bool reload_scoops = true;                ///< Allow scoop config reload
    bool reload_sinks = true;                 ///< Allow sink config reload
    bool reload_routes = true;                ///< Allow route config reload
    bool graceful_restart = true;             ///< Graceful component restart
};

/**
 * @brief Complete application configuration
 *
 * This is the main configuration structure for IPB applications.
 * It includes all component configurations and operational settings.
 */
struct ApplicationConfig {
    std::string name = "ipb";
    std::string version = "1.0.0";
    std::string instance_id;

    // Components
    std::vector<ScoopConfig> scoops;
    std::vector<SinkConfig> sinks;
    RouterConfig router;
    SchedulerConfig scheduler;

    // Operational
    LoggingConfig logging;
    MonitoringConfig monitoring;
    HotReloadConfig hot_reload;
    CommandInterfaceConfig command_interface;

    // Daemon mode
    bool daemon = false;
    std::string pid_file;
    std::string working_directory;

    // Platform profile
    common::DeploymentPlatform platform = common::DeploymentPlatform::SERVER_STANDARD;
};

// ============================================================================
// CONFIGURATION CONVERSION UTILITIES
// ============================================================================

/**
 * @brief Convert RouteConfig to RuleEngine's RoutingRule
 *
 * Helper function to convert configuration structures to
 * the rule engine's internal representation.
 */
struct ConfigConverter {
    /**
     * @brief Get sink IDs from route config
     *
     * Returns sink_ids from destinations if available, otherwise from legacy sink_ids
     */
    static std::vector<std::string> get_sink_ids(const RouteConfig& route) {
        if (!route.destinations.empty()) {
            std::vector<std::string> ids;
            ids.reserve(route.destinations.size());
            for (const auto& dest : route.destinations) {
                ids.push_back(dest.sink_id);
            }
            return ids;
        }
        return route.sink_ids;
    }

    /**
     * @brief Get address pattern from route config
     *
     * Returns pattern from filter if available, otherwise from legacy source_pattern
     */
    static std::string get_pattern(const RouteConfig& route) {
        if (!route.filter.address_pattern.empty()) {
            return route.filter.address_pattern;
        }
        return route.source_pattern;
    }
};

} // namespace ipb::core::config
