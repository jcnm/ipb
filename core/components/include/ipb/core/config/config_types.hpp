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
 * @brief Route definition
 */
struct RouteConfig {
    std::string id;
    std::string name;
    std::string source_pattern;     ///< Glob or regex for source matching
    std::vector<std::string> sink_ids;
    bool enabled = true;
    uint32_t priority = 0;

    // Transformation
    std::string transform_script;   ///< Optional transformation
    std::map<std::string, std::string> field_mappings;
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

    // Routes
    std::vector<RouteConfig> routes;

    // Default behavior
    std::string default_sink_id;
    bool drop_unrouted = false;
};

// ============================================================================
// APPLICATION CONFIGURATION
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

/**
 * @brief Complete application configuration
 */
struct ApplicationConfig {
    std::string name = "ipb";
    std::string version = "1.0.0";
    std::string instance_id;

    // Components
    std::vector<ScoopConfig> scoops;
    std::vector<SinkConfig> sinks;
    RouterConfig router;

    // Operational
    LoggingConfig logging;
    MetricsConfig metrics;

    // Daemon mode
    bool daemon = false;
    std::string pid_file;
    std::string working_directory;

    // Platform profile
    common::DeploymentPlatform platform = common::DeploymentPlatform::SERVER_STANDARD;
};

} // namespace ipb::core::config
