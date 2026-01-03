#pragma once

#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace ipb {
namespace gate {

// MQTT configuration
struct MQTTSinkConfig {
    std::string broker_url = "tcp://localhost:1883";
    std::string client_id  = "ipb-gateway";
    std::string base_topic = "ipb/data";
    int qos                = 1;
    bool retain            = false;
};

// Console configuration
struct ConsoleSinkConfig {
    std::string format = "json";
    bool colored       = true;
};

// Syslog configuration
struct SyslogSinkConfig {
    std::string facility = "local0";
    std::string identity = "ipb-gateway";
};

// Generic sink configuration
struct SinkConfig {
    std::string id;
    std::string type;
    bool enabled = true;

    // Type-specific configurations
    MQTTSinkConfig mqtt_config;
    ConsoleSinkConfig console_config;
    SyslogSinkConfig syslog_config;
};

// Source filter configuration
struct SourceFilterConfig {
    std::string address_pattern;
    std::vector<std::string> protocol_ids;
    std::vector<std::string> quality_filters;
};

// Routing destination configuration
struct RoutingDestinationConfig {
    std::string sink_id;
    std::string priority = "normal";
};

// Routing rule configuration
struct RoutingRuleConfig {
    std::string name;
    bool enabled = true;
    SourceFilterConfig source_filter;
    std::vector<RoutingDestinationConfig> destinations;
};

// Gateway configuration
struct GatewaySettings {
    std::string name      = "ipb-gateway";
    std::string log_level = "info";
    int worker_threads    = 4;
};

// Complete gateway configuration (loaded from file)
struct LoadedConfig {
    GatewaySettings gateway;
    std::vector<SinkConfig> sinks;
    std::vector<RoutingRuleConfig> routing_rules;
};

// Configuration loader class
class ConfigLoader {
public:
    ConfigLoader();
    ~ConfigLoader();

    // Load configuration from file
    bool load_from_file(const std::string& config_file);

    // Load configuration from YAML string
    bool load_from_string(const std::string& config_yaml);

    // Get loaded configuration
    LoadedConfig get_config() const;

private:
    LoadedConfig config_;

    // Parse YAML configuration
    bool parse_config(const YAML::Node& root);

    // Parse sink-specific configurations
    void parse_mqtt_config(const YAML::Node& node, SinkConfig& config);
    void parse_console_config(const YAML::Node& node, SinkConfig& config);
    void parse_syslog_config(const YAML::Node& node, SinkConfig& config);
};

}  // namespace gate
}  // namespace ipb
