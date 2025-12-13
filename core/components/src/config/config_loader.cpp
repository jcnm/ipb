/**
 * @file config_loader.cpp
 * @brief Configuration loader implementation
 */

#include <ipb/core/config/config_loader.hpp>

#include <yaml-cpp/yaml.h>
#include <json/json.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace ipb::core::config {

// ============================================================================
// FACTORY
// ============================================================================

std::unique_ptr<ConfigLoader> create_config_loader() {
    return std::make_unique<ConfigLoaderImpl>();
}

// ============================================================================
// FORMAT DETECTION
// ============================================================================

ConfigFormat ConfigLoader::detect_format(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".yaml" || ext == ".yml") {
        return ConfigFormat::YAML;
    } else if (ext == ".json") {
        return ConfigFormat::JSON;
    }

    return ConfigFormat::YAML;  // Default to YAML
}

ConfigFormat ConfigLoader::detect_format_from_content(std::string_view content) {
    // Skip whitespace
    size_t pos = 0;
    while (pos < content.size() && std::isspace(content[pos])) {
        ++pos;
    }

    if (pos >= content.size()) {
        return ConfigFormat::YAML;
    }

    // JSON starts with { or [
    if (content[pos] == '{' || content[pos] == '[') {
        return ConfigFormat::JSON;
    }

    // YAML document marker
    if (content.substr(pos, 3) == "---") {
        return ConfigFormat::YAML;
    }

    // Default to YAML (more permissive)
    return ConfigFormat::YAML;
}

// ============================================================================
// YAML PARSING HELPERS
// ============================================================================

namespace {

// Forward declarations for circular dependencies
MetricsConfig parse_metrics_config(const YAML::Node& node);
HealthCheckConfig parse_health_check_config(const YAML::Node& node);
PrometheusConfig parse_prometheus_config(const YAML::Node& node);
SecurityConfig parse_security_config(const YAML::Node& node);

template<typename T>
T yaml_get(const YAML::Node& node, const std::string& key, T default_value) {
    if (node[key]) {
        try {
            return node[key].as<T>();
        } catch (...) {
            return default_value;
        }
    }
    return default_value;
}

std::chrono::milliseconds yaml_get_ms(const YAML::Node& node, const std::string& key,
                                       std::chrono::milliseconds default_value) {
    if (node[key]) {
        try {
            return std::chrono::milliseconds(node[key].as<int64_t>());
        } catch (...) {
            return default_value;
        }
    }
    return default_value;
}

std::chrono::seconds yaml_get_sec(const YAML::Node& node, const std::string& key,
                                   std::chrono::seconds default_value) {
    if (node[key]) {
        try {
            return std::chrono::seconds(node[key].as<int64_t>());
        } catch (...) {
            return default_value;
        }
    }
    return default_value;
}

common::ProtocolType parse_protocol_type(const std::string& str) {
    static const std::map<std::string, common::ProtocolType> map = {
        // Industrial
        {"modbus_rtu", common::ProtocolType::MODBUS_RTU},
        {"modbus_tcp", common::ProtocolType::MODBUS_TCP},
        {"modbus_ascii", common::ProtocolType::MODBUS_ASCII},
        {"opcua", common::ProtocolType::OPCUA},
        {"profinet", common::ProtocolType::PROFINET},
        {"profibus", common::ProtocolType::PROFIBUS},
        {"ethercat", common::ProtocolType::ETHERCAT},
        {"canopen", common::ProtocolType::CANOPEN},
        {"devicenet", common::ProtocolType::DEVICENET},
        {"bacnet", common::ProtocolType::BACNET},
        {"hart", common::ProtocolType::HART},
        {"foundation_fieldbus", common::ProtocolType::FOUNDATION_FIELDBUS},
        // IoT
        {"mqtt", common::ProtocolType::MQTT},
        {"mqtt_sn", common::ProtocolType::MQTT_SN},
        {"coap", common::ProtocolType::COAP},
        {"amqp", common::ProtocolType::AMQP},
        {"dds", common::ProtocolType::DDS},
        {"sparkplug_b", common::ProtocolType::SPARKPLUG_B},
        {"lwm2m", common::ProtocolType::LWM2M},
        // IT
        {"http", common::ProtocolType::HTTP},
        {"https", common::ProtocolType::HTTPS},
        {"websocket", common::ProtocolType::WEBSOCKET},
        {"grpc", common::ProtocolType::GRPC},
        {"rest", common::ProtocolType::REST},
        {"graphql", common::ProtocolType::GRAPHQL},
        // Messaging
        {"kafka", common::ProtocolType::KAFKA},
        {"rabbitmq", common::ProtocolType::RABBITMQ},
        {"zeromq", common::ProtocolType::ZEROMQ},
        {"zmq", common::ProtocolType::ZEROMQ},
        {"redis_pubsub", common::ProtocolType::REDIS_PUBSUB},
        // Database
        {"influxdb", common::ProtocolType::INFLUXDB},
        {"timescaledb", common::ProtocolType::TIMESCALEDB},
        {"mongodb", common::ProtocolType::MONGODB},
        // Custom
        {"custom", common::ProtocolType::CUSTOM},
    };

    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    auto it = map.find(lower);
    if (it != map.end()) {
        return it->second;
    }
    return common::ProtocolType::CUSTOM;
}

std::string protocol_type_to_string(common::ProtocolType type) {
    static const std::map<common::ProtocolType, std::string> map = {
        // Industrial
        {common::ProtocolType::MODBUS_RTU, "modbus_rtu"},
        {common::ProtocolType::MODBUS_TCP, "modbus_tcp"},
        {common::ProtocolType::MODBUS_ASCII, "modbus_ascii"},
        {common::ProtocolType::OPCUA, "opcua"},
        {common::ProtocolType::PROFINET, "profinet"},
        {common::ProtocolType::PROFIBUS, "profibus"},
        {common::ProtocolType::ETHERCAT, "ethercat"},
        {common::ProtocolType::CANOPEN, "canopen"},
        {common::ProtocolType::DEVICENET, "devicenet"},
        {common::ProtocolType::BACNET, "bacnet"},
        {common::ProtocolType::HART, "hart"},
        {common::ProtocolType::FOUNDATION_FIELDBUS, "foundation_fieldbus"},
        // IoT
        {common::ProtocolType::MQTT, "mqtt"},
        {common::ProtocolType::MQTT_SN, "mqtt_sn"},
        {common::ProtocolType::COAP, "coap"},
        {common::ProtocolType::AMQP, "amqp"},
        {common::ProtocolType::DDS, "dds"},
        {common::ProtocolType::SPARKPLUG_B, "sparkplug_b"},
        {common::ProtocolType::LWM2M, "lwm2m"},
        // IT
        {common::ProtocolType::HTTP, "http"},
        {common::ProtocolType::HTTPS, "https"},
        {common::ProtocolType::WEBSOCKET, "websocket"},
        {common::ProtocolType::GRPC, "grpc"},
        {common::ProtocolType::REST, "rest"},
        {common::ProtocolType::GRAPHQL, "graphql"},
        // Messaging
        {common::ProtocolType::KAFKA, "kafka"},
        {common::ProtocolType::RABBITMQ, "rabbitmq"},
        {common::ProtocolType::ZEROMQ, "zeromq"},
        {common::ProtocolType::REDIS_PUBSUB, "redis_pubsub"},
        // Database
        {common::ProtocolType::INFLUXDB, "influxdb"},
        {common::ProtocolType::TIMESCALEDB, "timescaledb"},
        {common::ProtocolType::MONGODB, "mongodb"},
        // Custom
        {common::ProtocolType::CUSTOM, "custom"},
    };

    auto it = map.find(type);
    if (it != map.end()) {
        return it->second;
    }
    return "custom";
}

common::AuthMechanism parse_auth_mechanism(const std::string& str) {
    static const std::map<std::string, common::AuthMechanism> map = {
        {"none", common::AuthMechanism::NONE},
        {"username_password", common::AuthMechanism::USERNAME_PASSWORD},
        {"basic", common::AuthMechanism::USERNAME_PASSWORD},
        {"certificate", common::AuthMechanism::CERTIFICATE_X509},
        {"certificate_x509", common::AuthMechanism::CERTIFICATE_X509},
        {"token", common::AuthMechanism::TOKEN_JWT},
        {"token_jwt", common::AuthMechanism::TOKEN_JWT},
        {"jwt", common::AuthMechanism::TOKEN_JWT},
        {"oauth2", common::AuthMechanism::TOKEN_OAUTH2},
        {"token_oauth2", common::AuthMechanism::TOKEN_OAUTH2},
        {"kerberos", common::AuthMechanism::KERBEROS},
        {"ldap", common::AuthMechanism::LDAP},
        {"saml", common::AuthMechanism::SAML},
        {"api_key", common::AuthMechanism::API_KEY},
        {"apikey", common::AuthMechanism::API_KEY},
        {"mutual_tls", common::AuthMechanism::MUTUAL_TLS},
        {"mtls", common::AuthMechanism::MUTUAL_TLS},
        {"custom", common::AuthMechanism::CUSTOM},
    };

    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    auto it = map.find(lower);
    if (it != map.end()) {
        return it->second;
    }
    return common::AuthMechanism::NONE;
}

common::DeploymentPlatform parse_platform(const std::string& str) {
    static const std::map<std::string, common::DeploymentPlatform> map = {
        {"embedded_bare_metal", common::DeploymentPlatform::EMBEDDED_BARE_METAL},
        {"embedded_rtos", common::DeploymentPlatform::EMBEDDED_RTOS},
        {"embedded_linux", common::DeploymentPlatform::EMBEDDED_LINUX},
        {"edge_gateway", common::DeploymentPlatform::EDGE_GATEWAY},
        {"edge_mobile", common::DeploymentPlatform::EDGE_MOBILE},
        {"server_standard", common::DeploymentPlatform::SERVER_STANDARD},
        {"server_cloud", common::DeploymentPlatform::SERVER_CLOUD},
        {"server_containerized", common::DeploymentPlatform::SERVER_CONTAINERIZED},
    };

    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    auto it = map.find(lower);
    if (it != map.end()) {
        return it->second;
    }
    return common::DeploymentPlatform::SERVER_STANDARD;
}

// Parse TLS configuration
TlsConfig parse_tls_config(const YAML::Node& node) {
    TlsConfig config;
    if (!node) return config;

    config.enabled = yaml_get(node, "enabled", false);
    config.cert_file = yaml_get<std::string>(node, "cert_file", "");
    config.key_file = yaml_get<std::string>(node, "key_file", "");
    config.ca_file = yaml_get<std::string>(node, "ca_file", "");
    config.ca_path = yaml_get<std::string>(node, "ca_path", "");
    config.verify_peer = yaml_get(node, "verify_peer", true);
    config.verify_hostname = yaml_get(node, "verify_hostname", true);
    config.cipher_suites = yaml_get<std::string>(node, "cipher_suites", "");
    config.tls_version = yaml_get<std::string>(node, "tls_version", "auto");

    return config;
}

// Parse Auth configuration
AuthConfig parse_auth_config(const YAML::Node& node) {
    AuthConfig config;
    if (!node) return config;

    config.mechanism = parse_auth_mechanism(yaml_get<std::string>(node, "mechanism", "none"));
    config.username = yaml_get<std::string>(node, "username", "");
    config.password = yaml_get<std::string>(node, "password", "");
    config.token = yaml_get<std::string>(node, "token", "");
    config.certificate_file = yaml_get<std::string>(node, "certificate_file", "");
    config.private_key_file = yaml_get<std::string>(node, "private_key_file", "");

    if (node["extra_params"]) {
        for (const auto& kv : node["extra_params"]) {
            config.extra_params[kv.first.as<std::string>()] = kv.second.as<std::string>();
        }
    }

    return config;
}

// Parse Security configuration
SecurityConfig parse_security_config(const YAML::Node& node) {
    SecurityConfig config;
    if (!node) return config;

    config.tls = parse_tls_config(node["tls"]);
    config.auth = parse_auth_config(node["auth"]);
    config.encrypt_payload = yaml_get(node, "encrypt_payload", false);
    config.sign_messages = yaml_get(node, "sign_messages", false);

    return config;
}

// Parse Endpoint configuration
EndpointConfig parse_endpoint_config(const YAML::Node& node) {
    EndpointConfig config;
    if (!node) return config;

    config.host = yaml_get<std::string>(node, "host", "");
    config.port = yaml_get<uint16_t>(node, "port", 0);
    config.path = yaml_get<std::string>(node, "path", "");
    config.protocol = yaml_get<std::string>(node, "protocol", "tcp");
    config.device = yaml_get<std::string>(node, "device", "");
    config.baud_rate = yaml_get<uint32_t>(node, "baud_rate", 9600);
    config.data_bits = yaml_get<uint8_t>(node, "data_bits", 8);
    config.stop_bits = yaml_get<uint8_t>(node, "stop_bits", 1);

    std::string parity = yaml_get<std::string>(node, "parity", "N");
    config.parity = parity.empty() ? 'N' : parity[0];

    return config;
}

// Parse Connection configuration
ConnectionConfig parse_connection_config(const YAML::Node& node) {
    ConnectionConfig config;
    if (!node) return config;

    config.endpoint = parse_endpoint_config(node["endpoint"]);
    config.security = parse_security_config(node["security"]);

    config.connect_timeout = yaml_get_ms(node, "connect_timeout", std::chrono::milliseconds{5000});
    config.read_timeout = yaml_get_ms(node, "read_timeout", std::chrono::milliseconds{30000});
    config.write_timeout = yaml_get_ms(node, "write_timeout", std::chrono::milliseconds{30000});
    config.keepalive_interval = yaml_get_ms(node, "keepalive_interval", std::chrono::milliseconds{60000});

    config.auto_reconnect = yaml_get(node, "auto_reconnect", true);
    config.reconnect_delay = yaml_get_ms(node, "reconnect_delay", std::chrono::milliseconds{1000});
    config.max_reconnect_delay = yaml_get_ms(node, "max_reconnect_delay", std::chrono::milliseconds{60000});
    config.max_reconnect_attempts = yaml_get<uint32_t>(node, "max_reconnect_attempts", 0);

    config.send_buffer_size = yaml_get<uint32_t>(node, "send_buffer_size", 65536);
    config.recv_buffer_size = yaml_get<uint32_t>(node, "recv_buffer_size", 65536);

    return config;
}

// Parse Polling configuration
PollingConfig parse_polling_config(const YAML::Node& node) {
    PollingConfig config;
    if (!node) return config;

    config.enabled = yaml_get(node, "enabled", true);
    config.interval = yaml_get_ms(node, "interval", std::chrono::milliseconds{1000});
    config.timeout = yaml_get_ms(node, "timeout", std::chrono::milliseconds{5000});
    config.retry_count = yaml_get<uint32_t>(node, "retry_count", 3);
    config.retry_delay = yaml_get_ms(node, "retry_delay", std::chrono::milliseconds{100});

    return config;
}

// Parse Subscription configuration
SubscriptionConfig parse_subscription_config(const YAML::Node& node) {
    SubscriptionConfig config;
    if (!node) return config;

    config.enabled = yaml_get(node, "enabled", false);
    config.qos = yaml_get<uint8_t>(node, "qos", 0);
    config.persistent = yaml_get(node, "persistent", false);

    if (node["topics"]) {
        for (const auto& topic : node["topics"]) {
            config.topics.push_back(topic.as<std::string>());
        }
    }

    return config;
}

// Parse DataPoint mapping
DataPointMapping parse_datapoint_mapping(const YAML::Node& node) {
    DataPointMapping mapping;
    if (!node) return mapping;

    mapping.source_address = yaml_get<std::string>(node, "source_address", "");
    mapping.target_name = yaml_get<std::string>(node, "target_name", "");
    mapping.data_type = yaml_get<std::string>(node, "data_type", "");
    mapping.scale_factor = yaml_get<double>(node, "scale_factor", 1.0);
    mapping.offset = yaml_get<double>(node, "offset", 0.0);
    mapping.unit = yaml_get<std::string>(node, "unit", "");

    if (node["metadata"]) {
        for (const auto& kv : node["metadata"]) {
            mapping.metadata[kv.first.as<std::string>()] = kv.second.as<std::string>();
        }
    }

    return mapping;
}

// Parse Format configuration
FormatConfig parse_format_config(const YAML::Node& node) {
    FormatConfig config;
    if (!node) return config;

    config.format = yaml_get<std::string>(node, "format", "json");
    config.timestamp_format = yaml_get<std::string>(node, "timestamp_format", "ISO8601");
    config.encoding = yaml_get<std::string>(node, "encoding", "utf-8");
    config.include_metadata = yaml_get(node, "include_metadata", true);
    config.pretty_print = yaml_get(node, "pretty_print", false);
    config.custom_template = yaml_get<std::string>(node, "custom_template", "");

    return config;
}

// Parse Batch configuration
BatchConfig parse_batch_config(const YAML::Node& node) {
    BatchConfig config;
    if (!node) return config;

    config.enabled = yaml_get(node, "enabled", false);
    config.max_size = yaml_get<uint32_t>(node, "max_size", 100);
    config.max_delay = yaml_get_ms(node, "max_delay", std::chrono::milliseconds{1000});
    config.flush_on_shutdown = yaml_get(node, "flush_on_shutdown", true);

    return config;
}

// Parse Retry configuration
RetryConfig parse_retry_config(const YAML::Node& node) {
    RetryConfig config;
    if (!node) return config;

    config.enabled = yaml_get(node, "enabled", true);
    config.max_retries = yaml_get<uint32_t>(node, "max_retries", 3);
    config.initial_delay = yaml_get_ms(node, "initial_delay", std::chrono::milliseconds{100});
    config.max_delay = yaml_get_ms(node, "max_delay", std::chrono::milliseconds{10000});
    config.backoff_multiplier = yaml_get<double>(node, "backoff_multiplier", 2.0);

    return config;
}

// Parse Filter configuration
FilterConfig parse_filter_config(const YAML::Node& node) {
    FilterConfig config;
    if (!node) return config;

    if (node["include_patterns"]) {
        for (const auto& pattern : node["include_patterns"]) {
            config.include_patterns.push_back(pattern.as<std::string>());
        }
    }

    if (node["exclude_patterns"]) {
        for (const auto& pattern : node["exclude_patterns"]) {
            config.exclude_patterns.push_back(pattern.as<std::string>());
        }
    }

    if (node["tag_filters"]) {
        for (const auto& kv : node["tag_filters"]) {
            config.tag_filters[kv.first.as<std::string>()] = kv.second.as<std::string>();
        }
    }

    config.min_change_threshold = yaml_get<double>(node, "min_change_threshold", 0.0);
    config.min_interval = yaml_get_ms(node, "min_interval", std::chrono::milliseconds{0});

    return config;
}

// Parse Route filter configuration
RouteFilterConfig parse_route_filter_config(const YAML::Node& node) {
    RouteFilterConfig config;
    if (!node) return config;

    config.address_pattern = yaml_get<std::string>(node, "address_pattern", "");
    config.enable_value_filter = yaml_get(node, "enable_value_filter", false);
    config.value_condition = yaml_get<std::string>(node, "value_condition", "");

    if (node["protocol_ids"]) {
        for (const auto& id : node["protocol_ids"]) {
            config.protocol_ids.push_back(id.as<std::string>());
        }
    }

    if (node["quality_levels"]) {
        for (const auto& level : node["quality_levels"]) {
            config.quality_levels.push_back(level.as<std::string>());
        }
    }

    if (node["tags"]) {
        for (const auto& tag : node["tags"]) {
            config.tags.push_back(tag.as<std::string>());
        }
    }

    return config;
}

// Parse Route destination configuration
RouteDestinationConfig parse_route_destination_config(const YAML::Node& node) {
    RouteDestinationConfig config;
    if (!node) return config;

    config.sink_id = yaml_get<std::string>(node, "sink_id", "");
    config.priority = yaml_get<uint32_t>(node, "priority", 0);
    config.weight = yaml_get<uint32_t>(node, "weight", 100);
    config.failover_only = yaml_get(node, "failover_only", false);

    return config;
}

// Parse Route configuration
RouteConfig parse_route_config(const YAML::Node& node) {
    RouteConfig config;
    if (!node) return config;

    config.id = yaml_get<std::string>(node, "id", "");
    config.name = yaml_get<std::string>(node, "name", "");
    config.source_pattern = yaml_get<std::string>(node, "source_pattern", "");
    config.enabled = yaml_get(node, "enabled", true);
    config.priority = yaml_get<uint32_t>(node, "priority", 0);
    config.transform_script = yaml_get<std::string>(node, "transform_script", "");
    config.stop_on_match = yaml_get(node, "stop_on_match", false);

    // Parse filter (enhanced routing)
    if (node["filter"]) {
        config.filter = parse_route_filter_config(node["filter"]);
    }

    // Parse destinations (enhanced routing)
    if (node["destinations"]) {
        for (const auto& dest_node : node["destinations"]) {
            config.destinations.push_back(parse_route_destination_config(dest_node));
        }
    }

    // Legacy sink_ids support
    if (node["sink_ids"]) {
        for (const auto& id : node["sink_ids"]) {
            config.sink_ids.push_back(id.as<std::string>());
        }
    }

    if (node["field_mappings"]) {
        for (const auto& kv : node["field_mappings"]) {
            config.field_mappings[kv.first.as<std::string>()] = kv.second.as<std::string>();
        }
    }

    return config;
}

// Parse Scheduler configuration
SchedulerConfig parse_scheduler_config(const YAML::Node& node) {
    SchedulerConfig config;
    if (!node) return config;

    config.enabled = yaml_get(node, "enabled", true);
    config.enable_realtime_priority = yaml_get(node, "enable_realtime_priority", false);
    config.realtime_priority = yaml_get<int>(node, "realtime_priority", 50);
    config.enable_cpu_affinity = yaml_get(node, "enable_cpu_affinity", false);
    config.default_deadline = std::chrono::microseconds(
        yaml_get<int64_t>(node, "default_deadline_us", 1000));
    config.max_tasks = yaml_get<size_t>(node, "max_tasks", 10000);
    config.worker_threads = yaml_get<size_t>(node, "worker_threads", 0);
    config.preemptive = yaml_get(node, "preemptive", true);
    config.watchdog_timeout = yaml_get_ms(node, "watchdog_timeout", std::chrono::milliseconds{5000});

    if (node["cpu_cores"]) {
        for (const auto& core : node["cpu_cores"]) {
            config.cpu_cores.push_back(core.as<int>());
        }
    }

    return config;
}

// Parse Command interface configuration
CommandInterfaceConfig parse_command_interface_config(const YAML::Node& node) {
    CommandInterfaceConfig config;
    if (!node) return config;

    config.enabled = yaml_get(node, "enabled", false);
    config.broker_url = yaml_get<std::string>(node, "broker_url", "mqtt://localhost:1883");
    config.client_id = yaml_get<std::string>(node, "client_id", "ipb-gateway-cmd");
    config.command_topic = yaml_get<std::string>(node, "command_topic", "ipb/gateway/commands");
    config.response_topic = yaml_get<std::string>(node, "response_topic", "ipb/gateway/responses");
    config.status_topic = yaml_get<std::string>(node, "status_topic", "ipb/gateway/status");
    config.status_interval = yaml_get_sec(node, "status_interval", std::chrono::seconds{30});
    config.qos = yaml_get<uint8_t>(node, "qos", 1);

    if (node["security"]) {
        config.security = parse_security_config(node["security"]);
    }

    return config;
}

// Parse Health check configuration
HealthCheckConfig parse_health_check_config(const YAML::Node& node) {
    HealthCheckConfig config;
    if (!node) return config;

    config.enabled = yaml_get(node, "enabled", true);
    config.interval = yaml_get_sec(node, "interval", std::chrono::seconds{10});
    config.timeout = yaml_get_sec(node, "timeout", std::chrono::seconds{5});
    config.unhealthy_threshold = yaml_get<uint32_t>(node, "unhealthy_threshold", 3);
    config.healthy_threshold = yaml_get<uint32_t>(node, "healthy_threshold", 2);

    if (node["check_endpoints"]) {
        for (const auto& endpoint : node["check_endpoints"]) {
            config.check_endpoints.push_back(endpoint.as<std::string>());
        }
    }

    return config;
}

// Parse Prometheus configuration
PrometheusConfig parse_prometheus_config(const YAML::Node& node) {
    PrometheusConfig config;
    if (!node) return config;

    config.enabled = yaml_get(node, "enabled", false);
    config.port = yaml_get<uint16_t>(node, "port", 9090);
    config.path = yaml_get<std::string>(node, "path", "/metrics");
    config.bind_address = yaml_get<std::string>(node, "bind_address", "0.0.0.0");

    return config;
}

// Parse Monitoring configuration
MonitoringConfig parse_monitoring_config(const YAML::Node& node) {
    MonitoringConfig config;
    if (!node) return config;

    config.metrics = parse_metrics_config(node["metrics"]);
    config.health_check = parse_health_check_config(node["health_check"]);
    config.prometheus = parse_prometheus_config(node["prometheus"]);

    return config;
}

// Parse Hot reload configuration
HotReloadConfig parse_hot_reload_config(const YAML::Node& node) {
    HotReloadConfig config;
    if (!node) return config;

    config.enabled = yaml_get(node, "enabled", true);
    config.check_interval = yaml_get_sec(node, "check_interval", std::chrono::seconds{10});
    config.reload_scoops = yaml_get(node, "reload_scoops", true);
    config.reload_sinks = yaml_get(node, "reload_sinks", true);
    config.reload_routes = yaml_get(node, "reload_routes", true);
    config.graceful_restart = yaml_get(node, "graceful_restart", true);

    return config;
}

// Parse Logging configuration
LoggingConfig parse_logging_config(const YAML::Node& node) {
    LoggingConfig config;
    if (!node) return config;

    config.level = yaml_get<std::string>(node, "level", "info");
    config.output = yaml_get<std::string>(node, "output", "console");
    config.file_path = yaml_get<std::string>(node, "file_path", "");
    config.max_file_size_mb = yaml_get<uint32_t>(node, "max_file_size_mb", 100);
    config.max_files = yaml_get<uint32_t>(node, "max_files", 5);
    config.include_timestamp = yaml_get(node, "include_timestamp", true);
    config.include_thread_id = yaml_get(node, "include_thread_id", false);

    return config;
}

// Parse Metrics configuration
MetricsConfig parse_metrics_config(const YAML::Node& node) {
    MetricsConfig config;
    if (!node) return config;

    config.enabled = yaml_get(node, "enabled", true);
    config.collection_interval = yaml_get_sec(node, "collection_interval", std::chrono::seconds{10});
    config.export_format = yaml_get<std::string>(node, "export_format", "prometheus");
    config.export_endpoint = yaml_get<std::string>(node, "export_endpoint", "");
    config.export_port = yaml_get<uint16_t>(node, "export_port", 9090);

    return config;
}

// Parse base config fields
void parse_base_config(const YAML::Node& node, BaseConfig& config) {
    config.id = yaml_get<std::string>(node, "id", "");
    config.name = yaml_get<std::string>(node, "name", "");
    config.description = yaml_get<std::string>(node, "description", "");
    config.enabled = yaml_get(node, "enabled", true);

    if (node["metadata"]) {
        for (const auto& kv : node["metadata"]) {
            // Simple string metadata only for now
            try {
                config.metadata[kv.first.as<std::string>()] = kv.second.as<std::string>();
            } catch (...) {
                // Skip complex values
            }
        }
    }
}

// Parse Scoop configuration from YAML node
ScoopConfig parse_scoop_from_yaml(const YAML::Node& node) {
    ScoopConfig config;
    parse_base_config(node, config);

    config.protocol_type = parse_protocol_type(
        yaml_get<std::string>(node, "protocol_type", "custom"));
    config.protocol_version = yaml_get<std::string>(node, "protocol_version", "");

    config.connection = parse_connection_config(node["connection"]);
    config.polling = parse_polling_config(node["polling"]);
    config.subscription = parse_subscription_config(node["subscription"]);

    if (node["mappings"]) {
        for (const auto& mapping_node : node["mappings"]) {
            config.mappings.push_back(parse_datapoint_mapping(mapping_node));
        }
    }

    config.start_on_load = yaml_get(node, "start_on_load", true);
    config.priority = yaml_get<uint32_t>(node, "priority", 0);
    config.is_primary = yaml_get(node, "is_primary", false);

    return config;
}

// Parse Sink configuration from YAML node
SinkConfig parse_sink_from_yaml(const YAML::Node& node) {
    SinkConfig config;
    parse_base_config(node, config);

    config.protocol_type = parse_protocol_type(
        yaml_get<std::string>(node, "protocol_type", "custom"));
    config.protocol_version = yaml_get<std::string>(node, "protocol_version", "");

    config.connection = parse_connection_config(node["connection"]);
    config.format = parse_format_config(node["format"]);
    config.batch = parse_batch_config(node["batch"]);
    config.retry = parse_retry_config(node["retry"]);
    config.filter = parse_filter_config(node["filter"]);

    config.start_on_load = yaml_get(node, "start_on_load", true);
    config.weight = yaml_get<uint32_t>(node, "weight", 100);
    config.priority = yaml_get<uint32_t>(node, "priority", 0);

    return config;
}

// Parse Router configuration from YAML node
RouterConfig parse_router_from_yaml(const YAML::Node& node) {
    RouterConfig config;

    config.id = yaml_get<std::string>(node, "id", "default");
    config.name = yaml_get<std::string>(node, "name", "IPB Router");
    config.worker_threads = yaml_get<uint32_t>(node, "worker_threads", 0);
    config.queue_size = yaml_get<uint32_t>(node, "queue_size", 10000);
    config.enable_zero_copy = yaml_get(node, "enable_zero_copy", true);
    config.enable_lock_free = yaml_get(node, "enable_lock_free", true);
    config.batch_size = yaml_get<uint32_t>(node, "batch_size", 100);
    config.routing_table_size = yaml_get<size_t>(node, "routing_table_size", 1000);
    config.routing_timeout = std::chrono::microseconds(
        yaml_get<int64_t>(node, "routing_timeout_us", 500));
    config.default_sink_id = yaml_get<std::string>(node, "default_sink_id", "");
    config.drop_unrouted = yaml_get(node, "drop_unrouted", false);

    if (node["routes"]) {
        for (const auto& route_node : node["routes"]) {
            config.routes.push_back(parse_route_config(route_node));
        }
    }

    return config;
}

// Parse Application configuration from YAML node
ApplicationConfig parse_application_from_yaml(const YAML::Node& root) {
    ApplicationConfig config;

    config.name = yaml_get<std::string>(root, "name", "ipb");
    config.version = yaml_get<std::string>(root, "version", "1.0.0");
    config.instance_id = yaml_get<std::string>(root, "instance_id", "");

    // Parse scoops
    if (root["scoops"]) {
        for (const auto& scoop_node : root["scoops"]) {
            config.scoops.push_back(parse_scoop_from_yaml(scoop_node));
        }
    }

    // Parse sinks
    if (root["sinks"]) {
        for (const auto& sink_node : root["sinks"]) {
            config.sinks.push_back(parse_sink_from_yaml(sink_node));
        }
    }

    // Parse router
    if (root["router"]) {
        config.router = parse_router_from_yaml(root["router"]);
    }

    // Parse scheduler
    if (root["scheduler"]) {
        config.scheduler = parse_scheduler_config(root["scheduler"]);
    }

    // Parse logging
    config.logging = parse_logging_config(root["logging"]);

    // Parse monitoring (combines metrics, health_check, prometheus)
    if (root["monitoring"]) {
        config.monitoring = parse_monitoring_config(root["monitoring"]);
    } else {
        // Legacy support: parse metrics at root level
        config.monitoring.metrics = parse_metrics_config(root["metrics"]);
    }

    // Parse hot reload
    if (root["hot_reload"]) {
        config.hot_reload = parse_hot_reload_config(root["hot_reload"]);
    }

    // Parse command interface
    if (root["command_interface"]) {
        config.command_interface = parse_command_interface_config(root["command_interface"]);
    }

    // Daemon settings
    config.daemon = yaml_get(root, "daemon", false);
    config.pid_file = yaml_get<std::string>(root, "pid_file", "");
    config.working_directory = yaml_get<std::string>(root, "working_directory", "");

    // Platform
    config.platform = parse_platform(
        yaml_get<std::string>(root, "platform", "server_standard"));

    return config;
}

} // anonymous namespace

// ============================================================================
// JSON PARSING HELPERS
// ============================================================================

namespace {

template<typename T>
T json_get(const Json::Value& node, const std::string& key, T default_value);

template<>
std::string json_get<std::string>(const Json::Value& node, const std::string& key, std::string default_value) {
    return node.get(key, default_value).asString();
}

template<>
bool json_get<bool>(const Json::Value& node, const std::string& key, bool default_value) {
    return node.get(key, default_value).asBool();
}

template<>
int64_t json_get<int64_t>(const Json::Value& node, const std::string& key, int64_t default_value) {
    return node.get(key, default_value).asInt64();
}

template<>
uint32_t json_get<uint32_t>(const Json::Value& node, const std::string& key, uint32_t default_value) {
    return node.get(key, default_value).asUInt();
}

template<>
uint16_t json_get<uint16_t>(const Json::Value& node, const std::string& key, uint16_t default_value) {
    return static_cast<uint16_t>(node.get(key, default_value).asUInt());
}

template<>
uint8_t json_get<uint8_t>(const Json::Value& node, const std::string& key, uint8_t default_value) {
    return static_cast<uint8_t>(node.get(key, default_value).asUInt());
}

template<>
double json_get<double>(const Json::Value& node, const std::string& key, double default_value) {
    return node.get(key, default_value).asDouble();
}

std::chrono::milliseconds json_get_ms(const Json::Value& node, const std::string& key,
                                       std::chrono::milliseconds default_value) {
    return std::chrono::milliseconds(node.get(key, static_cast<int64_t>(default_value.count())).asInt64());
}

std::chrono::seconds json_get_sec(const Json::Value& node, const std::string& key,
                                   std::chrono::seconds default_value) {
    return std::chrono::seconds(node.get(key, static_cast<int64_t>(default_value.count())).asInt64());
}

// Forward declarations for JSON parsers
TlsConfig parse_tls_config_json(const Json::Value& node);
AuthConfig parse_auth_config_json(const Json::Value& node);
SecurityConfig parse_security_config_json(const Json::Value& node);
EndpointConfig parse_endpoint_config_json(const Json::Value& node);
ConnectionConfig parse_connection_config_json(const Json::Value& node);
PollingConfig parse_polling_config_json(const Json::Value& node);
SubscriptionConfig parse_subscription_config_json(const Json::Value& node);
DataPointMapping parse_datapoint_mapping_json(const Json::Value& node);
FormatConfig parse_format_config_json(const Json::Value& node);
BatchConfig parse_batch_config_json(const Json::Value& node);
RetryConfig parse_retry_config_json(const Json::Value& node);
FilterConfig parse_filter_config_json(const Json::Value& node);
RouteConfig parse_route_config_json(const Json::Value& node);
LoggingConfig parse_logging_config_json(const Json::Value& node);
MetricsConfig parse_metrics_config_json(const Json::Value& node);

TlsConfig parse_tls_config_json(const Json::Value& node) {
    TlsConfig config;
    if (node.isNull()) return config;

    config.enabled = json_get<bool>(node, "enabled", false);
    config.cert_file = json_get<std::string>(node, "cert_file", "");
    config.key_file = json_get<std::string>(node, "key_file", "");
    config.ca_file = json_get<std::string>(node, "ca_file", "");
    config.ca_path = json_get<std::string>(node, "ca_path", "");
    config.verify_peer = json_get<bool>(node, "verify_peer", true);
    config.verify_hostname = json_get<bool>(node, "verify_hostname", true);
    config.cipher_suites = json_get<std::string>(node, "cipher_suites", "");
    config.tls_version = json_get<std::string>(node, "tls_version", "auto");

    return config;
}

AuthConfig parse_auth_config_json(const Json::Value& node) {
    AuthConfig config;
    if (node.isNull()) return config;

    config.mechanism = parse_auth_mechanism(json_get<std::string>(node, "mechanism", "none"));
    config.username = json_get<std::string>(node, "username", "");
    config.password = json_get<std::string>(node, "password", "");
    config.token = json_get<std::string>(node, "token", "");
    config.certificate_file = json_get<std::string>(node, "certificate_file", "");
    config.private_key_file = json_get<std::string>(node, "private_key_file", "");

    if (node.isMember("extra_params")) {
        for (const auto& key : node["extra_params"].getMemberNames()) {
            config.extra_params[key] = node["extra_params"][key].asString();
        }
    }

    return config;
}

SecurityConfig parse_security_config_json(const Json::Value& node) {
    SecurityConfig config;
    if (node.isNull()) return config;

    config.tls = parse_tls_config_json(node["tls"]);
    config.auth = parse_auth_config_json(node["auth"]);
    config.encrypt_payload = json_get<bool>(node, "encrypt_payload", false);
    config.sign_messages = json_get<bool>(node, "sign_messages", false);

    return config;
}

EndpointConfig parse_endpoint_config_json(const Json::Value& node) {
    EndpointConfig config;
    if (node.isNull()) return config;

    config.host = json_get<std::string>(node, "host", "");
    config.port = json_get<uint16_t>(node, "port", 0);
    config.path = json_get<std::string>(node, "path", "");
    config.protocol = json_get<std::string>(node, "protocol", "tcp");
    config.device = json_get<std::string>(node, "device", "");
    config.baud_rate = json_get<uint32_t>(node, "baud_rate", 9600);
    config.data_bits = json_get<uint8_t>(node, "data_bits", 8);
    config.stop_bits = json_get<uint8_t>(node, "stop_bits", 1);

    std::string parity = json_get<std::string>(node, "parity", "N");
    config.parity = parity.empty() ? 'N' : parity[0];

    return config;
}

ConnectionConfig parse_connection_config_json(const Json::Value& node) {
    ConnectionConfig config;
    if (node.isNull()) return config;

    config.endpoint = parse_endpoint_config_json(node["endpoint"]);
    config.security = parse_security_config_json(node["security"]);

    config.connect_timeout = json_get_ms(node, "connect_timeout", std::chrono::milliseconds{5000});
    config.read_timeout = json_get_ms(node, "read_timeout", std::chrono::milliseconds{30000});
    config.write_timeout = json_get_ms(node, "write_timeout", std::chrono::milliseconds{30000});
    config.keepalive_interval = json_get_ms(node, "keepalive_interval", std::chrono::milliseconds{60000});

    config.auto_reconnect = json_get<bool>(node, "auto_reconnect", true);
    config.reconnect_delay = json_get_ms(node, "reconnect_delay", std::chrono::milliseconds{1000});
    config.max_reconnect_delay = json_get_ms(node, "max_reconnect_delay", std::chrono::milliseconds{60000});
    config.max_reconnect_attempts = json_get<uint32_t>(node, "max_reconnect_attempts", 0);

    config.send_buffer_size = json_get<uint32_t>(node, "send_buffer_size", 65536);
    config.recv_buffer_size = json_get<uint32_t>(node, "recv_buffer_size", 65536);

    return config;
}

PollingConfig parse_polling_config_json(const Json::Value& node) {
    PollingConfig config;
    if (node.isNull()) return config;

    config.enabled = json_get<bool>(node, "enabled", true);
    config.interval = json_get_ms(node, "interval", std::chrono::milliseconds{1000});
    config.timeout = json_get_ms(node, "timeout", std::chrono::milliseconds{5000});
    config.retry_count = json_get<uint32_t>(node, "retry_count", 3);
    config.retry_delay = json_get_ms(node, "retry_delay", std::chrono::milliseconds{100});

    return config;
}

SubscriptionConfig parse_subscription_config_json(const Json::Value& node) {
    SubscriptionConfig config;
    if (node.isNull()) return config;

    config.enabled = json_get<bool>(node, "enabled", false);
    config.qos = json_get<uint8_t>(node, "qos", 0);
    config.persistent = json_get<bool>(node, "persistent", false);

    if (node.isMember("topics") && node["topics"].isArray()) {
        for (const auto& topic : node["topics"]) {
            config.topics.push_back(topic.asString());
        }
    }

    return config;
}

DataPointMapping parse_datapoint_mapping_json(const Json::Value& node) {
    DataPointMapping mapping;
    if (node.isNull()) return mapping;

    mapping.source_address = json_get<std::string>(node, "source_address", "");
    mapping.target_name = json_get<std::string>(node, "target_name", "");
    mapping.data_type = json_get<std::string>(node, "data_type", "");
    mapping.scale_factor = json_get<double>(node, "scale_factor", 1.0);
    mapping.offset = json_get<double>(node, "offset", 0.0);
    mapping.unit = json_get<std::string>(node, "unit", "");

    if (node.isMember("metadata")) {
        for (const auto& key : node["metadata"].getMemberNames()) {
            mapping.metadata[key] = node["metadata"][key].asString();
        }
    }

    return mapping;
}

FormatConfig parse_format_config_json(const Json::Value& node) {
    FormatConfig config;
    if (node.isNull()) return config;

    config.format = json_get<std::string>(node, "format", "json");
    config.timestamp_format = json_get<std::string>(node, "timestamp_format", "ISO8601");
    config.encoding = json_get<std::string>(node, "encoding", "utf-8");
    config.include_metadata = json_get<bool>(node, "include_metadata", true);
    config.pretty_print = json_get<bool>(node, "pretty_print", false);
    config.custom_template = json_get<std::string>(node, "custom_template", "");

    return config;
}

BatchConfig parse_batch_config_json(const Json::Value& node) {
    BatchConfig config;
    if (node.isNull()) return config;

    config.enabled = json_get<bool>(node, "enabled", false);
    config.max_size = json_get<uint32_t>(node, "max_size", 100);
    config.max_delay = json_get_ms(node, "max_delay", std::chrono::milliseconds{1000});
    config.flush_on_shutdown = json_get<bool>(node, "flush_on_shutdown", true);

    return config;
}

RetryConfig parse_retry_config_json(const Json::Value& node) {
    RetryConfig config;
    if (node.isNull()) return config;

    config.enabled = json_get<bool>(node, "enabled", true);
    config.max_retries = json_get<uint32_t>(node, "max_retries", 3);
    config.initial_delay = json_get_ms(node, "initial_delay", std::chrono::milliseconds{100});
    config.max_delay = json_get_ms(node, "max_delay", std::chrono::milliseconds{10000});
    config.backoff_multiplier = json_get<double>(node, "backoff_multiplier", 2.0);

    return config;
}

FilterConfig parse_filter_config_json(const Json::Value& node) {
    FilterConfig config;
    if (node.isNull()) return config;

    if (node.isMember("include_patterns") && node["include_patterns"].isArray()) {
        for (const auto& pattern : node["include_patterns"]) {
            config.include_patterns.push_back(pattern.asString());
        }
    }

    if (node.isMember("exclude_patterns") && node["exclude_patterns"].isArray()) {
        for (const auto& pattern : node["exclude_patterns"]) {
            config.exclude_patterns.push_back(pattern.asString());
        }
    }

    if (node.isMember("tag_filters")) {
        for (const auto& key : node["tag_filters"].getMemberNames()) {
            config.tag_filters[key] = node["tag_filters"][key].asString();
        }
    }

    config.min_change_threshold = json_get<double>(node, "min_change_threshold", 0.0);
    config.min_interval = json_get_ms(node, "min_interval", std::chrono::milliseconds{0});

    return config;
}

RouteConfig parse_route_config_json(const Json::Value& node) {
    RouteConfig config;
    if (node.isNull()) return config;

    config.id = json_get<std::string>(node, "id", "");
    config.name = json_get<std::string>(node, "name", "");
    config.source_pattern = json_get<std::string>(node, "source_pattern", "");
    config.enabled = json_get<bool>(node, "enabled", true);
    config.priority = json_get<uint32_t>(node, "priority", 0);
    config.transform_script = json_get<std::string>(node, "transform_script", "");

    if (node.isMember("sink_ids") && node["sink_ids"].isArray()) {
        for (const auto& id : node["sink_ids"]) {
            config.sink_ids.push_back(id.asString());
        }
    }

    if (node.isMember("field_mappings")) {
        for (const auto& key : node["field_mappings"].getMemberNames()) {
            config.field_mappings[key] = node["field_mappings"][key].asString();
        }
    }

    return config;
}

LoggingConfig parse_logging_config_json(const Json::Value& node) {
    LoggingConfig config;
    if (node.isNull()) return config;

    config.level = json_get<std::string>(node, "level", "info");
    config.output = json_get<std::string>(node, "output", "console");
    config.file_path = json_get<std::string>(node, "file_path", "");
    config.max_file_size_mb = json_get<uint32_t>(node, "max_file_size_mb", 100);
    config.max_files = json_get<uint32_t>(node, "max_files", 5);
    config.include_timestamp = json_get<bool>(node, "include_timestamp", true);
    config.include_thread_id = json_get<bool>(node, "include_thread_id", false);

    return config;
}

MetricsConfig parse_metrics_config_json(const Json::Value& node) {
    MetricsConfig config;
    if (node.isNull()) return config;

    config.enabled = json_get<bool>(node, "enabled", true);
    config.collection_interval = json_get_sec(node, "collection_interval", std::chrono::seconds{10});
    config.export_format = json_get<std::string>(node, "export_format", "prometheus");
    config.export_endpoint = json_get<std::string>(node, "export_endpoint", "");
    config.export_port = json_get<uint16_t>(node, "export_port", 9090);

    return config;
}

void parse_base_config_json(const Json::Value& node, BaseConfig& config) {
    config.id = json_get<std::string>(node, "id", "");
    config.name = json_get<std::string>(node, "name", "");
    config.description = json_get<std::string>(node, "description", "");
    config.enabled = json_get<bool>(node, "enabled", true);
}

ScoopConfig parse_scoop_from_json(const Json::Value& node) {
    ScoopConfig config;
    parse_base_config_json(node, config);

    config.protocol_type = parse_protocol_type(
        json_get<std::string>(node, "protocol_type", "custom"));
    config.protocol_version = json_get<std::string>(node, "protocol_version", "");

    config.connection = parse_connection_config_json(node["connection"]);
    config.polling = parse_polling_config_json(node["polling"]);
    config.subscription = parse_subscription_config_json(node["subscription"]);

    if (node.isMember("mappings") && node["mappings"].isArray()) {
        for (const auto& mapping_node : node["mappings"]) {
            config.mappings.push_back(parse_datapoint_mapping_json(mapping_node));
        }
    }

    config.start_on_load = json_get<bool>(node, "start_on_load", true);
    config.priority = json_get<uint32_t>(node, "priority", 0);
    config.is_primary = json_get<bool>(node, "is_primary", false);

    return config;
}

SinkConfig parse_sink_from_json(const Json::Value& node) {
    SinkConfig config;
    parse_base_config_json(node, config);

    config.protocol_type = parse_protocol_type(
        json_get<std::string>(node, "protocol_type", "custom"));
    config.protocol_version = json_get<std::string>(node, "protocol_version", "");

    config.connection = parse_connection_config_json(node["connection"]);
    config.format = parse_format_config_json(node["format"]);
    config.batch = parse_batch_config_json(node["batch"]);
    config.retry = parse_retry_config_json(node["retry"]);
    config.filter = parse_filter_config_json(node["filter"]);

    config.start_on_load = json_get<bool>(node, "start_on_load", true);
    config.weight = json_get<uint32_t>(node, "weight", 100);
    config.priority = json_get<uint32_t>(node, "priority", 0);

    return config;
}

RouterConfig parse_router_from_json(const Json::Value& node) {
    RouterConfig config;

    config.id = json_get<std::string>(node, "id", "default");
    config.name = json_get<std::string>(node, "name", "IPB Router");
    config.worker_threads = json_get<uint32_t>(node, "worker_threads", 0);
    config.queue_size = json_get<uint32_t>(node, "queue_size", 10000);
    config.enable_zero_copy = json_get<bool>(node, "enable_zero_copy", true);
    config.enable_lock_free = json_get<bool>(node, "enable_lock_free", true);
    config.batch_size = json_get<uint32_t>(node, "batch_size", 100);
    config.default_sink_id = json_get<std::string>(node, "default_sink_id", "");
    config.drop_unrouted = json_get<bool>(node, "drop_unrouted", false);

    if (node.isMember("routes") && node["routes"].isArray()) {
        for (const auto& route_node : node["routes"]) {
            config.routes.push_back(parse_route_config_json(route_node));
        }
    }

    return config;
}

ApplicationConfig parse_application_from_json(const Json::Value& root) {
    ApplicationConfig config;

    config.name = json_get<std::string>(root, "name", "ipb");
    config.version = json_get<std::string>(root, "version", "1.0.0");
    config.instance_id = json_get<std::string>(root, "instance_id", "");

    if (root.isMember("scoops") && root["scoops"].isArray()) {
        for (const auto& scoop_node : root["scoops"]) {
            config.scoops.push_back(parse_scoop_from_json(scoop_node));
        }
    }

    if (root.isMember("sinks") && root["sinks"].isArray()) {
        for (const auto& sink_node : root["sinks"]) {
            config.sinks.push_back(parse_sink_from_json(sink_node));
        }
    }

    if (root.isMember("router")) {
        config.router = parse_router_from_json(root["router"]);
    }

    config.logging = parse_logging_config_json(root["logging"]);

    // Legacy support: parse metrics at root level into monitoring.metrics
    config.monitoring.metrics = parse_metrics_config_json(root["metrics"]);

    config.daemon = json_get<bool>(root, "daemon", false);
    config.pid_file = json_get<std::string>(root, "pid_file", "");
    config.working_directory = json_get<std::string>(root, "working_directory", "");
    config.platform = parse_platform(json_get<std::string>(root, "platform", "server_standard"));

    return config;
}

} // anonymous namespace

// ============================================================================
// IMPLEMENTATION
// ============================================================================

common::Result<std::string> ConfigLoaderImpl::read_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return common::Result<std::string>(
            common::ErrorCode::NOT_FOUND,
            "Configuration file not found: " + path.string());
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return common::Result<std::string>(
            common::ErrorCode::OS_ERROR,
            "Failed to open configuration file: " + path.string());
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return common::Result<std::string>(buffer.str());
}

common::Result<void> ConfigLoaderImpl::write_file(const std::filesystem::path& path,
                                                   std::string_view content) {
    // Create parent directories if needed
    auto parent = path.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return common::Result<void>(
                common::ErrorCode::OS_ERROR,
                "Failed to create directory: " + parent.string());
        }
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        return common::Result<void>(
            common::ErrorCode::OS_ERROR,
            "Failed to open file for writing: " + path.string());
    }

    file << content;
    if (!file.good()) {
        return common::Result<void>(
            common::ErrorCode::OS_ERROR,
            "Failed to write to file: " + path.string());
    }

    return common::Result<void>();
}

ConfigFormat ConfigLoaderImpl::resolve_format(const std::filesystem::path& path, ConfigFormat format) {
    if (format == ConfigFormat::AUTO) {
        return detect_format(path);
    }
    return format;
}

// ============================================================================
// FILE LOADING
// ============================================================================

common::Result<ApplicationConfig> ConfigLoaderImpl::load_application(
    const std::filesystem::path& path, ConfigFormat format) {

    auto content_result = read_file(path);
    if (!content_result) {
        return common::Result<ApplicationConfig>(
            content_result.error_code(), content_result.error_message());
    }

    return parse_application(content_result.value(), resolve_format(path, format));
}

common::Result<ScoopConfig> ConfigLoaderImpl::load_scoop(
    const std::filesystem::path& path, ConfigFormat format) {

    auto content_result = read_file(path);
    if (!content_result) {
        return common::Result<ScoopConfig>(
            content_result.error_code(), content_result.error_message());
    }

    return parse_scoop(content_result.value(), resolve_format(path, format));
}

common::Result<SinkConfig> ConfigLoaderImpl::load_sink(
    const std::filesystem::path& path, ConfigFormat format) {

    auto content_result = read_file(path);
    if (!content_result) {
        return common::Result<SinkConfig>(
            content_result.error_code(), content_result.error_message());
    }

    return parse_sink(content_result.value(), resolve_format(path, format));
}

common::Result<RouterConfig> ConfigLoaderImpl::load_router(
    const std::filesystem::path& path, ConfigFormat format) {

    auto content_result = read_file(path);
    if (!content_result) {
        return common::Result<RouterConfig>(
            content_result.error_code(), content_result.error_message());
    }

    return parse_router(content_result.value(), resolve_format(path, format));
}

common::Result<std::vector<ScoopConfig>> ConfigLoaderImpl::load_scoops_from_directory(
    const std::filesystem::path& dir_path, ConfigFormat format) {

    if (!std::filesystem::is_directory(dir_path)) {
        return common::Result<std::vector<ScoopConfig>>(
            common::ErrorCode::NOT_FOUND,
            "Directory not found: " + dir_path.string());
    }

    std::vector<ScoopConfig> configs;

    for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        bool is_yaml = (ext == ".yaml" || ext == ".yml");
        bool is_json = (ext == ".json");

        if (format == ConfigFormat::AUTO && !is_yaml && !is_json) continue;
        if (format == ConfigFormat::YAML && !is_yaml) continue;
        if (format == ConfigFormat::JSON && !is_json) continue;

        auto result = load_scoop(entry.path(), format);
        if (result) {
            configs.push_back(std::move(result.value()));
        }
    }

    return common::Result<std::vector<ScoopConfig>>(std::move(configs));
}

common::Result<std::vector<SinkConfig>> ConfigLoaderImpl::load_sinks_from_directory(
    const std::filesystem::path& dir_path, ConfigFormat format) {

    if (!std::filesystem::is_directory(dir_path)) {
        return common::Result<std::vector<SinkConfig>>(
            common::ErrorCode::NOT_FOUND,
            "Directory not found: " + dir_path.string());
    }

    std::vector<SinkConfig> configs;

    for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        bool is_yaml = (ext == ".yaml" || ext == ".yml");
        bool is_json = (ext == ".json");

        if (format == ConfigFormat::AUTO && !is_yaml && !is_json) continue;
        if (format == ConfigFormat::YAML && !is_yaml) continue;
        if (format == ConfigFormat::JSON && !is_json) continue;

        auto result = load_sink(entry.path(), format);
        if (result) {
            configs.push_back(std::move(result.value()));
        }
    }

    return common::Result<std::vector<SinkConfig>>(std::move(configs));
}

// ============================================================================
// STRING PARSING
// ============================================================================

common::Result<ApplicationConfig> ConfigLoaderImpl::parse_application(
    std::string_view content, ConfigFormat format) {

    if (format == ConfigFormat::AUTO) {
        format = detect_format_from_content(content);
    }

    try {
        if (format == ConfigFormat::JSON) {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::string errors;
            std::istringstream stream{std::string{content}};

            if (!Json::parseFromStream(builder, stream, &root, &errors)) {
                return common::Result<ApplicationConfig>(
                    common::ErrorCode::CONFIG_PARSE_ERROR,
                    "JSON parse error: " + errors);
            }

            return common::Result<ApplicationConfig>(parse_application_from_json(root));
        } else {
            YAML::Node root = YAML::Load(std::string(content));
            return common::Result<ApplicationConfig>(parse_application_from_yaml(root));
        }
    } catch (const std::exception& e) {
        return common::Result<ApplicationConfig>(
            common::ErrorCode::CONFIG_PARSE_ERROR,
            std::string("Parse error: ") + e.what());
    }
}

common::Result<ScoopConfig> ConfigLoaderImpl::parse_scoop(
    std::string_view content, ConfigFormat format) {

    if (format == ConfigFormat::AUTO) {
        format = detect_format_from_content(content);
    }

    try {
        if (format == ConfigFormat::JSON) {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::string errors;
            std::istringstream stream{std::string{content}};

            if (!Json::parseFromStream(builder, stream, &root, &errors)) {
                return common::Result<ScoopConfig>(
                    common::ErrorCode::CONFIG_PARSE_ERROR,
                    "JSON parse error: " + errors);
            }

            return common::Result<ScoopConfig>(parse_scoop_from_json(root));
        } else {
            YAML::Node root = YAML::Load(std::string(content));
            return common::Result<ScoopConfig>(parse_scoop_from_yaml(root));
        }
    } catch (const std::exception& e) {
        return common::Result<ScoopConfig>(
            common::ErrorCode::CONFIG_PARSE_ERROR,
            std::string("Parse error: ") + e.what());
    }
}

common::Result<SinkConfig> ConfigLoaderImpl::parse_sink(
    std::string_view content, ConfigFormat format) {

    if (format == ConfigFormat::AUTO) {
        format = detect_format_from_content(content);
    }

    try {
        if (format == ConfigFormat::JSON) {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::string errors;
            std::istringstream stream{std::string{content}};

            if (!Json::parseFromStream(builder, stream, &root, &errors)) {
                return common::Result<SinkConfig>(
                    common::ErrorCode::CONFIG_PARSE_ERROR,
                    "JSON parse error: " + errors);
            }

            return common::Result<SinkConfig>(parse_sink_from_json(root));
        } else {
            YAML::Node root = YAML::Load(std::string(content));
            return common::Result<SinkConfig>(parse_sink_from_yaml(root));
        }
    } catch (const std::exception& e) {
        return common::Result<SinkConfig>(
            common::ErrorCode::CONFIG_PARSE_ERROR,
            std::string("Parse error: ") + e.what());
    }
}

common::Result<RouterConfig> ConfigLoaderImpl::parse_router(
    std::string_view content, ConfigFormat format) {

    if (format == ConfigFormat::AUTO) {
        format = detect_format_from_content(content);
    }

    try {
        if (format == ConfigFormat::JSON) {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::string errors;
            std::istringstream stream{std::string{content}};

            if (!Json::parseFromStream(builder, stream, &root, &errors)) {
                return common::Result<RouterConfig>(
                    common::ErrorCode::CONFIG_PARSE_ERROR,
                    "JSON parse error: " + errors);
            }

            return common::Result<RouterConfig>(parse_router_from_json(root));
        } else {
            YAML::Node root = YAML::Load(std::string(content));
            return common::Result<RouterConfig>(parse_router_from_yaml(root));
        }
    } catch (const std::exception& e) {
        return common::Result<RouterConfig>(
            common::ErrorCode::CONFIG_PARSE_ERROR,
            std::string("Parse error: ") + e.what());
    }
}

// ============================================================================
// SERIALIZATION (Stubs - to be implemented)
// ============================================================================

common::Result<std::string> ConfigLoaderImpl::serialize_application(
    const ApplicationConfig& /*config*/, ConfigFormat /*format*/) {
    return common::Result<std::string>(
        common::ErrorCode::NOT_IMPLEMENTED,
        "Application serialization not yet implemented");
}

common::Result<std::string> ConfigLoaderImpl::serialize_scoop(
    const ScoopConfig& /*config*/, ConfigFormat /*format*/) {
    return common::Result<std::string>(
        common::ErrorCode::NOT_IMPLEMENTED,
        "Scoop serialization not yet implemented");
}

common::Result<std::string> ConfigLoaderImpl::serialize_sink(
    const SinkConfig& /*config*/, ConfigFormat /*format*/) {
    return common::Result<std::string>(
        common::ErrorCode::NOT_IMPLEMENTED,
        "Sink serialization not yet implemented");
}

common::Result<std::string> ConfigLoaderImpl::serialize_router(
    const RouterConfig& /*config*/, ConfigFormat /*format*/) {
    return common::Result<std::string>(
        common::ErrorCode::NOT_IMPLEMENTED,
        "Router serialization not yet implemented");
}

// ============================================================================
// FILE SAVING
// ============================================================================

common::Result<void> ConfigLoaderImpl::save_application(
    const ApplicationConfig& config, const std::filesystem::path& path, ConfigFormat format) {

    auto result = serialize_application(config, resolve_format(path, format));
    if (!result) {
        return common::Result<void>(result.error_code(), result.error_message());
    }

    return write_file(path, result.value());
}

common::Result<void> ConfigLoaderImpl::save_scoop(
    const ScoopConfig& config, const std::filesystem::path& path, ConfigFormat format) {

    auto result = serialize_scoop(config, resolve_format(path, format));
    if (!result) {
        return common::Result<void>(result.error_code(), result.error_message());
    }

    return write_file(path, result.value());
}

common::Result<void> ConfigLoaderImpl::save_sink(
    const SinkConfig& config, const std::filesystem::path& path, ConfigFormat format) {

    auto result = serialize_sink(config, resolve_format(path, format));
    if (!result) {
        return common::Result<void>(result.error_code(), result.error_message());
    }

    return write_file(path, result.value());
}

// ============================================================================
// VALIDATION
// ============================================================================

common::Result<void> ConfigLoaderImpl::validate(const ApplicationConfig& config) {
    if (config.name.empty()) {
        return common::Result<void>(
            common::ErrorCode::INVALID_ARGUMENT,
            "Application name is required");
    }

    // Validate scoops
    for (const auto& scoop : config.scoops) {
        auto result = validate(scoop);
        if (!result) {
            return result;
        }
    }

    // Validate sinks
    for (const auto& sink : config.sinks) {
        auto result = validate(sink);
        if (!result) {
            return result;
        }
    }

    // Validate router
    auto router_result = validate(config.router);
    if (!router_result) {
        return router_result;
    }

    return common::Result<void>();
}

common::Result<void> ConfigLoaderImpl::validate(const ScoopConfig& config) {
    if (config.id.empty()) {
        return common::Result<void>(
            common::ErrorCode::INVALID_ARGUMENT,
            "Scoop ID is required");
    }

    return common::Result<void>();
}

common::Result<void> ConfigLoaderImpl::validate(const SinkConfig& config) {
    if (config.id.empty()) {
        return common::Result<void>(
            common::ErrorCode::INVALID_ARGUMENT,
            "Sink ID is required");
    }

    return common::Result<void>();
}

common::Result<void> ConfigLoaderImpl::validate(const RouterConfig& /*config*/) {
    // Router has sensible defaults, no required fields
    return common::Result<void>();
}

} // namespace ipb::core::config
